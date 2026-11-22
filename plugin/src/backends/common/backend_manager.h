#pragma once
#include "VimMotions.h"
#include "backend_common.h"
#include <memory>
#include <unordered_map>
class Manager
{
  public:
    BOOL InstallVim(HWND hWindow);
    BOOL UninstallVim(HWND hWindow);
    BOOL NotifyCaretMoved(HWND hWindow, LRESULT nPosition);
    void SetSaveHandler(void (*pHandler)(HWND)) { m_pSaveHandler = pHandler; }
    void SetFailureHandler(void (*pHandler)()) { m_pFailureHandler = pHandler; }
    void ActivateDocument(HWND hWindow, UINT_PTR nBufferId);
    void CloseDocument(UINT_PTR nBufferId);

  private:
#if !defined(ENABLE_NATIVE_BACKEND)
    struct DispatchScope
    {
        explicit DispatchScope(Manager *pManager) : m_pManager(pManager)
        {
            if (m_pManager)
                ++m_pManager->m_nDispatchDepth;
        }
        ~DispatchScope()
        {
            if (m_pManager)
                m_pManager->FinishDispatch();
        }
        Manager *m_pManager;
    };
    static void                  OnBackendFailure(void *pContext);
    void                         FinishDispatch();
    unsigned                     m_nDispatchDepth  = 0;
    bool                         m_bDisabled       = false;
    bool                         m_bFailurePending = false;
    static LRESULT CALLBACK      KeyboardMessageHook(int nCode, WPARAM wParam, LPARAM lParam);
    static thread_local Manager *s_pKeyboardManager;
    HHOOK                        m_hKeyboardHook = nullptr;
    Backend                     *FindSession(HWND hWindow);
    struct DocumentSession
    {
        std::unique_ptr<Backend> pBackend;
        HWND                     hView     = nullptr;
        UINT_PTR                 nBufferId = 0;
    };
    std::unordered_map<LRESULT, DocumentSession> m_aDocuments;
    std::unordered_map<HWND, LRESULT>            m_aViews;
#endif
    static LRESULT CALLBACK
    VimSubclassProc(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR nId, DWORD_PTR nData);
#if defined(ENABLE_NATIVE_BACKEND)
    std::unordered_map<HWND, std::unique_ptr<Backend>> m_aSessions;
#endif
    void (*m_pSaveHandler)(HWND) = nullptr;
    void (*m_pFailureHandler)()  = nullptr;
};
