// Run in an isolated binary directory with config/vimpad.lua containing:
// return {passthrough_keys = {"<C-s>", "<C-Tab>", "<C-S-Tab>"}}
#include "ScintillaMessages.h"
#include "VimMotions.h"
#if defined(TEST_DOCUMENT_MANAGER)
#include "backend_manager.h"
#endif
#include <commctrl.h>
#include <iostream>
#include <string>
#include <windows.h>

namespace
{
int         g_nCommand     = 0;
int         g_nChars       = 0;
bool        g_bConsumeChar = false;
HWND        g_hEditor      = nullptr;
std::string g_sSavedText;

LRESULT CALLBACK HostProc(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam)
{
    if (nMessage == WM_COMMAND && lParam == 0)
    {
        g_nCommand = LOWORD(wParam);
        char aText[128]{};
        SendMessageW(g_hEditor, Scintilla::GETTEXT, sizeof(aText), reinterpret_cast<LPARAM>(aText));
        g_sSavedText = aText;
        return 0;
    }
    return DefWindowProcW(hWindow, nMessage, wParam, lParam);
}

LRESULT CALLBACK ObserveChars(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR)
{
    if (nMessage == WM_CHAR)
    {
        ++g_nChars;
        if (g_bConsumeChar)
            return 0;
    }
    return DefSubclassProc(hWindow, nMessage, wParam, lParam);
}
} // namespace

