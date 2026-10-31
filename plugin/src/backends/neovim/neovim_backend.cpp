#include "NeovimBackend.h"
#include "ScintillaMessages.h"

#include <commctrl.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace NeovimBackend
{
namespace
{
constexpr UINT_PTR NeovimResponseTimerId = 0x4E56494D;
struct Value
{
    int                nType    = 0; // 0 nil, 1 integer, 2 string, 3 array, 4 other
    int64_t            nInteger = 0;
    std::string        sString;
    std::vector<Value> aItems;
};
struct Session
{
    HWND        hWindow   = nullptr;
    LRESULT     nDocument = 0;
    HANDLE      hProcess  = nullptr;
    HANDLE      hInput    = nullptr;
    HANDLE      hOutput   = nullptr;
    unsigned    nId       = 0;
    std::string sText;
    bool        bFailed = false;
    std::string sIncoming;
    size_t      nReadOffset    = 0;
    bool        bBufferedRead  = false;
    unsigned    nStateId       = 0;
    unsigned    nRevision      = 0;
    unsigned    nStateRevision = 0;
    UINT_PTR    nTimer         = 0;
};
std::unordered_map<HWND, Session> g_aSessions;
Session                          *g_pSession = nullptr;
#define g_sSession (*g_pSession)

void Close(HANDLE &hHandle)
{
    if (hHandle && hHandle != INVALID_HANDLE_VALUE)
        CloseHandle(hHandle);
    hHandle = nullptr;
}
void Stop()
{
    if (g_sSession.nTimer)
        KillTimer(g_sSession.hWindow, g_sSession.nTimer);
    g_sSession.nTimer = 0;
    Close(g_sSession.hInput);
    if (g_sSession.hProcess)
    {
        if (WaitForSingleObject(g_sSession.hProcess, 1000) != WAIT_OBJECT_0)
        {
            TerminateProcess(g_sSession.hProcess, 1);
            WaitForSingleObject(g_sSession.hProcess, 1000);
        }
    }
    Close(g_sSession.hOutput);
    Close(g_sSession.hProcess);
}
void Number(std::string &sData, uint64_t nValue, int nBytes)
{
    for (int nShift = (nBytes - 1) * 8; nShift >= 0; nShift -= 8)
        sData.push_back(static_cast<char>(nValue >> nShift));
}
void String(std::string &sData, const std::string &sValue)
{
    sData.push_back(static_cast<char>(0xdb));
    Number(sData, sValue.size(), 4);
    sData += sValue;
}
bool Read(void *pData, size_t nSize)
{
    if (g_sSession.bBufferedRead)
    {
        if (nSize > g_sSession.sIncoming.size() - g_sSession.nReadOffset)
            return false;
        std::memcpy(pData, g_sSession.sIncoming.data() + g_sSession.nReadOffset, nSize);
        g_sSession.nReadOffset += nSize;
        return true;
    }
    auto     *pBytes    = static_cast<char *>(pData);
    ULONGLONG nDeadline = GetTickCount64() + 5000;
    while (nSize)
    {
        DWORD nAvailable = 0, nRead = 0;
        if (!PeekNamedPipe(g_sSession.hOutput, nullptr, 0, nullptr, &nAvailable, nullptr))
            return false;
        if (!nAvailable)
        {
            if (WaitForSingleObject(g_sSession.hProcess, 0) == WAIT_OBJECT_0 || GetTickCount64() >= nDeadline)
                return false;
            Sleep(1);
            continue;
        }
        if (!ReadFile(
                g_sSession.hOutput, pBytes, static_cast<DWORD>(nSize < nAvailable ? nSize : nAvailable), &nRead,
                nullptr) ||
            !nRead)
            return false;
        pBytes += nRead;
        nSize -= nRead;
    }
    return true;
}
bool ReadNumber(int nBytes, uint64_t &nValue)
{
    nValue = 0;
    for (int nIndex = 0; nIndex < nBytes; ++nIndex)
    {
        unsigned char nByte;
        if (!Read(&nByte, 1))
            return false;
        nValue = (nValue << 8) | nByte;
    }
    return true;
}
bool Decode(Value &sValue, int nDepth = 0)
{
    if (nDepth > 32)
        return false;
    unsigned char nTag;
    if (!Read(&nTag, 1))
        return false;
    uint64_t nLength = 0;
    if (nTag <= 0x7f)
    {
        sValue.nType    = 1;
        sValue.nInteger = nTag;
        return true;
    }
    if (nTag >= 0xe0)
    {
        sValue.nType    = 1;
        sValue.nInteger = static_cast<int8_t>(nTag);
        return true;
    }
    if ((nTag & 0xe0) == 0xa0)
    {
        sValue.nType = 2;
        nLength      = nTag & 31;
    }
    else if ((nTag & 0xf0) == 0x90)
    {
        sValue.nType = 3;
        nLength      = nTag & 15;
    }
    else if (nTag == 0xc0)
        return true;
    else if (nTag == 0xc2 || nTag == 0xc3)
    {
        sValue.nType = 4;
        return true;
    }
    else if (nTag >= 0xcc && nTag <= 0xcf)
    {
        sValue.nType = 1;
        if (!ReadNumber(1 << (nTag - 0xcc), nLength))
            return false;
        sValue.nInteger = static_cast<int64_t>(nLength);
        return true;
    }
    else if (nTag >= 0xd0 && nTag <= 0xd3)
    {
        int nBytes = 1 << (nTag - 0xd0);
        if (!ReadNumber(nBytes, nLength))
            return false;
        sValue.nType    = 1;
        sValue.nInteger = nBytes == 8 ? static_cast<int64_t>(nLength)
                                      : static_cast<int64_t>(nLength << (64 - nBytes * 8)) >> (64 - nBytes * 8);
        return true;
    }
    else if (nTag == 0xd9 || nTag == 0xda || nTag == 0xdb || nTag == 0xc4 || nTag == 0xc5 || nTag == 0xc6)
    {
        sValue.nType = 2;
        if (!ReadNumber(nTag == 0xd9 || nTag == 0xc4 ? 1 : nTag == 0xda || nTag == 0xc5 ? 2 : 4, nLength))
            return false;
    }
    else if (nTag == 0xdc || nTag == 0xdd)
    {
        sValue.nType = 3;
        if (!ReadNumber(nTag == 0xdc ? 2 : 4, nLength))
            return false;
    }
    else
        return false;
    if (nLength > 16 * 1024 * 1024)
        return false;
    if (sValue.nType == 2)
    {
        sValue.sString.resize(static_cast<size_t>(nLength));
        return !nLength || Read(sValue.sString.data(), static_cast<size_t>(nLength));
    }
    sValue.aItems.resize(static_cast<size_t>(nLength));
    for (auto &sItem : sValue.aItems)
        if (!Decode(sItem, nDepth + 1))
            return false;
    return true;
}
void Array(std::string &sData, size_t nSize)
{
    sData.push_back(static_cast<char>(0xdc));
    Number(sData, nSize, 2);
}
void Integer(std::string &sData, int64_t nValue)
{
    sData.push_back(static_cast<char>(0xd3));
    Number(sData, static_cast<uint64_t>(nValue), 8);
}
void Boolean(std::string &sData, bool bValue) { sData.push_back(static_cast<char>(bValue ? 0xc3 : 0xc2)); }
bool SendRpc(const std::string &sMethod, const std::string &sArguments, unsigned &nId, bool bNotification = false)
{
    std::string sRequest;
    sRequest.push_back(static_cast<char>(bNotification ? 0x93 : 0x94));
    sRequest.push_back(static_cast<char>(bNotification ? 2 : 0));
    if (!bNotification)
    {
        nId = ++g_sSession.nId;
        sRequest.push_back(static_cast<char>(0xce));
        Number(sRequest, nId, 4);
    }
    String(sRequest, sMethod);
    sRequest += sArguments;
    DWORD nWritten = 0;
    std::cout << "---Request: " << sRequest << std::endl;
    return WriteFile(g_sSession.hInput, sRequest.data(), static_cast<DWORD>(sRequest.size()), &nWritten, nullptr) &&
           nWritten == sRequest.size();
}
bool Rpc(const std::string &sMethod, const std::string &sArguments, Value &sResult)
{
    unsigned nId = 0;
    if (!SendRpc(sMethod, sArguments, nId))
        return false;
    Value sResponse;
    if (!Decode(sResponse) || sResponse.nType != 3 || sResponse.aItems.size() != 4 ||
        sResponse.aItems[0].nInteger != 1 || sResponse.aItems[1].nInteger != nId || sResponse.aItems[2].nType != 0)
        return false;
    sResult = std::move(sResponse.aItems[3]);
    return true;
}
std::string StateArguments()
{
    std::string sArgs;
    Array(sArgs, 1);
    Array(sArgs, 2);
    Array(sArgs, 2);
    String(sArgs, "nvim_buf_get_lines");
    Array(sArgs, 4);
    Integer(sArgs, 0);
    Integer(sArgs, 0);
    Integer(sArgs, -1);
    Boolean(sArgs, true);
    Array(sArgs, 2);
    String(sArgs, "nvim_win_get_cursor");
    Array(sArgs, 1);
    Integer(sArgs, 0);
    return sArgs;
}
bool RequestState()
{
    std::cout << "Request state" << std::endl;
    if (g_sSession.nStateId)
        return true;
    g_sSession.nStateRevision = g_sSession.nRevision;
    return SendRpc("nvim_call_atomic", StateArguments(), g_sSession.nStateId);
}
std::string Normalize(const std::string &sText);
bool        SyncDocument(const std::string &sText, LRESULT nLine, LRESULT nColumn)
{
    std::cout << "Document SYNC" << std::endl;
    std::string sArgs;
    Array(sArgs, 1);
    Array(sArgs, 2);
    Array(sArgs, 2);
    String(sArgs, "nvim_buf_set_lines");
    Array(sArgs, 5);
    Integer(sArgs, 0);
    Integer(sArgs, 0);
    Integer(sArgs, -1);
    Boolean(sArgs, true);
    std::string sNormalized = Normalize(sText);
    size_t      nLines      = 1;
    for (char nChar : sNormalized)
        if (nChar == '\n')
            ++nLines;
    Array(sArgs, nLines);
    size_t nStart = 0;
    for (size_t nIndex = 0; nIndex <= sNormalized.size(); ++nIndex)
        if (nIndex == sNormalized.size() || sNormalized[nIndex] == '\n')
        {
            String(sArgs, sNormalized.substr(nStart, nIndex - nStart));
            nStart = nIndex + 1;
        }
    Array(sArgs, 2);
    String(sArgs, "nvim_win_set_cursor");
    Array(sArgs, 2);
    Integer(sArgs, 0);
    Array(sArgs, 2);
    Integer(sArgs, nLine + 1);
    Integer(sArgs, nColumn);
    Value sResult;
    return Rpc("nvim_call_atomic", sArgs, sResult) && sResult.nType == 3 && sResult.aItems.size() == 2 &&
           sResult.aItems[1].nType == 0;
}
std::string Text(HWND hWindow)
{
    size_t      nLength = static_cast<size_t>(SendMessageW(hWindow, Scintilla::GETLENGTH, 0, 0));
    std::string sText(nLength + 1, '\0');
    SendMessageW(hWindow, Scintilla::GETTEXT, nLength + 1, reinterpret_cast<LPARAM>(sText.data()));
    sText.resize(nLength);
    return sText;
}
bool Supported(const std::string &sText)
{
    for (unsigned char nChar : sText)
        if (nChar == 0)
            return false;
    return true;
}
std::string Normalize(const std::string &sText)
{
    std::string sResult;
    for (size_t nIndex = 0; nIndex < sText.size(); ++nIndex)
    {
        if (sText[nIndex] == '\r')
        {
            sResult += '\n';
            if (nIndex + 1 < sText.size() && sText[nIndex + 1] == '\n')
                ++nIndex;
        }
        else
            sResult += sText[nIndex];
    }
    return sResult;
}
std::string RestoreEol(HWND hWindow, const std::string &sText)
{
    LRESULT nEol = SendMessageW(hWindow, Scintilla::GETEOLMODE, 0, 0);
    if (g_sSession.sText.find('\n') != std::string::npos)
        nEol = g_sSession.sText.find("\r\n") != std::string::npos ? Scintilla::EOL_CRLF : Scintilla::EOL_LF;
    else if (g_sSession.sText.find('\r') != std::string::npos)
        nEol = Scintilla::EOL_CR;
    if (nEol == Scintilla::EOL_LF)
        return sText;
    std::string sResult;
    for (char nChar : sText)
        if (nChar == '\n')
            sResult += nEol == Scintilla::EOL_CRLF ? "\r\n" : "\r";
        else
            sResult += nChar;
    return sResult;
}
bool Start()
{
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Install), &hModule))
        return false;
    wchar_t aPath[32768];
    DWORD   nLength = GetModuleFileNameW(hModule, aPath, 32768);
    if (!nLength || nLength == 32768)
        return false;
    std::wstring sPath(aPath, nLength);
    size_t       nSlash = sPath.find_last_of(L"\\/");
    if (nSlash == std::wstring::npos)
        return false;
    sPath = sPath.substr(0, nSlash) + L"\\neovim\\bin\\nvim.exe";
    SECURITY_ATTRIBUTES sSecurity{sizeof(sSecurity), nullptr, TRUE};
    HANDLE              hChildInput = nullptr, hChildOutput = nullptr;
    if (!CreatePipe(&hChildInput, &g_sSession.hInput, &sSecurity, 0) ||
        !CreatePipe(&g_sSession.hOutput, &hChildOutput, &sSecurity, 0))
    {
        Close(hChildInput);
        Close(hChildOutput);
        return false;
    }
    SetHandleInformation(g_sSession.hInput, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(g_sSession.hOutput, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW sStartup{};
    sStartup.cb         = sizeof(sStartup);
    sStartup.dwFlags    = STARTF_USESTDHANDLES;
    sStartup.hStdInput  = hChildInput;
    sStartup.hStdOutput = hChildOutput;
    sStartup.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION sProcess{};
    std::wstring        sCommand = L"\"" + sPath + L"\" --embed --headless -u NONE -i NONE -n";

    BOOL bStarted = CreateProcessW(
        sPath.c_str(), sCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &sStartup,
        &sProcess);
    Close(hChildInput);
    Close(hChildOutput);
    if (!bStarted)
        return false;
    CloseHandle(sProcess.hThread);
    g_sSession.hProcess = sProcess.hProcess;
    return true;
}
void Fail(const wchar_t *pMessage)
{
    g_sSession.bFailed = true;
    Stop();
    MessageBoxW(g_sSession.hWindow, pMessage, L"Neovim prototype", MB_OK | MB_ICONERROR);
}
bool ApplyState(const Value &sResult)
{
    std::cout << "APPLY STATE" << std::endl;
    if (sResult.nType != 3 || sResult.aItems.size() != 2 || sResult.aItems[1].nType != 0 ||
        sResult.aItems[0].nType != 3 || sResult.aItems[0].aItems.size() != 2)
        return false;
    const Value &sLines  = sResult.aItems[0].aItems[0];
    const Value &sCursor = sResult.aItems[0].aItems[1];
    if (sLines.nType != 3 || sCursor.nType != 3 || sCursor.aItems.size() != 2 || sCursor.aItems[0].nType != 1 ||
        sCursor.aItems[1].nType != 1)
        return false;
    std::string sText;
    for (size_t nIndex = 0; nIndex < sLines.aItems.size(); ++nIndex)
    {
        if (sLines.aItems[nIndex].nType != 2)
            return false;
        if (nIndex)
            sText += '\n';
        sText += sLines.aItems[nIndex].sString;
    }
    HWND hWindow = g_sSession.hWindow;
    std::cout << "Got text: " << sText << std::endl;
    if (SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0) != g_sSession.nDocument ||
        Text(hWindow) != g_sSession.sText)
        return true;
    const std::string sNewText = RestoreEol(hWindow, sText);
    if (sNewText != g_sSession.sText)
    {
        size_t nStart = 0;
        while (nStart < sNewText.size() && nStart < g_sSession.sText.size() &&
               sNewText[nStart] == g_sSession.sText[nStart])
            ++nStart;
        size_t nOldEnd = g_sSession.sText.size(), nNewEnd = sNewText.size();
        while (nOldEnd > nStart && nNewEnd > nStart && g_sSession.sText[nOldEnd - 1] == sNewText[nNewEnd - 1])
        {
            --nOldEnd;
            --nNewEnd;
        }
        SendMessageW(hWindow, Scintilla::SETTARGETSTART, nStart, 0);
        SendMessageW(hWindow, Scintilla::SETTARGETEND, nOldEnd, 0);
        SendMessageW(
            hWindow, Scintilla::REPLACETARGET, nNewEnd - nStart, reinterpret_cast<LPARAM>(sNewText.data() + nStart));
        g_sSession.sText = sNewText;
    }
    LRESULT nStart =
        SendMessageW(hWindow, Scintilla::POSITIONFROMLINE, static_cast<WPARAM>(sCursor.aItems[0].nInteger - 1), 0);
    SendMessageW(hWindow, Scintilla::GOTOPOS, static_cast<WPARAM>(nStart + sCursor.aItems[1].nInteger), 0);
    return true;
}
bool PollResponses()
{
    std::cout << "--POLL RESPONSE--" << std::endl;
    DWORD nAvailable = 0;
    if (!PeekNamedPipe(g_sSession.hOutput, nullptr, 0, nullptr, &nAvailable, nullptr))
        return false;
    while (nAvailable)
    {
        char  aBuffer[4096];
        DWORD nRead = 0;
        if (!ReadFile(
                g_sSession.hOutput, aBuffer, nAvailable < sizeof(aBuffer) ? nAvailable : sizeof(aBuffer), &nRead,
                nullptr) ||
            !nRead)
            return false;
        g_sSession.sIncoming.append(aBuffer, nRead);
        nAvailable -= nRead;
    }
    if (g_sSession.sIncoming.size() > 32 * 1024 * 1024)
        return false;
    for (;;)
    {
        g_sSession.nReadOffset   = 0;
        g_sSession.bBufferedRead = true;
        Value sResponse;
        bool  bComplete          = Decode(sResponse);
        g_sSession.bBufferedRead = false;
        if (!bComplete)
            break;
        g_sSession.sIncoming.erase(0, g_sSession.nReadOffset);
        if (sResponse.nType != 3 || sResponse.aItems.size() != 4 || sResponse.aItems[0].nInteger != 1 ||
            sResponse.aItems[1].nInteger != g_sSession.nStateId || sResponse.aItems[2].nType != 0)
            return false;
        g_sSession.nStateId = 0;
        if (g_sSession.nStateRevision != g_sSession.nRevision)
        {
            if (!RequestState())
                return false;
        }
        else if (!ApplyState(sResponse.aItems[3]))
            return false;
    }
    if (!g_sSession.nStateId)
    {
        KillTimer(g_sSession.hWindow, g_sSession.nTimer);
        g_sSession.nTimer = 0;
    }
    return true;
}
LRESULT CALLBACK Subclass(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    auto itSession = g_aSessions.find(hWindow);
    if (itSession == g_aSessions.end())
        return DefSubclassProc(hWindow, nMessage, wParam, lParam);
    g_pSession = &itSession->second;
    if (nMessage == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hWindow, Subclass, 0);
        Stop();
        g_aSessions.erase(itSession);
    }
    else if (nMessage == WM_TIMER && wParam == g_sSession.nTimer && g_sSession.nTimer)
    {
        if (!PollResponses())
            Fail(L"Neovim communication failed.");
        return 0;
    }
    else if ((nMessage == WM_CHAR || nMessage == WM_KEYDOWN) && !g_sSession.bFailed)
    {
        std::string sKey;
        if (nMessage == WM_KEYDOWN)
        {
            switch (wParam)
            {
            case VK_LEFT:
                sKey = "<Left>";
                break;
            case VK_RIGHT:
                sKey = "<Right>";
                break;
            case VK_UP:
                sKey = "<Up>";
                break;
            case VK_DOWN:
                sKey = "<Down>";
                break;
            case VK_DELETE:
                sKey = "<Del>";
                break;
            default:
                return DefSubclassProc(hWindow, nMessage, wParam, lParam);
            }
        }
        else if (wParam == 27)
            sKey = "<Esc>";
        else if (wParam == 13)
            sKey = "<CR>";
        else if (wParam == 8)
            sKey = "<BS>";
        else if (wParam == 9)
            sKey = "<Tab>";
        else if (wParam >= 1 && wParam <= 26)
            sKey = std::string("<C-") + static_cast<char>('a' + wParam - 1) + ">";
        else if (wParam == '<')
            sKey = "<lt>";
        else if (wParam < 128)
            sKey.assign(1, static_cast<char>(wParam));
        else
        {
            wchar_t nChar = static_cast<wchar_t>(wParam);
            char    aUtf8[4];
            int     nBytes = WideCharToMultiByte(CP_UTF8, 0, &nChar, 1, aUtf8, sizeof(aUtf8), nullptr, nullptr);
            if (nBytes <= 0)
                return 0;
            sKey.assign(aUtf8, nBytes);
        }
        if (SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0) != g_sSession.nDocument ||
            Text(hWindow) != g_sSession.sText)
        {
            std::string sText = Text(hWindow);
            if (!Supported(sText))
            {
                Fail(
                    L"The Neovim prototype supports ASCII text with LF line "
                    L"endings only.");
                return 0;
            }
            LRESULT nPosition = SendMessageW(hWindow, Scintilla::GETCURRENTPOS, 0, 0);
            LRESULT nLine     = SendMessageW(hWindow, Scintilla::LINEFROMPOSITION, nPosition, 0);
            LRESULT nColumn   = nPosition - SendMessageW(hWindow, Scintilla::POSITIONFROMLINE, nLine, 0);
            if (g_sSession.nStateId || !SyncDocument(sText, nLine, nColumn))
            {
                Fail(L"Could not synchronize the document with Neovim.");
                return 0;
            }
            g_sSession.sText     = sText;
            g_sSession.nDocument = SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0);
        }
        std::string sArgs;
        Array(sArgs, 1);
        String(sArgs, sKey);
        unsigned nUnused = 0;
        ++g_sSession.nRevision;
        if (!SendRpc("nvim_input", sArgs, nUnused, true) || !RequestState())
        {
            Fail(L"Neovim communication failed.");
            return 0;
        }
        if (!g_sSession.nTimer)
        {
            if (SetTimer(hWindow, NeovimResponseTimerId, 10, nullptr))
                g_sSession.nTimer = NeovimResponseTimerId;
        }
        if (!g_sSession.nTimer)
            Fail(L"Could not start the Neovim response timer.");
        return 0;
    }
    return DefSubclassProc(hWindow, nMessage, wParam, lParam);
}
} // namespace

