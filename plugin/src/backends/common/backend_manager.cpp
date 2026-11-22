#include "backend_manager.h"
#include "ScintillaMessages.h"
#include <commctrl.h>
#if defined(ENABLE_NATIVE_BACKEND)
#include "native_backend.h"
#else
#include "neovim_backend.h"
#endif
#if defined(ENABLE_NATIVE_BACKEND)
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
    auto pSession = std::make_unique<Backend_NATIVE>(hWindow);
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

    return TRUE;
}
BOOL Manager::NotifyCaretMoved(HWND hWindow, LRESULT nPosition)
{
    const auto itSession = m_aSessions.find(hWindow);
    if (itSession == m_aSessions.end())
    {
        SetLastError(ERROR_NOT_FOUND);
        return FALSE;
    }
    itSession->second->OnCaretMoved(nPosition);
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
void Manager::ActivateDocument(HWND, UINT_PTR) {}
void Manager::CloseDocument(UINT_PTR) {}
#else

thread_local Manager *Manager::s_pKeyboardManager = nullptr;

Backend *Manager::FindSession(HWND hWindow)
{
    auto itView = m_aViews.find(hWindow);
    if (itView == m_aViews.end())
        return nullptr;
    const LRESULT nDocument = SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0);
    if (itView->second != nDocument)
    {
        if (auto itOld = m_aDocuments.find(itView->second);
            itOld != m_aDocuments.end() && itOld->second.hView == hWindow)
        {
            static_cast<Backend_NVIM *>(itOld->second.pBackend.get())->SuspendView();
            itOld->second.hView = nullptr;
        }
        itView->second = nDocument;
    }
    auto itDocument = m_aDocuments.find(nDocument);
    if (itDocument == m_aDocuments.end())
    {
        // Reserve the entry before installation: Scintilla calls may reenter.
        m_aDocuments.try_emplace(nDocument);
        auto pBackend = std::make_unique<Backend_NVIM>(hWindow, m_pSaveHandler);
        if (!pBackend->Install())
        {
            pBackend->Uninstall();
            m_aDocuments.erase(nDocument);
            return nullptr;
        }
        SendMessageW(hWindow, Scintilla::ADDREFDOCUMENT, 0, nDocument);
        m_aDocuments.at(nDocument).pBackend = std::move(pBackend);
        m_aDocuments.at(nDocument).hView    = hWindow;
        return m_aDocuments.at(nDocument).pBackend.get();
    }
    if (!itDocument->second.pBackend)
        return nullptr;
    // A cloned document shares one process. Only its focused view owns input/UI.
    if (itDocument->second.hView != hWindow)
    {
        auto *pBackend = static_cast<Backend_NVIM *>(itDocument->second.pBackend.get());
        pBackend->SuspendView();
        itDocument->second.hView = hWindow;
        if (!pBackend->ResumeView(hWindow))
        {
            pBackend->SuspendView();
            itDocument->second.hView = nullptr;
            return nullptr;
        }
    }
    return itDocument->second.pBackend.get();
}

LRESULT CALLBACK Manager::KeyboardMessageHook(int nCode, WPARAM wParam, LPARAM lParam)
{
    auto *pManager = s_pKeyboardManager;
    auto *pMessage = reinterpret_cast<MSG *>(lParam);
    if (nCode >= 0 && wParam == PM_REMOVE && pManager && pMessage && pMessage->hwnd == GetFocus() &&
        (pMessage->message == WM_KEYDOWN || pMessage->message == WM_SYSKEYDOWN))
    {
        auto        *pSession           = pManager->FindSession(pMessage->hwnd);
        const WPARAM nKey               = pMessage->wParam;
        const bool   bHandledControlKey = (nKey >= 'A' && nKey <= 'Z') || (nKey >= VK_PRIOR && nKey <= VK_DOWN) ||
                                          (nKey >= VK_F1 && nKey <= VK_F12) || nKey == VK_INSERT || nKey == VK_DELETE ||
                                          nKey == VK_RETURN || nKey == VK_BACK || nKey == VK_TAB || nKey == VK_SPACE;
        // Host shortcuts save the currently visible text and leave Vim input pending.
        if (pSession && !pSession->PassesKeyToHost(nKey) && pMessage->message == WM_KEYDOWN &&
            (GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) && bHandledControlKey)
        {
            SendMessageW(pMessage->hwnd, pMessage->message, nKey, pMessage->lParam);
            pMessage->message = WM_NULL;
        }
    }
    return CallNextHookEx(pManager ? pManager->m_hKeyboardHook : nullptr, nCode, wParam, lParam);
}

BOOL Manager::InstallVim(HWND hWindow)
{
    wchar_t aClassName[32]{};
    if (!IsWindow(hWindow) || !GetClassNameW(hWindow, aClassName, 32) || lstrcmpW(aClassName, L"Scintilla"))
        return FALSE;
    if (m_aViews.contains(hWindow))
        return TRUE;
    m_aViews.emplace(hWindow, 0);
    if (!SetWindowSubclass(hWindow, VimSubclassProc, 0, reinterpret_cast<DWORD_PTR>(this)))
    {
        m_aViews.erase(hWindow);
        return FALSE;
    }
    if (!FindSession(hWindow))
    {
        UninstallVim(hWindow);
        return FALSE;
    }
    if (!m_hKeyboardHook)
    {
        s_pKeyboardManager = this;
        m_hKeyboardHook    = SetWindowsHookExW(WH_GETMESSAGE, KeyboardMessageHook, nullptr, GetCurrentThreadId());
        if (!m_hKeyboardHook)
        {
            UninstallVim(hWindow);
            s_pKeyboardManager = nullptr;
            return FALSE;
        }
    }
    return TRUE;
}

