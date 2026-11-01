#include <windows.h>

#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "ScintillaMessages.h"
#include "VimMotions.h"
#include "json_test_cases.h"

namespace
{

// Scintilla's API is message based: SendMessage(editor, SCI_..., wParam,
// lParam). These numeric values are generated from Scintilla.iface. Once this
// becomes a larger project, prefer vendoring Scintilla.h instead of listing the
// individual constants we use.
constexpr UINT SCI_SETTEXT         = 2181;
constexpr UINT SCI_STYLESETFONT    = 2056;
constexpr UINT SCI_STYLESETSIZE    = 2055;
constexpr UINT SCI_STYLECLEARALL   = 2050;
constexpr UINT SCI_SETMARGINWIDTHN = 2242;

// STYLE_DEFAULT is the base style inherited by all other Scintilla styles.
constexpr int STYLE_DEFAULT = 32;

// This small host has one editor, so process-wide variables keep it simple.
// A real multi-tab editor would store this information in a window object.
HWND           g_scintilla          = nullptr;
// A Windows DLL must remain loaded until all windows/classes/code it provides
// are no longer being used. These handles own the two runtime-loaded modules.
HMODULE        g_scintilla_module   = nullptr;
HMODULE        g_vim_motions_module = nullptr;
// We only need this exported function during shutdown; InstallVim is looked up
// and called immediately in InstallVimMotions.
UninstallVimFn g_uninstall_vim      = nullptr;

bool LoadReplayCase(TestCase &sReplayCase, bool &bReplay)
{
    int     nArgumentCount = 0;
    LPWSTR *aArguments     = CommandLineToArgvW(GetCommandLineW(), &nArgumentCount);
    if (aArguments == nullptr)
    {
        std::cerr << "Could not read command line\n";
        return false;
    }
    bool bSuccess = true;
    try
    {
        if (nArgumentCount != 1)
        {
            if (nArgumentCount != 4 || std::wstring(aArguments[1]) != L"--replay")
            {
                throw std::runtime_error("Usage: ScintillaHost.exe --replay <cases.json> <case-id>");
            }
            std::ifstream sFile(std::filesystem::path(aArguments[2]), std::ios::binary);
            if (!sFile)
            {
                throw std::runtime_error("Could not open replay cases file");
            }
            const std::string sJson((std::istreambuf_iterator<char>(sFile)), std::istreambuf_iterator<char>());
            if (sFile.bad())
            {
                throw std::runtime_error("Could not read replay cases file");
            }
            const int nIdSize = WideCharToMultiByte(CP_UTF8, 0, aArguments[3], -1, nullptr, 0, nullptr, nullptr);
            if (nIdSize == 0)
            {
                throw std::runtime_error("Could not read replay case ID");
            }
            std::string sId(nIdSize, '\0');
            WideCharToMultiByte(CP_UTF8, 0, aArguments[3], -1, sId.data(), nIdSize, nullptr, nullptr);
            sId.pop_back();
            for (const TestCase &sCase : ParseTestCasesJson(sJson))
            {
                if (sCase.sId == sId)
                {
                    if (bReplay)
                    {
                        throw std::runtime_error("Duplicate replay case ID: " + sId);
                    }
                    sReplayCase = sCase;
                    bReplay     = true;
                }
            }
            if (!bReplay)
            {
                throw std::runtime_error("Replay case not found: " + sId);
            }
            if (sReplayCase.nInitialCaret < 0 ||
                static_cast<unsigned long long>(sReplayCase.nInitialCaret) > sReplayCase.sInitialText.size())
            {
                throw std::runtime_error("Replay starting caret is outside the test text");
            }
        }
    }
    catch (const std::exception &sException)
    {
        std::cerr << "Replay setup failed: " << sException.what() << '\n';
        bSuccess = false;
    }
    LocalFree(aArguments);
    return bSuccess;
}

// Called after the outer host window changes size. Scintilla is a child window,
// so it needs an explicit rectangle; Win32 does not resize child controls for
// us automatically.
void ResizeEditor(HWND window)
{
    if (g_scintilla == nullptr)
    {
        return;
    }

    RECT client{};
    GetClientRect(window, &client);
    MoveWindow(g_scintilla, 0, 0, client.right, client.bottom, TRUE);
}

// Win32 delivers all events for the outer application window here: resizing,
// keyboard focus, close requests, painting, and many others. A case that
// returns 0 has handled that event; everything else is delegated to Windows'
// standard implementation (DefWindowProcW).
LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message)
    {
    case WM_SIZE:
        ResizeEditor(window);
        return 0;

    case WM_SETFOCUS:
        // The host itself never accepts text. Forward focus to the child so
        // keyboard input reaches Scintilla (and therefore the Vim subclass).
        if (g_scintilla != nullptr)
        {
            SetFocus(g_scintilla);
        }
        return 0;

    case WM_CLOSE:
        // Remove the subclass while both the target window and its DLL are
        // still alive. VimMotions also handles WM_NCDESTROY as a safety net.
        if (g_uninstall_vim != nullptr && g_scintilla != nullptr)
        {
            g_uninstall_vim(g_scintilla);
        }
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        // End GetMessageW below. WM_DESTROY is sent after the OS has started
        // destroying this window and its child windows.
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(window, message, w_param, l_param);
    }
}

