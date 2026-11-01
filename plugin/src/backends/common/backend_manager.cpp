#include "backend_manager.h"
#include <commctrl.h>
#if defined(ENABLE_NATIVE_BACKEND)
#include "native_backend.h"
#else
#include "neovim_backend.h"
#endif
BOOL Manager::InstallVim(HWND hWindow)
{
    wchar_t aClassName[32]{};
    if (!IsWindow(hWindow) || !GetClassNameW(hWindow, aClassName, 32) || lstrcmpW(aClassName, L"Scintilla"))
    {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return FALSE;
    }
    if (m_aSessions.contains(hWindow))
        return TRUE;
#if defined(ENABLE_NATIVE_BACKEND)
    auto pSession = std::make_unique<Backend_NATIVE>();
#else
    auto pSession = std::make_unique<Backend_NVIM>();
#endif
    if (!pSession->Install(hWindow))
        return FALSE;
    if (!SetWindowSubclass(hWindow, VimSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this)))
    {
        DWORD nError = GetLastError();
        pSession->Uninstall(hWindow);
        SetLastError(nError);
        return FALSE;
    }
    m_aSessions.emplace(hWindow, std::move(pSession));
    return TRUE;
}
BOOL Manager::UninstallVim(HWND hWindow)
{
    auto itSession = m_aSessions.find(hWindow);
    if (itSession == m_aSessions.end())
    {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }
    if (!RemoveWindowSubclass(hWindow, VimSubclassProc, 0))
        return FALSE;
    BOOL bResult = itSession->second->Uninstall(hWindow);
    m_aSessions.erase(itSession);
    return bResult;
}
LRESULT CALLBACK
Manager::VimSubclassProc(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR nId, DWORD_PTR nData)
{
    auto pManager  = reinterpret_cast<Manager *>(nData);
    auto itSession = pManager->m_aSessions.find(hWindow);
    if (itSession == pManager->m_aSessions.end())
        return DefSubclassProc(hWindow, nMessage, wParam, lParam);
    if (nMessage == WM_NCDESTROY)
    {
        pManager->UninstallVim(hWindow);
        return DefSubclassProc(hWindow, nMessage, wParam, lParam);
    }
    if (itSession->second->bHandle_input(hWindow, nMessage, wParam, lParam, nId, nData))
        return 0;
    return DefSubclassProc(hWindow, nMessage, wParam, lParam);
}
namespace
{
Manager g_xManager;
}
VIM_MOTIONS_API BOOL WINAPI InstallVim(HWND hWindow) { return g_xManager.InstallVim(hWindow); }
VIM_MOTIONS_API BOOL WINAPI UninstallVim(HWND hWindow) { return g_xManager.UninstallVim(hWindow); }
