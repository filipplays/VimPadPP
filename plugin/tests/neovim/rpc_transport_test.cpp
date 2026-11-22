// Build normally as a DLL integration test, or with RPC_TEST_PEER as a faulty
// replacement neovim/bin/nvim.exe in an isolated test runtime directory.
#include "msgpack_frame.h"
#include "msgpack_value.h"
#include <iostream>
#include <string>
#include <windows.h>

#if defined(RPC_TEST_PEER)
namespace
{
std::string Bytes(std::initializer_list<unsigned char> aBytes) { return {aBytes.begin(), aBytes.end()}; }
bool        Write(const std::string &sBytes)
{
    DWORD nWritten = 0;
    return WriteFile(
               GetStdHandle(STD_OUTPUT_HANDLE), sBytes.data(), static_cast<DWORD>(sBytes.size()), &nWritten, nullptr) &&
           nWritten == sBytes.size();
}
bool ReadRequest(MsgPack::Value &sRequest)
{
    static std::string  sIncoming;
    static MsgPackFrame sFrame;
    for (;;)
    {
        const auto eFrame = sFrame.Scan(sIncoming);
        if (eFrame == MsgPackFrame::Result::INVALID)
            return false;
        if (eFrame == MsgPackFrame::Result::COMPLETE)
        {
            const size_t nSize = sFrame.Size();
            const bool   bOk   = MsgPack::Decoder(std::string_view(sIncoming).substr(0, nSize)).Decode(sRequest);
            sIncoming.erase(0, nSize);
            sFrame.Reset();
            return bOk;
        }
        char  aBuffer[4096];
        DWORD nRead = 0;
        if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), aBuffer, sizeof(aBuffer), &nRead, nullptr) || !nRead)
            return false;
        sIncoming.append(aBuffer, nRead);
    }
}
std::string String(const std::string &sText) { return std::string(1, static_cast<char>(0xa0 | sText.size())) + sText; }
std::string Response(int64_t nId, const std::string &sResult)
{
    std::string sReply = Bytes({0x94, 1, 0xce});
    for (int nShift = 24; nShift >= 0; nShift -= 8)
        sReply.push_back(static_cast<char>(nId >> nShift));
    return sReply + Bytes({0xc0}) + sResult;
}
} // namespace
int main()
{
    char aMode[64]{};
    GetEnvironmentVariableA("VIMPAD_RPC_TEST_MODE", aMode, sizeof(aMode));
    const std::string sMode = aMode;
    if (sMode == "no-read")
    {
        Sleep(INFINITE);
        return 0;
    }
    MsgPack::Value sRequest;
    while (ReadRequest(sRequest))
    {
        if (sRequest.nType != 3 || sRequest.aItems.size() != 4)
            return 2;
        const int64_t     nId     = sRequest.aItems[1].nInteger;
        const std::string sMethod = sRequest.aItems[2].sString;
        if (sMode == "notifications")
        {
            const std::string sNotice = Bytes({0x93, 2}) + String("vim_motions_save") + Bytes({0x90});
            while (Write(sNotice))
                Sleep(1);
            return 0;
        }
        if (sMode == "trickle")
        {
            for (char nByte : Response(nId, Bytes({0xc0})))
            {
                if (!Write(std::string(1, nByte)))
                    return 0;
                Sleep(1200);
            }
            Sleep(INFINITE);
        }
        if (sMode == "oversized-header" || sMode == "value-budget")
        {
            std::string sArray;
            MsgPack::AppendArrayHeader(sArray, sMode == "oversized-header" ? 16777216 : MsgPack::MaxValues);
            Write(Response(nId, sArray));
            Sleep(INFINITE);
        }
        std::string sResult = Bytes({0xc0});
        if (sMethod == "nvim_eval")
            sResult = String("");
        if (sMethod == "nvim_exec_lua")
            sResult = Bytes({0x92, 0xa0, 0x90}); // Valid empty plugin config.
        if (sMethod == "nvim_call_atomic")
        {
            // Three atomic results, including the accepted journal checkpoint.
            sResult = Bytes({0x92, 0x93, 0xc0, 0xc0, 0x99, 1, 1, 1, 0, 0, 0, 0x90, 0x92, 1, 0, 0x93}) + String("n") +
                      Bytes({0x94, 0, 1, 1, 0}) + String("inclusive") + Bytes({0xc0});
        }
        if (!Write(Response(nId, sResult)))
            return 0;
        if (sMode == "write-stall" && sMethod == "nvim_exec_lua")
            Sleep(INFINITE); // Stop reading before the large document arrives.
        if (sMode == "async-budget" && sMethod == "nvim_call_atomic")
        {
            std::string sNotice = Bytes({0x93, 2}) + String("ignored");
            MsgPack::AppendArrayHeader(sNotice, MsgPack::MaxValues);
            Write(sNotice);
            Sleep(INFINITE);
        }
    }
}
#else
#include "ScintillaMessages.h"
#include "VimMotions.h"
#include <tlhelp32.h>

