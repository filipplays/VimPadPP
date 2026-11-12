// Run in an isolated binary directory with config/vimpad.lua containing:
// return {passthrough_keys = {"<C-s>", "<C-Tab>", "<C-S-Tab>"}}
#include "ScintillaMessages.h"
#include "VimMotions.h"
#include <commctrl.h>
#include <iostream>
#include <string>
#include <windows.h>

namespace
{
int         g_nCommand = 0;
int         g_nChars   = 0;
HWND        g_hEditor  = nullptr;
std::string g_sSavedText;

LRESULT CALLBACK HostProc(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam)
{
    if (nMessage == WM_COMMAND)
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
        ++g_nChars;
    return DefSubclassProc(hWindow, nMessage, wParam, lParam);
}
} // namespace

int main()
{
    HMODULE hScintilla = LoadLibraryW(L"Scintilla.dll");
    HMODULE hPlugin    = LoadLibraryW(L"VimMotions.dll");
    if (!hScintilla || !hPlugin)
        return 2;
    const auto pInstall   = reinterpret_cast<InstallVimFn>(GetProcAddress(hPlugin, "InstallVim"));
    const auto pUninstall = reinterpret_cast<UninstallVimFn>(GetProcAddress(hPlugin, "UninstallVim"));
    WNDCLASSW  sClass{};
    sClass.lpfnWndProc   = HostProc;
    sClass.hInstance     = GetModuleHandleW(nullptr);
    sClass.lpszClassName = L"PassthroughTest";
    RegisterClassW(&sClass);
    HWND hHost = CreateWindowW(
        sClass.lpszClassName, L"Passthrough test", WS_OVERLAPPEDWINDOW, 0, 0, 400, 300, nullptr, nullptr,
        sClass.hInstance, nullptr);
    g_hEditor = CreateWindowW(
        L"Scintilla", L"", WS_CHILD | WS_VISIBLE, 0, 0, 400, 300, hHost, nullptr, sClass.hInstance, nullptr);
    if (!hHost || !g_hEditor || !pInstall || !pUninstall)
        return 2;
    SendMessageW(g_hEditor, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>("abc"));
    SetWindowSubclass(g_hEditor, ObserveChars, 1, 0);
    if (!pInstall(g_hEditor))
        return 2;
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
    auto Shortcut = [&](UINT nKey, bool bShift)
    {
        BYTE aState[256]{};
        aState[VK_CONTROL] = 0x80;
        aState[VK_SHIFT]   = bShift ? 0x80 : 0;
        SetKeyboardState(aState);
        g_nCommand = 0;
        PostMessageW(g_hEditor, WM_KEYDOWN, nKey, 1);
        const ULONGLONG nDeadline = GetTickCount64() + 300;
        while (GetTickCount64() < nDeadline)
        {
            MSG sMessage;
            while (PeekMessageW(&sMessage, nullptr, 0, 0, PM_REMOVE))
                if (!TranslateAcceleratorW(hHost, hAccelerators, &sMessage))
                {
                    TranslateMessage(&sMessage);
                    DispatchMessageW(&sMessage);
                }
            Sleep(1);
        }
        SetKeyboardState(aOriginal);
        return g_nCommand;
    };
    // Save must include edits whose asynchronous state replies have not arrived.
    SendMessageW(g_hEditor, WM_CHAR, 'i', 0);
    SendMessageW(g_hEditor, WM_CHAR, 'X', 0);
    bool bOk = Shortcut('S', false) == 100 && g_sSavedText == "Xabc";
    bOk      = (Shortcut(VK_TAB, false) == 101) && bOk;
    bOk      = (Shortcut(VK_TAB, true) == 102) && bOk;
    // Exact modifier matching: these shortcuts still belong to Neovim.
    bOk      = (Shortcut('S', true) == 0) && bOk;
    SendMessageW(g_hEditor, WM_CHAR, 27, 0);
    bOk = (Shortcut('V', false) == 0) && bOk;
    SendMessageW(g_hEditor, WM_CHAR, 27, 0);
    // Direct dispatch bypasses accelerators; the translated character must reach
    // Scintilla even if Ctrl is released between keydown and WM_CHAR.
    BYTE aState[256]{};
    aState[VK_CONTROL] = 0x80;
    SetKeyboardState(aState);
    SendMessageW(g_hEditor, WM_KEYDOWN, 'S', 0x001f0001);
    SetKeyboardState(aOriginal);
    g_nChars = 0;
    SendMessageW(g_hEditor, WM_CHAR, 19, 0x001f0001);
    bOk = (g_nChars == 1) && bOk;
    // A subsequent ordinary key must resume Neovim input.
    SendMessageW(g_hEditor, WM_KEYDOWN, 'I', 0x00170001);
    g_nChars = 0;
    SendMessageW(g_hEditor, WM_CHAR, 'i', 0x00170001);
    bOk = (g_nChars == 0) && bOk;
    bOk = pUninstall(g_hEditor) && bOk;
    DestroyAcceleratorTable(hAccelerators);
    DestroyWindow(hHost);
    FreeLibrary(hPlugin);
    FreeLibrary(hScintilla);
    std::cout << (bOk ? "Passthrough integration passed\n" : "Passthrough integration FAILED\n");
    return bOk ? 0 : 1;
}
