#include "ScintillaEditor.h"

ScintillaEditor::ScintillaEditor(HWND hWindow) : m_hWindow(hWindow) {}

LRESULT
ScintillaEditor::Send(UINT nMessage, WPARAM wParam, LPARAM lParam) const
{
    return SendMessageW(m_hWindow, nMessage, wParam, lParam);
}
