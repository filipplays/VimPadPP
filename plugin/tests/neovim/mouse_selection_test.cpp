// Run beside the Neovim VimMotions.dll, Scintilla.dll and staged neovim/.
#include "ScintillaMessages.h"
#include "VimMotions.h"
#include <iostream>
#include <string>
#include <windows.h>

namespace
{
void Pump(unsigned nMilliseconds)
{
    const ULONGLONG nDeadline = GetTickCount64() + nMilliseconds;
    do
    {
        MSG sMessage;
        while (PeekMessageW(&sMessage, nullptr, 0, 0, PM_REMOVE))
        {
            // Mouse input is supplied explicitly; ignore incidental desktop motion.
            if (sMessage.message >= WM_MOUSEFIRST && sMessage.message <= WM_MOUSELAST)
                continue;
            TranslateMessage(&sMessage);
            DispatchMessageW(&sMessage);
        }
        Sleep(1);
    } while (GetTickCount64() < nDeadline);
}
LPARAM Point(HWND hEditor, LRESULT nPosition)
{
    const LRESULT nX = SendMessageW(hEditor, 2164 /* SCI_POINTXFROMPOSITION */, 0, nPosition);
    const LRESULT nY = SendMessageW(hEditor, Scintilla::POINTYFROMPOSITION, 0, nPosition);
    return MAKELPARAM(nX, nY + 3);
}
bool Selection(HWND hEditor, LRESULT nAnchor, LRESULT nCaret)
{
    const LRESULT nActualAnchor = SendMessageW(hEditor, Scintilla::GETANCHOR, 0, 0);
    const LRESULT nActualCaret  = SendMessageW(hEditor, Scintilla::GETCURRENTPOS, 0, 0);
    if (nActualAnchor == nAnchor && nActualCaret == nCaret)
        return true;
    std::cerr << "selection " << nActualAnchor << ',' << nActualCaret << " expected " << nAnchor << ',' << nCaret
              << '\n';
    return false;
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
    HWND       hHost      = CreateWindowW(
        L"STATIC", L"Mouse selection test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 640, 400, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    HWND hEditor = CreateWindowW(
        L"Scintilla", L"", WS_CHILD | WS_VISIBLE, 0, 0, 640, 360, hHost, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hEditor || !pInstall || !pUninstall)
        return 2;
    SetFocus(hEditor);
    bool bOk = true;
    for (const char *pPending : {"f", "d", "df", "di", "3", "g", "r", ":"})
    {
        for (const unsigned nWait : {0u, 30u})
        {
            for (const bool bDrag : {false, true})
            {
                SendMessageW(hEditor, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>("abcdef"));
                SendMessageW(hEditor, Scintilla::SETSEL, 0, 0);
                if (!pInstall(hEditor))
                    return 2;
                Pump(100);
                for (const char nChar : std::string(pPending))
                    SendMessageW(hEditor, WM_CHAR, nChar, 0);
                if (nWait)
                    Pump(nWait);
                const ULONGLONG nStart = GetTickCount64();
                SendMessageW(hEditor, WM_LBUTTONDOWN, MK_LBUTTON, Point(hEditor, 1));
                SendMessageW(hEditor, WM_MOUSEMOVE, MK_LBUTTON, Point(hEditor, 4));
                SendMessageW(hEditor, WM_LBUTTONUP, 0, Point(hEditor, 4));
                Pump(50);
                if (GetTickCount64() - nStart >= 1000)
                {
                    std::cerr << "Cancellation took too long for " << pPending << '\n';
                    bOk = false;
                }
                bOk                   = Selection(hEditor, 0, 0) && bOk;
                // The next gesture must work, and a subsequent Vim edit proves
                // the session survived and the pending command was cancelled.
                const LRESULT nAnchor = bDrag ? 1 : 3;
                const LRESULT nCaret  = bDrag ? 4 : 3;
                SendMessageW(hEditor, WM_LBUTTONDOWN, MK_LBUTTON, Point(hEditor, nAnchor));
                if (bDrag)
                    SendMessageW(hEditor, WM_MOUSEMOVE, MK_LBUTTON, Point(hEditor, nCaret));
                SendMessageW(hEditor, WM_LBUTTONUP, 0, Point(hEditor, nCaret));
                SendMessageW(hEditor, WM_CHAR, bDrag ? 'd' : 'x', 0);
                Pump(100);
                char aText[256]{};
                SendMessageW(hEditor, Scintilla::GETTEXT, sizeof(aText), reinterpret_cast<LPARAM>(aText));
                const std::string sExpected = bDrag ? "aef" : "abcef";
                if (aText != sExpected)
                {
                    std::cerr << "After cancelling " << pPending << ": [" << aText << "] expected [" << sExpected
                              << "]\n";
                    bOk = false;
                }
                bOk = pUninstall(hEditor) && bOk;
            }
        }
    }
    // A completed command with a reply in flight must still allow the first
    // click. Insert mode also keeps its mode when there is no incomplete input.
    for (const char nInitialKey : {'l', 'x', 'i'})
    {
        SendMessageW(hEditor, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>("abcdef"));
        SendMessageW(hEditor, Scintilla::SETSEL, 0, 0);
        if (!pInstall(hEditor))
            return 2;
        Pump(100);
        SendMessageW(hEditor, WM_CHAR, nInitialKey, 0);
        SendMessageW(hEditor, WM_LBUTTONDOWN, MK_LBUTTON, Point(hEditor, 3));
        SendMessageW(hEditor, WM_LBUTTONUP, 0, Point(hEditor, 3));
        Pump(50);
        bOk = Selection(hEditor, 3, 3) && bOk;
        SendMessageW(hEditor, WM_CHAR, nInitialKey == 'i' ? 'Z' : 'x', 0);
        Pump(100);
        char aText[256]{};
        SendMessageW(hEditor, Scintilla::GETTEXT, sizeof(aText), reinterpret_cast<LPARAM>(aText));
        const std::string sExpected = nInitialKey == 'i' ? "abcZdef" : nInitialKey == 'x' ? "bcdf" : "abcef";
        if (aText != sExpected)
        {
            std::cerr << "Ordinary click after " << nInitialKey << ": [" << aText << "] expected [" << sExpected
                      << "]\n";
            bOk = false;
        }
        bOk = pUninstall(hEditor) && bOk;
    }
    struct Case
    {
        const char *pText;
        LRESULT     nAnchor;
        LRESULT     nCaret;
        bool        bImmediate;
        bool        bExclusive   = false;
        bool        bCaptureLost = false;
    };
    const Case aCases[] = {
        {"abcdef", 1, 4, false},
        {"abcdef", 4, 1, false},
        {"abcdef", 1, 4, true},
        {"abcdef", 4, 1, true},
        {"abc\ndef", 1, 5, false},
        {"abc\r\ndef", 6, 1, false},
        {"abc\ndef", 0, 4, false},
        {"abc\r\ndef", 5, 0, false},
        {"a\xc3\xa9\xe4\xb8\xadZ", 1, 6, false},
        {"a\xc3\xa9\xe4\xb8\xadZ", 6, 1, false},
        {"abcdef", 0, 6, false},
        {"abcdef", 2, 3, false},
        {"abcdef", 1, 4, false, true},
        {"abcdef", 4, 1, false, true},
        {"abcdef", 2, 3, false, true},
        {"abcdef", 1, 4, true, false, true}};
    for (const Case &sCase : aCases)
    {
        SendMessageW(hEditor, 2037 /* SCI_SETCODEPAGE */, 65001, 0);
        SendMessageW(hEditor, Scintilla::SETTEXT, 0, reinterpret_cast<LPARAM>(sCase.pText));
        SendMessageW(hEditor, Scintilla::SETSEL, 0, 0);
        if (!pInstall(hEditor))
            return 2;
        Pump(100);
        if (sCase.bExclusive)
        {
            for (const char nChar : std::string(":set selection=exclusive\r"))
                SendMessageW(hEditor, WM_CHAR, nChar, 0);
            Pump(100);
        }
        // A pending state reply must not erase the new mouse selection.
        SendMessageW(hEditor, WM_CHAR, 'l', 0);
        SendMessageW(hEditor, WM_LBUTTONDOWN, MK_LBUTTON, Point(hEditor, sCase.nAnchor));
        Pump(30);
        SendMessageW(hEditor, WM_MOUSEMOVE, MK_LBUTTON, Point(hEditor, sCase.nCaret));
        Pump(100);
        bOk = Selection(hEditor, sCase.nAnchor, sCase.nCaret) && bOk;
        if (sCase.bCaptureLost)
            ReleaseCapture();
        else
            SendMessageW(hEditor, WM_LBUTTONUP, 0, Point(hEditor, sCase.nCaret));
        if (!sCase.bImmediate)
        {
            Pump(200);
            bOk = Selection(hEditor, sCase.nAnchor, sCase.nCaret) && bOk;
        }
        SendMessageW(hEditor, WM_CHAR, 'd', 0);
        Pump(250);
        std::string  sExpected = sCase.pText;
        const size_t nFirst    = static_cast<size_t>(std::min(sCase.nAnchor, sCase.nCaret));
        sExpected.erase(nFirst, static_cast<size_t>(std::max(sCase.nAnchor, sCase.nCaret)) - nFirst);
        char aText[256]{};
        SendMessageW(hEditor, Scintilla::GETTEXT, sizeof(aText), reinterpret_cast<LPARAM>(aText));
        if (sExpected != aText)
        {
            std::cerr << "delete " << sCase.nAnchor << ',' << sCase.nCaret << ": [" << aText << "] expected ["
                      << sExpected << "]\n";
            bOk = false;
        }
        bOk = pUninstall(hEditor) && bOk;
    }
    DestroyWindow(hHost);
    FreeLibrary(hPlugin);
    FreeLibrary(hScintilla);
    std::cout << (bOk ? "Mouse selection integration passed\n" : "Mouse selection integration FAILED\n");
    return bOk ? 0 : 1;
}
