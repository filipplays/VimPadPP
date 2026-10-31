#pragma once

#include <windows.h>

namespace NeovimBackend
{
BOOL Install(HWND hScintillaWindow);
BOOL Uninstall(HWND hScintillaWindow);
} // namespace NeovimBackend
