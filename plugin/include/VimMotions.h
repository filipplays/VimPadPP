#pragma once

#include <windows.h>

// This header is the intentionally small boundary between a host application
// and the VimMotions DLL. HWND is the Windows handle identifying one Scintilla
// editor. The host loads these functions with GetProcAddress, so it does not
// link against an import library.
#if defined(VIM_MOTIONS_BUILD)
#define VIM_MOTIONS_API extern "C" __declspec(dllexport)
#else
#define VIM_MOTIONS_API extern "C"
#endif

// Install adds the Vim message subclass and creates the per-editor state.
// Uninstall removes it. Both return TRUE on success and FALSE on failure; use
// GetLastError() if a caller needs the reason.
VIM_MOTIONS_API BOOL WINAPI InstallVim(HWND scintilla_window);
VIM_MOTIONS_API BOOL WINAPI UninstallVim(HWND scintilla_window);

// The exact function-pointer types required when casting GetProcAddress.
using InstallVimFn   = BOOL(WINAPI *)(HWND scintilla_window);
using UninstallVimFn = BOOL(WINAPI *)(HWND scintilla_window);
