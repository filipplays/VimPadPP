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

    auto Check = [&](char nKey, const char *pText, int nPosition)
    {
        static uint32_t count = 0;
        if ((nKey >= 'A' && nKey <= 'Z') || (nKey >= 'a' && nKey <= 'z'))
        {
            SendMessageW(hEditor, WM_CHAR, nKey, 0);
        }
        else
        {
            SendMessageW(hEditor, WM_KEYDOWN, 27, 0);
        }
        char aText[128]{};
        SendMessageW(hEditor, Scintilla::GETTEXT, sizeof(aText), reinterpret_cast<LPARAM>(aText));

        int nActual = static_cast<int>(SendMessageW(hEditor, Scintilla::GETCURRENTPOS, 0, 0));

        if (std::string(aText) != pText || nActual != nPosition)
        {
            std::cerr << "Tried key: " << nKey << " got text:" << std::endl;
            std::cerr << aText << std::endl;
            std::cerr << "Curr poss: " << nActual << std::endl;
            std::cerr << "Curr count: " << count << std::endl;
            return false;
        }
        count++;
        return true;
    };

    bool bOk = Check('h', "one two\r\nthree", 0) && Check('l', "one two\r\nthree", 1) &&
               Check('w', "one two\r\nthree", 4) && Check('x', "one wo\r\nthree", 4) &&
               Check('w', "one wo\r\nthree", 8) && Check('x', "one wo\r\nhree", 8) && Check('i', "one wo\r\nhree", 8) &&
               Check('A', "one wo\r\nAhree", 9) && Check(27, "one wo\r\nAhree", 8) && Check('x', "one wo\r\nhree", 8);

    bOk = pUninstall(hEditor) && bOk;
    DestroyWindow(hEditor);
    FreeLibrary(hPlugin);
    FreeLibrary(hScintilla);

    if (bOk)
        std::cout << "Neovim integration passed\n";
    return bOk ? 0 : 1;
}