BOOL Install(HWND hWindow)
{
    wchar_t aClass[32]{};
    if (!IsWindow(hWindow) || !GetClassNameW(hWindow, aClass, 32) || lstrcmpW(aClass, L"Scintilla"))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (g_aSessions.contains(hWindow))
        return TRUE;
    g_pSession           = &g_aSessions[hWindow];
    g_sSession.hWindow   = hWindow;
    g_sSession.nDocument = SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0);
    g_sSession.sText     = Text(hWindow);
    if (!Supported(g_sSession.sText))
    {
        Fail(
            L"The Neovim prototype does not support NUL bytes "
            L"only.");
        g_aSessions.erase(hWindow);
        SetLastError(ERROR_NOT_SUPPORTED);
        return FALSE;
    }
    if (!Start())
    {
        Fail(
            L"Could not start bundled Neovim. Check that neovim\\bin\\nvim.exe "
            L"is installed beside the plugin.");
        g_aSessions.erase(hWindow);
        SetLastError(ERROR_PROCESS_ABORTED);
        return FALSE;
    }
    LRESULT nPosition = SendMessageW(hWindow, Scintilla::GETCURRENTPOS, 0, 0);
    LRESULT nLine     = SendMessageW(hWindow, Scintilla::LINEFROMPOSITION, nPosition, 0);
    LRESULT nColumn   = nPosition - SendMessageW(hWindow, Scintilla::POSITIONFROMLINE, nLine, 0);
    if (!SyncDocument(g_sSession.sText, nLine, nColumn))
    {
        Fail(L"Neovim initialization failed or timed out.");
        g_aSessions.erase(hWindow);
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    if (!SetWindowSubclass(hWindow, Subclass, 0, 0))
    {
        Stop();
        g_aSessions.erase(hWindow);
        return FALSE;
    }
    return TRUE;
}
BOOL Uninstall(HWND hWindow)
{
    auto itSession = g_aSessions.find(hWindow);
    if (itSession == g_aSessions.end())
    {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }
    g_pSession = &itSession->second;
    if (!RemoveWindowSubclass(hWindow, Subclass, 0))
        return FALSE;
    Stop();
    g_aSessions.erase(itSession);
    return TRUE;
}
} // namespace NeovimBackend