int main()
{
    HMODULE hScintilla = LoadLibraryW(L"Scintilla.dll");
    HMODULE hPlugin    = LoadLibraryW(L"VimMotions.dll");
    if (!hScintilla || !hPlugin)
        return 2;
#if defined(TEST_DOCUMENT_MANAGER)
    Manager xManager;
    auto    pInstall   = [&](HWND hWindow) { return xManager.InstallVim(hWindow); };
    auto    pUninstall = [&](HWND hWindow) { return xManager.UninstallVim(hWindow); };
#else
    const auto pInstall   = reinterpret_cast<InstallVimFn>(GetProcAddress(hPlugin, "InstallVim"));
    const auto pUninstall = reinterpret_cast<UninstallVimFn>(GetProcAddress(hPlugin, "UninstallVim"));
#endif
    WNDCLASSW sClass{};
    sClass.lpfnWndProc   = HostProc;
    sClass.hInstance     = GetModuleHandleW(nullptr);
    sClass.lpszClassName = L"PassthroughTest";
    RegisterClassW(&sClass);
    HWND hHost = CreateWindowW(
        sClass.lpszClassName, L"Passthrough test", WS_OVERLAPPEDWINDOW, 0, 0, 400, 300, nullptr, nullptr,
        sClass.hInstance, nullptr);
    g_hEditor = CreateWindowW(
        L"Scintilla", L"", WS_CHILD | WS_VISIBLE, 0, 0, 400, 300, hHost, nullptr, sClass.hInstance, nullptr);
    if (!hHost || !g_hEditor)
        return 2;
    SendMessageW(g_hEditor, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>("abc"));
    SetWindowSubclass(g_hEditor, ObserveChars, 1, 0);
    if (!pInstall(g_hEditor))
        return 2;
#if defined(TEST_DOCUMENT_MANAGER)
    xManager.ActivateDocument(g_hEditor, 1);
#endif
    ShowWindow(hHost, SW_SHOW);
    SetForegroundWindow(hHost);
    SetFocus(g_hEditor);
    ACCEL aAccelerators[] = {
        {FVIRTKEY | FCONTROL, 'S', 100},
        {FVIRTKEY | FCONTROL, VK_TAB, 101},
        {FVIRTKEY | FCONTROL | FSHIFT, VK_TAB, 102},
        {FVIRTKEY | FCONTROL, 'V', 103},
        {FVIRTKEY | FCONTROL | FSHIFT, 'S', 104},
    };
    HACCEL hAccelerators = CreateAcceleratorTableW(aAccelerators, 5);
    BYTE   aOriginal[256]{};
    GetKeyboardState(aOriginal);
    auto Shortcut = [&](UINT nKey, bool bShift, DWORD nWait = 300)
    {
        BYTE aState[256]{};
        aState[VK_CONTROL] = 0x80;
        aState[VK_SHIFT]   = bShift ? 0x80 : 0;
        SetKeyboardState(aState);
        g_nCommand = 0;
        PostMessageW(g_hEditor, WM_KEYDOWN, nKey, 1);
        const ULONGLONG nDeadline = GetTickCount64() + nWait;
        do
        {
            MSG sMessage;
            while (PeekMessageW(&sMessage, nullptr, 0, 0, PM_REMOVE))
            {
                if (sMessage.message == WM_KEYDOWN)
                    SetKeyboardState(aState);
                if (!TranslateAcceleratorW(hHost, hAccelerators, &sMessage))
                {
                    TranslateMessage(&sMessage);
                    DispatchMessageW(&sMessage);
                }
            }
            Sleep(1);
        } while (GetTickCount64() < nDeadline);
        SetKeyboardState(aOriginal);
        return g_nCommand;
    };
    // Save must reach the host even while Neovim has outstanding state replies.
    SendMessageW(g_hEditor, WM_CHAR, 'i', 0);
    SendMessageW(g_hEditor, WM_CHAR, 'X', 0);
    bool bOk = Shortcut('S', false) == 100;
    // Repeated saves must not wait for outstanding replies.
    for (int nIndex = 0; nIndex < 50; ++nIndex)
    {
        SendMessageW(g_hEditor, WM_CHAR, 'x', 0);
        if (Shortcut('S', false, 5) != 100)
        {
            std::cerr << "Rapid save failed at iteration " << nIndex << " command=" << g_nCommand << '\n';
            bOk = false;
            break;
        }
    }
    if (!bOk)
        std::cerr << "Rapid shortcuts failed\n";
    // A pending find must survive switching documents and saving visible text.
    SendMessageW(g_hEditor, WM_CHAR, 27, 0);
    Shortcut('S', false);
    const LRESULT nFirstDocument = SendMessageW(g_hEditor, Scintilla::GETDOCPOINTER, 0, 0);
    SendMessageW(g_hEditor, WM_CHAR, '0', 0);
    Shortcut('S', false);
    SendMessageW(g_hEditor, WM_CHAR, 'f', 0);
    char aVisibleText[128]{};
    SendMessageW(g_hEditor, Scintilla::GETTEXT, sizeof(aVisibleText), reinterpret_cast<LPARAM>(aVisibleText));
    const ULONGLONG nSaveStart = GetTickCount64();
    bOk                        = (Shortcut('S', false, 5) == 100) && bOk;
    bOk                        = (g_sSavedText == aVisibleText && GetTickCount64() - nSaveStart < 1000) && bOk;
#if defined(TEST_DOCUMENT_MANAGER)
    HWND hClone = CreateWindowW(L"Scintilla", L"", WS_CHILD, 0, 0, 200, 100, hHost, nullptr, sClass.hInstance, nullptr);
    SendMessageW(hClone, Scintilla::SETDOCPOINTER, 0, nFirstDocument);
    bOk = xManager.InstallVim(hClone) && bOk;
    xManager.ActivateDocument(g_hEditor, 1);
    bOk = xManager.UninstallVim(hClone) && bOk;
    DestroyWindow(hClone);
#endif
    const LRESULT nSecondDocument = SendMessageW(g_hEditor, Scintilla::CREATEDOCUMENT, 0, 0);
    SendMessageW(g_hEditor, Scintilla::SETDOCPOINTER, 0, nSecondDocument);
#if defined(TEST_DOCUMENT_MANAGER)
    xManager.ActivateDocument(g_hEditor, 2);
#endif
    SendMessageW(g_hEditor, WM_CHAR, 'i', 0);
    SendMessageW(g_hEditor, WM_CHAR, 'Y', 0);
    SendMessageW(g_hEditor, WM_CHAR, 27, 0);
    Shortcut('S', false);
    char aSecondText[16]{};
    SendMessageW(g_hEditor, Scintilla::GETTEXT, sizeof(aSecondText), reinterpret_cast<LPARAM>(aSecondText));
    if (std::string(aSecondText) != "Y")
        std::cerr << "Second document text: " << aSecondText << "\n";
    bOk = (std::string(aSecondText) == "Y") && bOk;
    SendMessageW(g_hEditor, WM_CHAR, 'f', 0); // Close/suspend a second pending command independently.
    SendMessageW(g_hEditor, Scintilla::SETDOCPOINTER, 0, nFirstDocument);
#if defined(TEST_DOCUMENT_MANAGER)
    xManager.ActivateDocument(g_hEditor, 1);
    xManager.CloseDocument(2);
    xManager.CloseDocument(2); // Duplicate notification must be harmless.
#endif
    SendMessageW(g_hEditor, WM_CHAR, 'c', 0);
    Shortcut('S', false);
    const LRESULT nFound = SendMessageW(g_hEditor, Scintilla::GETCURRENTPOS, 0, 0);
    if (nFound != 53)
        std::cerr << "Find cursor: " << nFound << "\n";
    bOk = (nFound == 53) && bOk;
    SendMessageW(g_hEditor, Scintilla::RELEASEDOCUMENT, 0, nSecondDocument);
    bOk = (Shortcut(VK_TAB, false) == 101) && bOk;
    bOk = (Shortcut(VK_TAB, true) == 102) && bOk;
    if (!bOk)
        std::cerr << "Document or tab checks failed\n";
    // Exact modifier matching: these shortcuts still belong to Neovim.
    bOk = (Shortcut('S', true) == 0) && bOk;
    SendMessageW(g_hEditor, WM_CHAR, 27, 0);
    bOk = (Shortcut('V', false) == 0) && bOk;
    SendMessageW(g_hEditor, WM_CHAR, 27, 0);
    if (!bOk)
        std::cerr << "Modifier checks failed\n";
    // Direct dispatch bypasses accelerators; the translated character must reach
    // Scintilla even if Ctrl is released between keydown and WM_CHAR.
    BYTE aState[256]{};
    aState[VK_CONTROL] = 0x80;
    SetKeyboardState(aState);
    SendMessageW(g_hEditor, WM_KEYDOWN, 'S', 0x001f0001);
    SetKeyboardState(aOriginal);
    g_nChars       = 0;
    // Observe the forwarded character without editing the document.
    g_bConsumeChar = true;
    SendMessageW(g_hEditor, WM_CHAR, 19, 0x001f0001);
    g_bConsumeChar = false;
    if (g_nChars != 1)
        std::cerr << "Pass chars: " << g_nChars << "\n";
    bOk = (g_nChars == 1) && bOk;
    // A subsequent ordinary key must resume Neovim input.
    SendMessageW(g_hEditor, WM_KEYDOWN, 'I', 0x00170001);
    g_nChars = 0;
    SendMessageW(g_hEditor, WM_CHAR, 'i', 0x00170001);
    if (g_nChars != 0)
        std::cerr << "Vim chars: " << g_nChars << "\n";
    bOk                     = (g_nChars == 0) && bOk;
    const BOOL bUninstalled = pUninstall(g_hEditor);
    if (!bUninstalled)
        std::cerr << "Uninstall failed\n";
    bOk = bUninstalled && bOk;
    DestroyAcceleratorTable(hAccelerators);
    DestroyWindow(hHost);
    FreeLibrary(hPlugin);
    FreeLibrary(hScintilla);
    std::cout << (bOk ? "Passthrough integration passed\n" : "Passthrough integration FAILED\n");
    return bOk ? 0 : 1;
}
