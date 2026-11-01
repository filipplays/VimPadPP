#include "native_backend.h"

#include "vim_engine.h"
#include <commctrl.h>

#include <memory>
#include <unordered_map>

BOOL Backend_NATIVE::bHandle_input(HWND, UINT nMessage, WPARAM wParam, LPARAM, UINT_PTR, DWORD_PTR)
{
    if (nMessage == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        m_pEngine->Cancel();
        return TRUE;
    }
    if (nMessage == WM_KEYDOWN && ((GetKeyState(VK_CONTROL) & 0x8000) != 0) && wParam >= 'A' && wParam <= 'Z')
    {
        m_pEngine->HandleCharacter(static_cast<unsigned char>(wParam + ('a' - 'A')), true, false);
        return TRUE;
    }
    if (nMessage == WM_CHAR && m_pEngine->GetMode() == VimEngine::Mode::Normal)
    {
        if (m_pEngine->HandleCharacter(static_cast<unsigned char>(wParam), false, false))
        {
            return TRUE;
        }
        else
        {
            return FALSE;
        }
    }
    return FALSE;
}

BOOL Backend_NATIVE::Install()
{
    m_pEngine = std::make_unique<VimEngine>(m_hWindow);
    return TRUE;
}

BOOL Backend_NATIVE::Uninstall() { return TRUE; }