#pragma once
#include <windows.h>
class Backend
{
  public:
    virtual ~Backend()                   = default;
    virtual BOOL Install(HWND hWindow)   = 0;
    virtual BOOL Uninstall(HWND hWindow) = 0;
    virtual BOOL
    bHandle_input(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR nId, DWORD_PTR nData) = 0;
};
