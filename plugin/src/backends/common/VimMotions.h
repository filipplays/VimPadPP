#pragma once
#include <windows.h>
#if defined(VIM_MOTIONS_BUILD)
#define VIM_MOTIONS_API extern "C" __declspec(dllexport)
#else
#define VIM_MOTIONS_API extern "C"
#endif
VIM_MOTIONS_API BOOL WINAPI InstallVim(HWND hWindow);
VIM_MOTIONS_API BOOL WINAPI UninstallVim(HWND hWindow);
using InstallVimFn   = BOOL(WINAPI *)(HWND);
using UninstallVimFn = BOOL(WINAPI *)(HWND);
