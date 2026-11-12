#include "backends/common/ScintillaMessages.h"
#include "backends/common/backend_manager.h"
// #include "VimMotions.h"

#include <commctrl.h>
#include <shellapi.h>
#include <string>

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
constexpr UINT    NPPN_READY               = 1001;
constexpr UINT    NPPN_SHUTDOWN            = 1009;
constexpr UINT    SCN_UPDATEUI             = 2007;
constexpr UINT    NPPM_DOOPEN              = WM_USER + 1000 + 77;
constexpr UINT    NPPM_SETMENUITEMCHECK    = WM_USER + 1000 + 40;
constexpr UINT    NPPM_GETCURRENTSCINTILLA = WM_USER + 1000 + 4;
constexpr UINT    NPPM_SAVECURRENTFILE     = WM_USER + 1000 + 38;
NppData           g_sNpp{};
Manager           g_xManager;
bool              g_bEnabled   = false;
constexpr wchar_t PluginName[] = L"VimPad++";

void UpdateToggleMenu();

#if !defined(ENABLE_NATIVE_BACKEND)
void SaveView(HWND hWindow)
{
    int nCurrentView = -1;
    SendMessageW(g_sNpp.hNotepad, NPPM_GETCURRENTSCINTILLA, 0, reinterpret_cast<LPARAM>(&nCurrentView));
    HWND hCurrentView = nCurrentView == 0   ? g_sNpp.hMainScintilla
                        : nCurrentView == 1 ? g_sNpp.hSecondScintilla
                                            : nullptr;
    if (hCurrentView != hWindow)
    {
        MessageBoxW(
            g_sNpp.hNotepad, L"Could not save: the active editor view changed.", PluginName, MB_OK | MB_ICONERROR);
        return;
    }
    SendMessageW(g_sNpp.hNotepad, NPPM_SAVECURRENTFILE, 0, 0);
}
#endif

bool InstallViews()
{
#if !defined(ENABLE_NATIVE_BACKEND)
    g_xManager.SetSaveHandler(SaveView);
#endif
    BOOL bMainInstalled   = g_xManager.InstallVim(g_sNpp.hMainScintilla);
    BOOL bSecondInstalled = g_xManager.InstallVim(g_sNpp.hSecondScintilla);
    if (!bMainInstalled || !bSecondInstalled)
    {
        if (bMainInstalled)
            g_xManager.UninstallVim(g_sNpp.hMainScintilla);
        if (bSecondInstalled)
            g_xManager.UninstallVim(g_sNpp.hSecondScintilla);
    }
    g_bEnabled = bMainInstalled && bSecondInstalled;
    UpdateToggleMenu();
    return g_bEnabled;
}

void AttachViews() { InstallViews(); }

void DetachViews()
{
    g_xManager.UninstallVim(g_sNpp.hMainScintilla);
    g_xManager.UninstallVim(g_sNpp.hSecondScintilla);
    g_bEnabled = false;
    UpdateToggleMenu();
}

void ToggleViews()
{
    if (g_bEnabled)
        DetachViews();
    else
        AttachViews();
}

#if !defined(ENABLE_NATIVE_BACKEND)
void ModuleAnchor() {}

std::wstring ConfigDirectory(const wchar_t *pFolder = L"config")
{
    HMODULE hModule = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ModuleAnchor), &hModule))
        return {};
    wchar_t aPath[32768];
    DWORD   nLength = GetModuleFileNameW(hModule, aPath, 32768);
    if (!nLength || nLength == 32768)
        return {};
    std::wstring sPath(aPath, nLength);
    size_t       nSlash = sPath.find_last_of(L"\\/");
    return nSlash == std::wstring::npos ? std::wstring{} : sPath.substr(0, nSlash) + L"\\" + pFolder;
}

bool EnsureConfigDirectory(const std::wstring &sDirectory)
{
    if (sDirectory.empty())
        return false;
    if (CreateDirectoryW(sDirectory.c_str(), nullptr))
        return true;
    DWORD nAttributes = GetFileAttributesW(sDirectory.c_str());
    return nAttributes != INVALID_FILE_ATTRIBUTES && (nAttributes & FILE_ATTRIBUTE_DIRECTORY);
}

