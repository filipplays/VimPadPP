#include "backends/common/backend_manager.h"
// #include "VimMotions.h"

#include <commctrl.h>

// Minimal Notepad++ plugin ABI. See the official PluginInterface.h.
struct NppData
{
    // WHy are there two scintilla windows
    HWND hNotepad;
    HWND hMainScintilla;
    HWND hSecondScintilla;
};
struct ShortcutKey;
struct FuncItem
{
    wchar_t aName[64];
    void (*pFunction)();
    int          nCommandId;
    bool         bChecked;
    ShortcutKey *pShortcut;
};
struct SCNotification;

namespace
{
constexpr UINT NPPN_READY    = 1001;
constexpr UINT NPPN_SHUTDOWN = 1009;
NppData        g_sNpp{};
Manager g_xManager;

void AttachViews()
{
    g_xManager.InstallVim(g_sNpp.hMainScintilla);
    g_xManager.InstallVim(g_sNpp.hSecondScintilla);
}

void DetachViews()
{
    g_xManager.UninstallVim(g_sNpp.hMainScintilla);
    g_xManager.UninstallVim(g_sNpp.hSecondScintilla);
}

FuncItem g_aFunctions[] = {
    {L"Enable Vim motions", AttachViews, 0, false, nullptr}};
} // namespace

extern "C" __declspec(dllexport) void setInfo(NppData sData) { g_sNpp = sData; }

extern "C" __declspec(dllexport) const wchar_t *getName()
{
    return L"VIM Motions";
}

extern "C" __declspec(dllexport) FuncItem *getFuncsArray(int *pCount)
{
    *pCount = static_cast<int>(sizeof(g_aFunctions) / sizeof(g_aFunctions[0]));
    return g_aFunctions;
}

extern "C" __declspec(dllexport) void beNotified(SCNotification *pNotification)
{
    if (!pNotification)
        return;
    // SCNotification starts with the Windows NMHDR structure.
    const auto *pHeader = reinterpret_cast<const NMHDR *>(pNotification);
    if (pHeader->hwndFrom != g_sNpp.hNotepad)
        return;
    if (pHeader->code == NPPN_READY)
        AttachViews();
    else if (pHeader->code == NPPN_SHUTDOWN)
        DetachViews();
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM)
{
    return 0;
}

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
