#pragma once

#include <windows.h>

class ScintillaEditor
{
  public:
    explicit ScintillaEditor(HWND hWindow);

    [[nodiscard]] LRESULT
    Send(UINT nMessage, WPARAM wParam = 0, LPARAM lParam = 0) const;

  private:
    HWND m_hWindow;
};
