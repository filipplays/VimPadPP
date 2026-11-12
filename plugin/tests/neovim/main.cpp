#include "ScintillaMessages.h"
#include "VimMotions.h"
#include <cstdint>
#include <iostream>
#include <string>
#include <windows.h>

int main()
{
    HMODULE hScintilla = LoadLibraryW(L"Scintilla.dll");
    HMODULE hPlugin    = LoadLibraryW(L"VimMotions.dll");
    if (!hScintilla || !hPlugin)
        return 2;

    const auto pInstall   = reinterpret_cast<InstallVimFn>(GetProcAddress(hPlugin, "InstallVim"));
    const auto pUninstall = reinterpret_cast<UninstallVimFn>(GetProcAddress(hPlugin, "UninstallVim"));

    HWND hEditor = CreateWindowExW(
        0, L"Scintilla", L"", WS_POPUP, 0, 0, 400, 300, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!hEditor || !pInstall || !pUninstall)
    {
        std::cerr << "Failed to get obtain the library functions!!!" << std::endl;
        return 2;
    }
    else
    {
        std::cout << "Obtained library functions!" << std::endl;
    }

    SendMessageW(hEditor, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>("one two\r\nthree"));
    SendMessageW(hEditor, Scintilla::GOTOPOS, 1, 0);

    if (!pInstall(hEditor))
    {
        std::cerr << "Install failed: " << GetLastError() << '\n';
        return 1;
    }
    else
    {
        std::cout << "Install successfull!!!" << std::endl;
    }

    auto SendKey = [&](WPARAM nKey)
    {
        SendMessageW(hEditor, WM_CHAR, nKey, 0);
        const ULONGLONG nDeadline = GetTickCount64() + 300;
        while (GetTickCount64() < nDeadline)
        {
            MSG sMessage;
            while (PeekMessageW(&sMessage, nullptr, 0, 0, PM_REMOVE))
                DispatchMessageW(&sMessage);
            Sleep(1);
        }
    };
    auto Check = [&](const char *pLabel, LRESULT nStart, LRESULT nEnd, LRESULT nStyle)
    {
        constexpr UINT GetSelectionStart = 2143;
        constexpr UINT GetSelectionEnd   = 2145;
        const LRESULT  nActualStart      = SendMessageW(hEditor, GetSelectionStart, 0, 0);
        const LRESULT  nActualEnd        = SendMessageW(hEditor, GetSelectionEnd, 0, 0);
        const LRESULT  nActualStyle      = SendMessageW(hEditor, Scintilla::GETCARETSTYLE, 0, 0);
        if (nActualStart != nStart || nActualEnd != nEnd || nActualStyle != nStyle)
        {
            std::cerr << pLabel << ": selection " << nActualStart << "," << nActualEnd << " style " << nActualStyle
                      << "\n";
            return false;
        }
        return true;
    };

    bool bOk = Check("normal", 1, 1, Scintilla::CARETSTYLE_BLOCK);
    SendKey('i');
    bOk = Check("insert", 1, 1, Scintilla::CARETSTYLE_LINE) && bOk;
    SendKey(27);
    SendKey('v');
    bOk = Check("visual character", 0, 1, Scintilla::CARETSTYLE_BLOCK) && bOk;
    SendKey('l');
    bOk = Check("visual character extended", 0, 2, Scintilla::CARETSTYLE_BLOCK) && bOk;
    SendKey(27);
    SendKey('V');
    bOk = Check("visual line", 0, 9, Scintilla::CARETSTYLE_BLOCK) && bOk;
    SendKey('j');
    bOk = Check("visual lines extended", 0, 14, Scintilla::CARETSTYLE_BLOCK) && bOk;
    SendKey(27);
    bOk = Check("normal after visual", 10, 10, Scintilla::CARETSTYLE_BLOCK) && bOk;

    SendKey(22);
    SendKey('k');
    constexpr UINT GetSelections = 2570;
    const LRESULT  nSelections   = SendMessageW(hEditor, GetSelections, 0, 0);
    if (nSelections != 2)
    {
        std::cerr << "visual block: expected two selections, got " << nSelections << "\n";
        bOk = false;
    }
    SendKey(27);
    bOk = Check("normal after block", 1, 1, Scintilla::CARETSTYLE_BLOCK) && bOk;

    // Real keyboard input delivers a keydown as well as the translated character.
    SendMessageW(hEditor, WM_KEYDOWN, 'I', 0);
    SendKey('i');
    SendMessageW(hEditor, WM_KEYDOWN, VK_RETURN, 0);
    SendKey(13);
    char aText[128]{};
    SendMessageW(hEditor, Scintilla::GETTEXT, sizeof(aText), reinterpret_cast<LPARAM>(aText));
    if (std::string(aText) != "o\r\nne two\r\nthree")
    {
        std::cerr << "Enter produced unexpected text: " << aText << "\n";
        bOk = false;
    }
    SendMessageW(hEditor, WM_KEYDOWN, VK_BACK, 0);
    SendKey(8);
    SendMessageW(hEditor, Scintilla::GETTEXT, sizeof(aText), reinterpret_cast<LPARAM>(aText));
    if (std::string(aText) != "one two\r\nthree")
    {
        std::cerr << "Backspace produced unexpected text: " << aText << "\n";
        bOk = false;
    }
    BYTE aKeyboardState[256]{};
    GetKeyboardState(aKeyboardState);
    aKeyboardState[VK_CONTROL] |= 0x80;
    SetKeyboardState(aKeyboardState);
    SendMessageW(hEditor, WM_KEYDOWN, 'W', 0);
    aKeyboardState[VK_CONTROL] &= 0x7f;
    SetKeyboardState(aKeyboardState);
    SendKey(23);
    SendMessageW(hEditor, Scintilla::GETTEXT, sizeof(aText), reinterpret_cast<LPARAM>(aText));
    if (std::string(aText) != "ne two\r\nthree")
    {
        std::cerr << "Ctrl-W produced unexpected text: " << aText << "\n";
        bOk = false;
    }
    SendKey(27);

    // A host accelerator must not claim Ctrl-V before the editor sees it.
    ShowWindow(hEditor, SW_SHOW);
    SetForegroundWindow(hEditor);
    SetFocus(hEditor);
    ACCEL  sAccelerator{FVIRTKEY | FCONTROL, 'V', 100};
    HACCEL hAccelerator = CreateAcceleratorTableW(&sAccelerator, 1);
    GetKeyboardState(aKeyboardState);
    aKeyboardState[VK_CONTROL] |= 0x80;
    SetKeyboardState(aKeyboardState);
    PostMessageW(hEditor, WM_KEYDOWN, 'V', 0);
    bool            bHostHandled = false;
    const ULONGLONG nDeadline    = GetTickCount64() + 300;
    while (GetTickCount64() < nDeadline)
    {
        MSG sMessage;
        while (PeekMessageW(&sMessage, nullptr, 0, 0, PM_REMOVE))
        {
            if (TranslateAcceleratorW(hEditor, hAccelerator, &sMessage))
                bHostHandled = true;
            else
            {
                TranslateMessage(&sMessage);
                DispatchMessageW(&sMessage);
            }
        }
        Sleep(1);
    }
    aKeyboardState[VK_CONTROL] &= 0x7f;
    SetKeyboardState(aKeyboardState);
    DestroyAcceleratorTable(hAccelerator);
    SendKey('l');
    bOk = Check("Ctrl-V bypasses host accelerator", 0, 2, Scintilla::CARETSTYLE_BLOCK) && bOk;
    if (bHostHandled)
    {
        std::cerr << "Host accelerator handled Ctrl-V\n";
        bOk = false;
    }
    SendKey(27);
    // The command bar must not cover a search result, even on the last row.
    ShowWindow(hEditor, SW_SHOWNOACTIVATE);
    std::string sSearchText;
    for (int nLine = 0; nLine < 200; ++nLine)
        sSearchText += "ordinary line\r\n";
    sSearchText += std::string(300, 'x') + " needle";
    SendMessageW(hEditor, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>(sSearchText.c_str()));
    constexpr UINT SetWrapMode = 2268;
    SendMessageW(hEditor, SetWrapMode, 1, 0);
    SendMessageW(hEditor, Scintilla::GOTOPOS, 0, 0);
    const LRESULT nOriginalEndAtLastLine = SendMessageW(hEditor, Scintilla::GETENDATLASTLINE, 0, 0);
    SendKey('/');
    for (char cKey : std::string("needle"))
        SendKey(cKey);
    SendKey('\r');
    auto CheckCaretVisible = [&](const char *pLabel)
    {
        const LRESULT nCaret  = SendMessageW(hEditor, Scintilla::GETCURRENTPOS, 0, 0);
        const LRESULT nLine   = SendMessageW(hEditor, Scintilla::LINEFROMPOSITION, nCaret, 0);
        const LRESULT nY      = SendMessageW(hEditor, Scintilla::POINTYFROMPOSITION, 0, nCaret);
        const LRESULT nHeight = SendMessageW(hEditor, Scintilla::TEXTHEIGHT, nLine, 0);
        RECT          sUsable{};
        GetClientRect(hEditor, &sUsable);
        HWND hBar = FindWindowExW(hEditor, nullptr, L"STATIC", nullptr);
        if (hBar && IsWindowVisible(hBar))
        {
            RECT sBar{};
            GetWindowRect(hBar, &sBar);
            MapWindowPoints(HWND_DESKTOP, hEditor, reinterpret_cast<POINT *>(&sBar), 2);
            sUsable.bottom = sBar.top;
        }
        if (nCaret != static_cast<LRESULT>(sSearchText.size() - 6) || nY < 0 || nY + nHeight > sUsable.bottom)
        {
            std::cerr << pLabel << ": caret=" << nCaret << " y=" << nY << " height=" << nHeight
                      << " usable bottom=" << sUsable.bottom << '\n';
            return false;
        }
        return true;
    };
    bOk = CheckCaretVisible("wrapped search at document end") && bOk;
    SendMessageW(hEditor, Scintilla::SETZOOM, 6, 0);
    SetWindowPos(hEditor, nullptr, 0, 0, 320, 180, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    // Pump the posted layout check without issuing another editing command.
    MSG sLayoutMessage{};
    while (PeekMessageW(&sLayoutMessage, nullptr, 0, 0, PM_REMOVE))
        DispatchMessageW(&sLayoutMessage);
    bOk = CheckCaretVisible("zoom and resize") && bOk;
    bOk = pUninstall(hEditor) && bOk;
    if (SendMessageW(hEditor, Scintilla::GETENDATLASTLINE, 0, 0) != nOriginalEndAtLastLine)
    {
        std::cerr << "Uninstall did not restore scrolling policy\n";
        bOk = false;
    }
    DestroyWindow(hEditor);
    FreeLibrary(hPlugin);
    FreeLibrary(hScintilla);

    if (bOk)
        std::cout << "Neovim integration passed\n";
    return bOk ? 0 : 1;
}