void Manager::ActivateDocument(HWND hWindow, UINT_PTR nBufferId)
{
    if (auto *pBackend = FindSession(hWindow))
    {
        const LRESULT nDocument              = SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0);
        m_aDocuments.at(nDocument).nBufferId = nBufferId;
        (void)pBackend;
    }
}

void Manager::CloseDocument(UINT_PTR nBufferId)
{
    for (auto itDocument = m_aDocuments.begin(); itDocument != m_aDocuments.end();)
    {
        if (itDocument->second.nBufferId != nBufferId || !nBufferId)
        {
            ++itDocument;
            continue;
        }
        auto *pBackend = static_cast<Backend_NVIM *>(itDocument->second.pBackend.get());
        if (pBackend)
        {
            pBackend->SuspendView();
            pBackend->Uninstall();
        }
        if (!m_aViews.empty())
            SendMessageW(m_aViews.begin()->first, Scintilla::RELEASEDOCUMENT, 0, itDocument->first);
        itDocument = m_aDocuments.erase(itDocument);
    }
}

BOOL Manager::NotifyCaretMoved(HWND hWindow, LRESULT nPosition)
{
    // Ignore notifications from inactive clones and during document switches.
    auto itView = m_aViews.find(hWindow);
    if (itView == m_aViews.end() || itView->second != SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0))
        return FALSE;
    auto itDocument = m_aDocuments.find(itView->second);
    if (itDocument == m_aDocuments.end() || !itDocument->second.pBackend || itDocument->second.hView != hWindow)
        return FALSE;
    itDocument->second.pBackend->OnCaretMoved(nPosition);
    return TRUE;
}

BOOL Manager::UninstallVim(HWND hWindow)
{
    if (!m_aViews.contains(hWindow))
        return FALSE;
    RemoveWindowSubclass(hWindow, VimSubclassProc, 0);
    for (auto &[nDocument, sDocument] : m_aDocuments)
        if (sDocument.pBackend && sDocument.hView == hWindow)
        {
            static_cast<Backend_NVIM *>(sDocument.pBackend.get())->SuspendView();
            sDocument.hView = nullptr;
        }
    m_aViews.erase(hWindow);
    if (m_aViews.empty())
    {
        for (auto &[nDocument, sDocument] : m_aDocuments)
        {
            if (sDocument.pBackend)
                sDocument.pBackend->Uninstall();
            SendMessageW(hWindow, Scintilla::RELEASEDOCUMENT, 0, nDocument);
        }
        m_aDocuments.clear();
        if (m_hKeyboardHook)
            UnhookWindowsHookEx(m_hKeyboardHook);
        m_hKeyboardHook    = nullptr;
        s_pKeyboardManager = nullptr;
    }
    return TRUE;
}

LRESULT CALLBACK
Manager::VimSubclassProc(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR nId, DWORD_PTR nData)
{
    auto *pManager = reinterpret_cast<Manager *>(nData);
    if (nMessage == WM_NCDESTROY)
    {
        pManager->UninstallVim(hWindow);
        return DefSubclassProc(hWindow, nMessage, wParam, lParam);
    }
    // Document identity queries are also used by this subclass's routing logic.
    // Forward them directly to avoid recursively routing the query itself.
    if (nMessage == Scintilla::GETDOCPOINTER)
        return DefSubclassProc(hWindow, nMessage, wParam, lParam);
    // Background clone layout/timer messages must not steal the active session.
    auto itView = pManager->m_aViews.find(hWindow);
    if (itView == pManager->m_aViews.end())
        return DefSubclassProc(hWindow, nMessage, wParam, lParam);
    Backend *pBackend = nullptr;
    if (nMessage == WM_SETFOCUS || nMessage == WM_KEYDOWN || nMessage == WM_CHAR || nMessage == WM_LBUTTONDOWN)
        pBackend = pManager->FindSession(hWindow);
    else
    {
        auto itDocument = pManager->m_aDocuments.find(itView->second);
        if (itDocument != pManager->m_aDocuments.end() && itDocument->second.hView == hWindow &&
            itDocument->second.pBackend && itView->second == SendMessageW(hWindow, Scintilla::GETDOCPOINTER, 0, 0))
            pBackend = itDocument->second.pBackend.get();
    }
    if (pBackend && pBackend->bHandle_input(hWindow, nMessage, wParam, lParam, nId, nData))
        return 0;
    return DefSubclassProc(hWindow, nMessage, wParam, lParam);
}
#endif
namespace
{
Manager g_xManager;
}
VIM_MOTIONS_API BOOL WINAPI InstallVim(HWND hWindow) { return g_xManager.InstallVim(hWindow); }
VIM_MOTIONS_API BOOL WINAPI UninstallVim(HWND hWindow) { return g_xManager.UninstallVim(hWindow); }