void OpenConfigFolder()
{
    std::wstring sDirectory = ConfigDirectory();
    if (!EnsureConfigDirectory(sDirectory) ||
        reinterpret_cast<INT_PTR>(
            ShellExecuteW(g_sNpp.hNotepad, L"open", sDirectory.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
        MessageBoxW(
            g_sNpp.hNotepad, (L"Could not create or open the config folder:\n" + sDirectory).c_str(), PluginName,
            MB_OK | MB_ICONERROR);
}

void OpenOrCreateConfig(const std::wstring &sDirectory, const std::wstring &sName, const char *pStarter)
{
    if (!EnsureConfigDirectory(sDirectory))
    {
        MessageBoxW(
            g_sNpp.hNotepad, (L"Could not create the config folder:\n" + sDirectory).c_str(), PluginName,
            MB_OK | MB_ICONERROR);
        return;
    }
    std::wstring sFile = sDirectory + L"\\" + sName;
    if (GetFileAttributesW(sFile.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        HANDLE hFile =
            CreateFileW(sFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE)
        {
            MessageBoxW(g_sNpp.hNotepad, (L"Could not create " + sFile).c_str(), PluginName, MB_OK | MB_ICONERROR);
            return;
        }
        DWORD nSize    = static_cast<DWORD>(lstrlenA(pStarter));
        DWORD nWritten = 0;
        bool  bWritten = WriteFile(hFile, pStarter, nSize, &nWritten, nullptr) && nWritten == nSize;
        CloseHandle(hFile);
        if (!bWritten)
        {
            MessageBoxW(g_sNpp.hNotepad, L"Could not write the starter config.", PluginName, MB_OK | MB_ICONERROR);
            return;
        }
    }
    if (!SendMessageW(g_sNpp.hNotepad, NPPM_DOOPEN, 0, reinterpret_cast<LPARAM>(sFile.c_str())))
        MessageBoxW(g_sNpp.hNotepad, (L"Could not open " + sFile).c_str(), PluginName, MB_OK | MB_ICONERROR);
}

void OpenPluginConfig()
{
    OpenOrCreateConfig(
        ConfigDirectory(), L"vimpad.lua",
        "-- VimPad++ settings. Neovim settings belong in neovim/init.lua.\r\n"
        "return {\r\n"
        "  passthrough_keys = {\r\n"
        "    -- Uncomment to let Notepad++ handle these shortcuts:\r\n"
        "    -- \"<C-s>\", \"<C-Tab>\", \"<C-S-Tab>\",\r\n"
        "  },\r\n"
        "}\r\n");
}

void OpenConfig()
{
    OpenOrCreateConfig(ConfigDirectory(L"neovim"), L"init.lua", "-- Add your Neovim settings and mappings here.\r\n");
}

void ReloadConfig()
{
    bool bWasEnabled = g_bEnabled;
    DetachViews();
    bool bLoaded = InstallViews();
    if (bLoaded && !bWasEnabled)
        DetachViews();
    if (bLoaded)
        MessageBoxW(
            g_sNpp.hNotepad, L"VimPad++ and Neovim configurations reloaded successfully.", PluginName,
            MB_OK | MB_ICONINFORMATION);
}
#endif

FuncItem g_aFunctions[] = {
    {L"Enable VimPad++", ToggleViews, 0, false, nullptr},
#if !defined(ENABLE_NATIVE_BACKEND)
    {L"Open Neovim config", OpenConfig, 0, false, nullptr},
    {L"Open VimPad++ config", OpenPluginConfig, 0, false, nullptr},
    {L"Open config folder", OpenConfigFolder, 0, false, nullptr},
    {L"Reload VimPad++ and Neovim configs", ReloadConfig, 0, false, nullptr},
#endif
};

void UpdateToggleMenu()
{
    SendMessageW(g_sNpp.hNotepad, NPPM_SETMENUITEMCHECK, g_aFunctions[0].nCommandId, g_bEnabled);
}
} // namespace

extern "C" __declspec(dllexport) void setInfo(NppData sData) { g_sNpp = sData; }

extern "C" __declspec(dllexport) const wchar_t *getName() { return PluginName; }

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
    if ((pHeader->hwndFrom == g_sNpp.hMainScintilla || pHeader->hwndFrom == g_sNpp.hSecondScintilla) &&
        pHeader->code == SCN_UPDATEUI)
    {
        const LRESULT nPosition = SendMessageW(pHeader->hwndFrom, Scintilla::GETCURRENTPOS, 0, 0);
        g_xManager.NotifyCaretMoved(pHeader->hwndFrom, nPosition);
        return;
    }
    if (pHeader->hwndFrom != g_sNpp.hNotepad)
        return;
    if (pHeader->code == NPPN_READY)
        AttachViews();
    else if (pHeader->code == NPPN_SHUTDOWN)
        DetachViews();
}

extern "C" __declspec(dllexport) LRESULT messageProc(UINT, WPARAM, LPARAM) { return 0; }

extern "C" __declspec(dllexport) BOOL isUnicode() { return TRUE; }
