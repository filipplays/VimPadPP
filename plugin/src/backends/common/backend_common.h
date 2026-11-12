#pragma once
#include <windows.h>
class Backend
{
  public:
    explicit Backend(HWND hWindow) : m_hWindow(hWindow) {}
    virtual ~Backend()       = default;
    virtual BOOL Install()   = 0;
    virtual BOOL Uninstall() = 0;
    virtual void OnCaretMoved(LRESULT) {}
    virtual bool PassesKeyToHost(WPARAM) const { return false; }
    virtual bool PrepareHostKey() { return true; }
    virtual BOOL
    bHandle_input(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR nId, DWORD_PTR nData) = 0;

  protected:
    HWND m_hWindow;
};