bool CreateScintilla(HWND parent, HINSTANCE instance)
{
    // Loading the DLL registers Scintilla's window class. Keeping this module
    // handle alive is mandatory while the child window exists.
    g_scintilla_module = LoadLibraryW(L"Scintilla.dll");
    if (g_scintilla_module == nullptr)
    {
        MessageBoxW(
            parent, L"Could not load Scintilla.dll from the application directory.", L"Scintilla host",
            MB_ICONERROR | MB_OK);
        return false;
    }

    // "Scintilla" is a Win32 window class registered by Scintilla.dll. This
    // creates one instance of that class as a visible child of our host window.
    g_scintilla = CreateWindowExW(
        0, L"Scintilla", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0, 0, parent, nullptr, instance, nullptr);
    if (g_scintilla == nullptr)
    {
        MessageBoxW(
            parent, L"Scintilla.dll loaded but its window could not be created.", L"Scintilla host",
            MB_ICONERROR | MB_OK);
        FreeLibrary(g_scintilla_module);
        g_scintilla_module = nullptr;
        return false;
    }

    // Configure the control through its documented SCI_* messages. The text
    // parameters are UTF-8 / narrow strings as expected by Scintilla, even
    // though this host otherwise uses the Unicode (W) Win32 API.

    char sInitial_text[] = "This is VIM!\r\n"
                           "This is VIM!\r\n"
                           "This is VIM!";

    SendMessageW(g_scintilla, SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<LPARAM>("Consolas"));
    SendMessageW(g_scintilla, SCI_STYLESETSIZE, STYLE_DEFAULT, 12);
    SendMessageW(g_scintilla, SCI_STYLECLEARALL, 0, 0);
    SendMessageW(g_scintilla, SCI_SETMARGINWIDTHN, 0, 0);
    SendMessageW(g_scintilla, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(sInitial_text));
    SetFocus(g_scintilla);
    return true;
}

bool InstallVimMotions(HWND parent)
{
    // Deliberately use explicit dynamic loading: the host has no link-time
    // dependency on VimMotions.lib. The same pattern will let winIDEA run even
    // when the optional VimMotions DLL is absent.
    g_vim_motions_module = LoadLibraryW(L"VimMotions.dll");
    if (g_vim_motions_module == nullptr)
    {
        MessageBoxW(
            parent, L"Could not load VimMotions.dll from the application directory.", L"Scintilla host",
            MB_ICONERROR | MB_OK);
        return false;
    }

    // The Win32 API exposes dynamically found functions as FARPROC. The ABI is
    // compatible with our exported WINAPI functions; MinGW only lacks a type-
    // safe conversion for this standard Win32 pattern.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    const auto install_vim = reinterpret_cast<InstallVimFn>(GetProcAddress(g_vim_motions_module, "InstallVim"));
    g_uninstall_vim        = reinterpret_cast<UninstallVimFn>(GetProcAddress(g_vim_motions_module, "UninstallVim"));
#pragma GCC diagnostic pop
    if (install_vim == nullptr || g_uninstall_vim == nullptr || !install_vim(g_scintilla))
    {
        MessageBoxW(
            parent, L"VimMotions.dll is missing its API or could not install.", L"Scintilla host",
            MB_ICONERROR | MB_OK);
        FreeLibrary(g_vim_motions_module);
        g_vim_motions_module = nullptr;
        g_uninstall_vim      = nullptr;
        return false;
    }
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    TestCase sReplayCase;
    bool     bReplay = false;
    if (!LoadReplayCase(sReplayCase, bReplay))
    {
        return 2;
    }
    // wWinMain is the Unicode GUI entry point. -municode in the Makefile tells
    // MinGW to use it instead of a console main().
    constexpr wchar_t kClassName[] = L"HackathonScintillaHost";

    // Register a *window class* (a template describing Windows of this type),
    // then create one actual outer application window from that template.
    WNDCLASSEXW window_class{};
    window_class.cbSize        = sizeof(window_class);
    window_class.hInstance     = instance;
    window_class.hCursor       = LoadCursor(nullptr, IDC_IBEAM);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = kClassName;
    window_class.lpfnWndProc   = WindowProc;

    if (RegisterClassExW(&window_class) == 0)
    {
        return 1;
    }

    // This is the outer frame/title-bar window. The editable Scintilla control
    // is created as its child later by CreateScintilla.
    HWND window = CreateWindowExW(
        0, kClassName, L"Scintilla x64 Host", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 640, nullptr,
        nullptr, instance, nullptr);
    if (window == nullptr)
    {
        return 1;
    }

    if (!CreateScintilla(window, instance))
    {
        DestroyWindow(window);
        return 1;
    }

    if (!InstallVimMotions(window))
    {
        DestroyWindow(window);
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);

    if (bReplay)
    {
        SendMessageW(g_scintilla, SCI_SETTEXT, 0, reinterpret_cast<LPARAM>(sReplayCase.sInitialText.c_str()));
        SendMessageW(g_scintilla, Scintilla::GOTOPOS, sReplayCase.nInitialCaret, 0);
        SetFocus(g_scintilla);
    }

    // Standard Win32 message loop: wait for an OS event, perform the keyboard
    // translation that produces WM_CHAR, then route the message to WindowProc
    // or to the appropriate child control/subclass.
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    // The parent window has already destroyed its child at this point, so the
    // order here safely releases the extension DLL before Scintilla.dll.
    g_scintilla = nullptr;
    if (g_vim_motions_module != nullptr)
    {
        FreeLibrary(g_vim_motions_module);
    }
    if (g_scintilla_module != nullptr)
    {
        FreeLibrary(g_scintilla_module);
    }
    return 0;
}
