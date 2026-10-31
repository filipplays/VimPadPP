#include "VimMotions.h"

#if defined(ENABLE_NATIVE_BACKEND)
#include "VimEngine.h"
#else
#include "NeovimBackend.h"
#endif

#if defined(ENABLE_NATIVE_BACKEND)
#include <commctrl.h>

#include <memory>
#include <unordered_map>

namespace
{
std::unordered_map<HWND, std::unique_ptr<VimEngine>> g_aEngines;

bool IsScintillaWindow(HWND hWindow)
{
    wchar_t aClassName[32]{};
    return IsWindow(hWindow) && GetClassNameW(hWindow, aClassName, 32) > 0 &&
           lstrcmpW(aClassName, L"Scintilla") == 0;
}

bool IsControlPressed() { return (GetKeyState(VK_CONTROL) & 0x8000) != 0; }

LRESULT CALLBACK VimSubclassProc(
    HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR,
    DWORD_PTR)
{
    const auto itEngine = g_aEngines.find(hWindow);
    if (itEngine != g_aEngines.end())
    {
        VimEngine &sEngine = *itEngine->second;
        if (nMessage == WM_KEYDOWN && wParam == VK_ESCAPE)
        {
            sEngine.Cancel();
            return 0;
        }
        if (nMessage == WM_KEYDOWN && IsControlPressed() && wParam >= 'A' &&
            wParam <= 'Z')
        {
            sEngine.HandleCharacter(
                static_cast<unsigned char>(wParam + ('a' - 'A')), true, false);
            return 0;
        }
        if (nMessage == WM_CHAR && sEngine.GetMode() == VimEngine::Mode::Normal)
        {
            return sEngine.HandleCharacter(
                       static_cast<unsigned char>(wParam), false, false)
                       ? 0
                       : DefSubclassProc(hWindow, nMessage, wParam, lParam);
        }
    }
    if (nMessage == WM_NCDESTROY)
    {
        g_aEngines.erase(hWindow);
        RemoveWindowSubclass(hWindow, VimSubclassProc, 0);
    }
    return DefSubclassProc(hWindow, nMessage, wParam, lParam);
}
} // namespace

VIM_MOTIONS_API BOOL WINAPI InstallVim(HWND hScintillaWindow)
{
    if (!IsScintillaWindow(hScintillaWindow))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (g_aEngines.contains(hScintillaWindow))
    {
        return TRUE;
    }
    if (!SetWindowSubclass(hScintillaWindow, VimSubclassProc, 0, 0))
    {
        return FALSE;
    }
    g_aEngines.emplace(
        hScintillaWindow, std::make_unique<VimEngine>(hScintillaWindow));
    return TRUE;
}

VIM_MOTIONS_API BOOL WINAPI UninstallVim(HWND hScintillaWindow)
{
    const auto itEngine = g_aEngines.find(hScintillaWindow);
    if (itEngine == g_aEngines.end())
    {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }
    if (!RemoveWindowSubclass(hScintillaWindow, VimSubclassProc, 0))
    {
        return FALSE;
    }
    g_aEngines.erase(itEngine);
    return TRUE;
}

#else

VIM_MOTIONS_API BOOL WINAPI InstallVim(HWND hScintillaWindow)
{
    return NeovimBackend::Install(hScintillaWindow);
}

VIM_MOTIONS_API BOOL WINAPI UninstallVim(HWND hScintillaWindow)
{
    return NeovimBackend::Uninstall(hScintillaWindow);
}

#endif
