#pragma once

// Host allocations only. Guest RAM sizes and save-state layouts are separate.
#ifdef PS2_SWITCH2_HOST_BUFFER_SCALE
inline constexpr unsigned Switch2HostBufferScale = PS2_SWITCH2_HOST_BUFFER_SCALE;
#else
inline constexpr unsigned Switch2HostBufferScale = 1;
#endif
