#pragma once
#include "backend_common.h"
#include "vim_engine.h"
#include <memory>
class Backend_NATIVE : public Backend
{
  public:
    BOOL Install(HWND hWindow) override;
    BOOL Uninstall(HWND hWindow) override;
    BOOL
    bHandle_input(HWND hWindow, UINT nMessage, WPARAM wParam, LPARAM lParam, UINT_PTR nId, DWORD_PTR nData) override;

  private:
    std::unique_ptr<VimEngine> m_pEngine;
};
