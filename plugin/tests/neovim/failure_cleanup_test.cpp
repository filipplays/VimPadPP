// Run beside the Neovim VimPad++.dll, Scintilla.dll and staged neovim/.
#include "ScintillaMessages.h"
#include <iostream>
#include <string>
#include <tlhelp32.h>
#include <utility>
#include <vector>
#include <windows.h>

namespace
{
struct NppData
{
    HWND hNotepad;
    HWND hMainScintilla;
    HWND hSecondScintilla;
};
struct FuncItem
{
    wchar_t aName[64];
    void (*pFunction)();
    int   nCommandId;
    bool  bChecked;
    void *pShortcut;
};
NppData g_sNpp{};
bool    g_bChecked        = false;
bool    g_bDialogSeen     = false;
bool    g_bModalKeyPassed = false;
int     g_nFindCommands   = 0;
HACCEL  g_hAccelerators   = nullptr;

LRESULT CALLBACK HostProc(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam)
{
    if (nMessage == WM_USER + 1040) // NPPM_SETMENUITEMCHECK
    {
        g_bChecked = lParam != 0;
        return TRUE;
    }
    if (nMessage == WM_USER + 1023) // NPPM_GETCURRENTDOCINDEX
        return 0;
    if (nMessage == WM_USER + 1059) // NPPM_GETBUFFERIDFROMPOS
        return lParam + 1;
    if (nMessage == WM_COMMAND && LOWORD(wParam) == 300)
    {
        ++g_nFindCommands;
        return 0;
    }
    return DefWindowProcW(hWindow, nMessage, wParam, lParam);
}

void Pump(unsigned nMilliseconds)
{
    const ULONGLONG nDeadline = GetTickCount64() + nMilliseconds;
    do
    {
        MSG sMessage;
        while (PeekMessageW(&sMessage, nullptr, 0, 0, PM_REMOVE))
        {
            if (!TranslateAcceleratorW(g_sNpp.hNotepad, g_hAccelerators, &sMessage))
            {
                TranslateMessage(&sMessage);
                DispatchMessageW(&sMessage);
            }
        }
        Sleep(1);
    } while (GetTickCount64() < nDeadline);
}

void FindShortcut()
{
    SetForegroundWindow(g_sNpp.hNotepad);
    SetFocus(g_sNpp.hMainScintilla);
    BYTE aOriginal[256]{}, aState[256]{};
    GetKeyboardState(aOriginal);
    aState[VK_CONTROL] = 0x80;
    SetKeyboardState(aState);
    PostMessageW(g_sNpp.hMainScintilla, WM_KEYDOWN, 'F', 1);
    Pump(100);
    SetKeyboardState(aOriginal);
}

LRESULT CALLBACK DialogHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HCBT_ACTIVATE)
    {
        HWND    hDialog = reinterpret_cast<HWND>(wParam);
        wchar_t aClass[32]{};
        GetClassNameW(hDialog, aClass, 32);
        if (!lstrcmpW(aClass, L"#32770"))
        {
            g_bDialogSeen = true;
            // Reenter the keyboard hook while the failed backend is still on
            // the stack. It must pass input through without deleting sessions.
            BYTE aOriginal[256]{}, aState[256]{};
            GetKeyboardState(aOriginal);
            aState[VK_CONTROL] = 0x80;
            SetKeyboardState(aState);
            SetFocus(g_sNpp.hMainScintilla);
            PostMessageW(g_sNpp.hMainScintilla, WM_KEYDOWN, 'F', 1);
            MSG sMessage{};
            g_bModalKeyPassed = PeekMessageW(&sMessage, g_sNpp.hMainScintilla, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE) &&
                                sMessage.message == WM_KEYDOWN;
            SetKeyboardState(aOriginal);
            PostMessageW(hDialog, WM_COMMAND, IDOK, 0);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

std::vector<HANDLE> Children()
{
    std::vector<HANDLE> aChildren;
    HANDLE              hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W     sProcess{};
    sProcess.dwSize = sizeof(sProcess);
    if (Process32FirstW(hSnapshot, &sProcess))
        do
        {
            if (sProcess.th32ParentProcessID == GetCurrentProcessId() && !lstrcmpiW(sProcess.szExeFile, L"nvim.exe"))
                if (HANDLE hProcess = OpenProcess(
                        SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                        sProcess.th32ProcessID))
                    aChildren.push_back(hProcess);
        } while (Process32NextW(hSnapshot, &sProcess));
    CloseHandle(hSnapshot);
    return aChildren;
}

bool Stopped(std::vector<HANDLE> &aChildren)
{
    bool bOk = true;
    for (HANDLE hProcess : aChildren)
    {
        bOk = (WaitForSingleObject(hProcess, 1000) == WAIT_OBJECT_0) && bOk;
        CloseHandle(hProcess);
    }
    return bOk;
}
} // namespace

int main()
{
    HMODULE hScintilla = LoadLibraryW(L"Scintilla.dll");
    HMODULE hPlugin    = LoadLibraryW(L"VimPad++.dll");
    if (!hScintilla || !hPlugin)
        return 2;
    const auto pSetInfo      = reinterpret_cast<void (*)(NppData)>(GetProcAddress(hPlugin, "setInfo"));
    const auto pGetFunctions = reinterpret_cast<FuncItem *(*)(int *)>(GetProcAddress(hPlugin, "getFuncsArray"));
    const auto pNotify       = reinterpret_cast<void (*)(void *)>(GetProcAddress(hPlugin, "beNotified"));
    if (!pSetInfo || !pGetFunctions || !pNotify)
        return 2;
    WNDCLASSW sClass{};
    sClass.lpfnWndProc   = HostProc;
    sClass.hInstance     = GetModuleHandleW(nullptr);
    sClass.lpszClassName = L"FailureCleanupTest";
    RegisterClassW(&sClass);
    g_sNpp.hNotepad = CreateWindowW(
        sClass.lpszClassName, L"Failure cleanup", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 640, 400, nullptr, nullptr,
        sClass.hInstance, nullptr);
    for (HWND *pView : {&g_sNpp.hMainScintilla, &g_sNpp.hSecondScintilla})
    {
        *pView = CreateWindowW(
            L"Scintilla", L"", WS_CHILD | WS_VISIBLE, 0, 0, 320, 300, g_sNpp.hNotepad, nullptr, sClass.hInstance,
            nullptr);
        if (!*pView)
            return 2;
        SendMessageW(*pView, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>("abcdef"));
    }
    pSetInfo(g_sNpp);
    int       nCount     = 0;
    FuncItem *pFunctions = pGetFunctions(&nCount);
    if (!nCount)
        return 2;
    pFunctions[0].nCommandId = 200;
    ACCEL sAccelerator{FVIRTKEY | FCONTROL, 'F', 300};
    g_hAccelerators   = CreateAcceleratorTableW(&sAccelerator, 1);
    HHOOK hDialogHook = SetWindowsHookExW(WH_CBT, DialogHook, nullptr, GetCurrentThreadId());
    if (!hDialogHook)
        return 2;
    NMHDR sReady{g_sNpp.hNotepad, 0, 1001}; // NPPN_READY
    pNotify(&sReady);
    Pump(100);
    FindShortcut();
    auto aChildren = Children();
    bool bOk       = g_bChecked && aChildren.size() == 2 && g_nFindCommands == 0;
    if (!bOk)
    {
        std::cerr << "Initial enable failed\n";
        return 1;
    }
    // Keep a host reference to the first document while it is inactive, then
    // create a third session in its view. Failure must stop inactive sessions too.
    const LRESULT nFirstDocument = SendMessageW(g_sNpp.hMainScintilla, Scintilla::GETDOCPOINTER, 0, 0);
    SendMessageW(g_sNpp.hMainScintilla, Scintilla::ADDREFDOCUMENT, 0, nFirstDocument);
    const LRESULT nThirdDocument = SendMessageW(g_sNpp.hMainScintilla, Scintilla::CREATEDOCUMENT, 0, 0);
    SendMessageW(g_sNpp.hMainScintilla, Scintilla::SETDOCPOINTER, 0, nThirdDocument);
    SendMessageW(g_sNpp.hMainScintilla, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>("abcdef"));
    NMHDR sActivated{g_sNpp.hNotepad, 3, 1010}; // NPPN_BUFFERACTIVATED
    pNotify(&sActivated);
    auto   aAllChildren = Children();
    HANDLE hVictim      = nullptr;
    for (HANDLE hProcess : aAllChildren)
        if (GetProcessId(hProcess) != GetProcessId(aChildren[0]) &&
            GetProcessId(hProcess) != GetProcessId(aChildren[1]))
            hVictim = hProcess;
    for (HANDLE hProcess : aChildren)
        CloseHandle(hProcess);
    aChildren = std::move(aAllChildren);
    if (!hVictim || aChildren.size() != 3)
        return 2;
    bOk = TerminateProcess(hVictim, 1) && bOk;
    Pump(500);
    bOk = Stopped(aChildren) && !g_bChecked && g_bDialogSeen && g_bModalKeyPassed && bOk;
    if (!bOk)
        std::cerr << "Failure cleanup/menu/modal routing failed\n";
    SendMessageW(g_sNpp.hMainScintilla, Scintilla::SETDOCPOINTER, 0, nFirstDocument);
    SendMessageW(g_sNpp.hMainScintilla, Scintilla::RELEASEDOCUMENT, 0, nFirstDocument);
    SendMessageW(g_sNpp.hMainScintilla, Scintilla::RELEASEDOCUMENT, 0, nThirdDocument);
    FindShortcut();
    bOk = g_nFindCommands == 1 && bOk;
    for (HWND hView : {g_sNpp.hMainScintilla, g_sNpp.hSecondScintilla})
    {
        SendMessageW(hView, Scintilla::SETSEL, 0, 0);
        SendMessageW(hView, WM_CHAR, 'Q', 0);
        char aText[32]{};
        SendMessageW(hView, Scintilla::GETTEXT, sizeof(aText), reinterpret_cast<LPARAM>(aText));
        bOk = std::string(aText) == "Qabcdef" && bOk;
        SendMessageW(hView, Scintilla::SETSEL, 0, 0);
    }
    auto aUnexpectedChildren = Children();
    bOk                      = aUnexpectedChildren.empty() && bOk; // Ordinary input must not recreate sessions.
    for (HANDLE hProcess : aUnexpectedChildren)
        CloseHandle(hProcess);
    pFunctions[0].pFunction();
    Pump(100);
    aChildren = Children();
    bOk       = g_bChecked && aChildren.size() == 2 && bOk;
    for (char nChar : std::string("iZ\x1b"))
        SendMessageW(g_sNpp.hMainScintilla, WM_CHAR, nChar, 0);
    Pump(100);
    char aText[32]{};
    SendMessageW(g_sNpp.hMainScintilla, Scintilla::GETTEXT, sizeof(aText), reinterpret_cast<LPARAM>(aText));
    bOk = std::string(aText) == "ZQabcdef" && bOk;
    // Detect a second failure from inside the keyboard hook itself, before a
    // response timer runs. The triggering shortcut must still reach the host.
    for (HANDLE hProcess : aChildren)
    {
        bOk = TerminateProcess(hProcess, 1) && bOk;
        bOk = WaitForSingleObject(hProcess, 1000) == WAIT_OBJECT_0 && bOk;
    }
    FindShortcut();
    bOk = !g_bChecked && g_nFindCommands == 2 && Stopped(aChildren) && bOk;
    UnhookWindowsHookEx(hDialogHook);
    DestroyAcceleratorTable(g_hAccelerators);
    DestroyWindow(g_sNpp.hNotepad);
    FreeLibrary(hPlugin);
    FreeLibrary(hScintilla);
    std::cout << (bOk ? "Failure cleanup integration passed\n" : "Failure cleanup integration FAILED\n");
    return bOk ? 0 : 1;
}
