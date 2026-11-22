#pragma once

#include <cstddef>

namespace MsgPack
{
inline constexpr size_t MaxFrameBytes   = 32 * 1024 * 1024;
inline constexpr size_t MaxPayloadBytes = 16 * 1024 * 1024;
inline constexpr size_t MaxDecodedBytes = 64 * 1024 * 1024;
inline constexpr size_t MaxValues       = 512 * 1024;
} // namespace MsgPack