namespace
{
unsigned         g_nErrors = 0;
LRESULT CALLBACK DialogHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HCBT_ACTIVATE)
    {
        HWND    hDialog = reinterpret_cast<HWND>(wParam);
        wchar_t aClass[32]{};
        GetClassNameW(hDialog, aClass, 32);
        if (!lstrcmpW(aClass, L"#32770"))
        {
            ++g_nErrors;
            PostMessageW(hDialog, WM_COMMAND, IDOK, 0);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
void Pump(unsigned nMilliseconds)
{
    const ULONGLONG nDeadline = GetTickCount64() + nMilliseconds;
    do
    {
        MSG sMessage;
        while (PeekMessageW(&sMessage, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageW(&sMessage);
        Sleep(1);
    } while (GetTickCount64() < nDeadline);
}
std::string Text(HWND hEditor)
{
    std::string sText(static_cast<size_t>(SendMessageW(hEditor, Scintilla::GETLENGTH, 0, 0)) + 1, '\0');
    SendMessageW(hEditor, Scintilla::GETTEXT, sText.size(), reinterpret_cast<LPARAM>(sText.data()));
    sText.pop_back();
    return sText;
}
bool NoLiveChildren()
{
    bool            bOk       = true;
    HANDLE          hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W sProcess{};
    sProcess.dwSize = sizeof(sProcess);
    if (Process32FirstW(hSnapshot, &sProcess))
        do
        {
            if (sProcess.th32ParentProcessID == GetCurrentProcessId() && !lstrcmpiW(sProcess.szExeFile, L"nvim.exe"))
                if (HANDLE hProcess = OpenProcess(SYNCHRONIZE, FALSE, sProcess.th32ProcessID))
                {
                    bOk = WaitForSingleObject(hProcess, 0) == WAIT_OBJECT_0 && bOk;
                    CloseHandle(hProcess);
                }
        } while (Process32NextW(hSnapshot, &sProcess));
    CloseHandle(hSnapshot);
    return bOk;
}
} // namespace

int main(int nArgumentCount, char **aArguments)
{
    const std::string sMode = nArgumentCount > 1 ? aArguments[1] : "large";
    SetEnvironmentVariableA("VIMPAD_RPC_TEST_MODE", sMode.c_str());
    HMODULE hScintilla = LoadLibraryW(L"Scintilla.dll");
    HMODULE hPlugin    = LoadLibraryW(L"VimMotions.dll");
    if (!hScintilla || !hPlugin)
        return 2;
    const auto pInstall   = reinterpret_cast<InstallVimFn>(GetProcAddress(hPlugin, "InstallVim"));
    const auto pUninstall = reinterpret_cast<UninstallVimFn>(GetProcAddress(hPlugin, "UninstallVim"));
    HWND       hEditor    = CreateWindowW(
        L"Scintilla", L"", WS_POPUP, 0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hEditor || !pInstall || !pUninstall)
        return 2;
    HHOOK hDialogHook = SetWindowsHookExW(WH_CBT, DialogHook, nullptr, GetCurrentThreadId());
    if (!hDialogHook)
        return 2;
    bool bOk = true;
    if (sMode == "large")
    {
        for (const size_t nLines : {65535u, 65536u, 65537u})
        {
            std::string sText = "aa";
            for (size_t nLine = 1; nLine < nLines; ++nLine)
                sText += "\naa";
            SendMessageW(hEditor, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>(sText.c_str()));
            SendMessageW(hEditor, Scintilla::SETSEL, 0, 0);
            if (!pInstall(hEditor))
                return 1;
            SendMessageW(hEditor, WM_CHAR, 'x', 0);
            Pump(200);
            bOk = Text(hEditor) == sText.substr(1) && bOk;
            // Exercise authoritative host replacement at the same boundary.
            sText.replace(0, 2, "bb");
            SendMessageW(hEditor, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>(sText.c_str()));
            SendMessageW(hEditor, Scintilla::SETSEL, 0, 0);
            SendMessageW(hEditor, WM_CHAR, 'x', 0);
            Pump(200);
            bOk = Text(hEditor) == sText.substr(1) && pUninstall(hEditor) && bOk;
        }
        bOk = !g_nErrors && bOk;
    }
    else
    {
        const std::string sText = sMode == "write-stall" ? std::string(2 * 1024 * 1024, 'x') : "host text";
        SendMessageW(hEditor, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>(sText.c_str()));
        const ULONGLONG nStart     = GetTickCount64();
        const bool      bInstalled = pInstall(hEditor);
        if (sMode == "async-budget")
        {
            bOk = bInstalled && bOk;
            Pump(100);
        }
        else
            bOk = !bInstalled && bOk;
        const ULONGLONG nElapsed = GetTickCount64() - nStart;
        const bool      bTimeout =
            sMode == "write-stall" || sMode == "no-read" || sMode == "notifications" || sMode == "trickle";
        // This measures process startup, the RPC deadline, cleanup, and dialog
        // handling together. Allow CI scheduling overhead while still bounding
        // the complete operation; the Python runner enforces a 20-second limit.
        const ULONGLONG nLimit = bTimeout ? 12000u : 6000u;
        if (nElapsed >= nLimit || (bTimeout && nElapsed < 4500))
        {
            std::cerr << "Unexpected elapsed time: " << nElapsed << " ms (limit " << nLimit << " ms)\n";
            bOk = false;
        }
        bOk = Text(hEditor) == sText && g_nErrors == 1 && NoLiveChildren() && bOk;
        // Cleanup must restore host editing without recreating the failed peer.
        SendMessageW(hEditor, Scintilla::SETSEL, 0, 0);
        SendMessageW(hEditor, WM_CHAR, 'Z', 0);
        bOk = Text(hEditor) == "Z" + sText && NoLiveChildren() && bOk;
        std::cout << sMode << " completed in " << nElapsed << " ms\n";
    }
    UnhookWindowsHookEx(hDialogHook);
    DestroyWindow(hEditor);
    FreeLibrary(hPlugin);
    FreeLibrary(hScintilla);
    std::cout << (bOk ? "RPC transport checks passed\n" : "RPC transport checks FAILED\n");
    return bOk ? 0 : 1;
}
#endif
