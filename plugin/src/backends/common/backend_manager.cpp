#include "backend_manager.h"
#include <commctrl.h>
#if defined(ENABLE_NATIVE_BACKEND)
#include "native_backend.h"
#else
#include "neovim_backend.h"
#endif
#if !defined(ENABLE_NATIVE_BACKEND)
thread_local Manager *Manager::s_pKeyboardManager = nullptr;

LRESULT CALLBACK Manager::KeyboardMessageHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    auto      *pMessage = reinterpret_cast<MSG *>(lParam);
    Manager   *pManager = s_pKeyboardManager;
    const bool bHandledControlKey =
        pMessage && ((pMessage->wParam >= 'A' && pMessage->wParam <= 'Z') ||
                     (pMessage->wParam >= VK_PRIOR && pMessage->wParam <= VK_DOWN) ||
                     (pMessage->wParam >= VK_F1 && pMessage->wParam <= VK_F12) || pMessage->wParam == VK_INSERT ||
                     pMessage->wParam == VK_DELETE || pMessage->wParam == VK_RETURN || pMessage->wParam == VK_BACK ||
                     pMessage->wParam == VK_TAB || pMessage->wParam == VK_SPACE);
    if (nCode >= 0 && wParam == PM_REMOVE && pManager && pMessage && pMessage->message == WM_KEYDOWN &&
        pMessage->hwnd == GetFocus() && pManager->m_aSessions.contains(pMessage->hwnd) &&
        (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && bHandledControlKey)
    {
        // Intercept before the host translates its accelerator table. The backend
        // handles this keydown directly, so no translated WM_CHAR is needed.
        SendMessageW(pMessage->hwnd, pMessage->message, pMessage->wParam, pMessage->lParam);
        pMessage->message = WM_NULL;
    }
    return CallNextHookEx(pManager ? pManager->m_hKeyboardHook : nullptr, nCode, wParam, lParam);
}
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
    auto pSession = std::make_unique<Backend_NATIVE>(hWindow);
#else
    auto pSession = std::make_unique<Backend_NVIM>(hWindow);
#endif
    if (!pSession->Install())
        return FALSE;
    if (!SetWindowSubclass(hWindow, VimSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this)))
    {
        DWORD nError = GetLastError();
        pSession->Uninstall();
        SetLastError(nError);
        return FALSE;
    }
    m_aSessions.emplace(hWindow, std::move(pSession));
#if !defined(ENABLE_NATIVE_BACKEND)
    if (!m_hKeyboardHook)
    {
        s_pKeyboardManager = this;
        m_hKeyboardHook    = SetWindowsHookExW(WH_GETMESSAGE, KeyboardMessageHook, nullptr, GetCurrentThreadId());
        if (!m_hKeyboardHook)
        {
            DWORD nError       = GetLastError();
            s_pKeyboardManager = nullptr;
            RemoveWindowSubclass(hWindow, VimSubclassProc, 0);
            m_aSessions.at(hWindow)->Uninstall();
            m_aSessions.erase(hWindow);
            SetLastError(nError);
            return FALSE;
        }
    }
#endif
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
    BOOL bResult = itSession->second->Uninstall();
    m_aSessions.erase(itSession);
#if !defined(ENABLE_NATIVE_BACKEND)
    if (m_aSessions.empty() && m_hKeyboardHook)
    {
        UnhookWindowsHookEx(m_hKeyboardHook);
        m_hKeyboardHook    = nullptr;
        s_pKeyboardManager = nullptr;
    }
#endif
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
