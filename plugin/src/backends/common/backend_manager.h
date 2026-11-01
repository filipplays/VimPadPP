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

  private:
#if !defined(ENABLE_NATIVE_BACKEND)
    static LRESULT CALLBACK      KeyboardMessageHook(int nCode, WPARAM wParam, LPARAM lParam);
    static thread_local Manager *s_pKeyboardManager;
    HHOOK                        m_hKeyboardHook = nullptr;
#endif
    static LRESULT CALLBACK
    VimSubclassProc(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR nId, DWORD_PTR nData);
    std::unordered_map<HWND, std::unique_ptr<Backend>> m_aSessions;
};
