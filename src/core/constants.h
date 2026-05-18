#pragma once

#include <cstdint>

namespace SkyrimHT {

// Version info
inline constexpr const char* VERSION = "0.1.0";

// Target game executable
inline constexpr const char* GAME_EXE = "SkyrimSE.exe";

// Default UDP port for OpenTrack
inline constexpr uint16_t DEFAULT_UDP_PORT = 4242;

// Shared math constant
inline constexpr float DEG_TO_RAD = 0.0174533f;

// Default hotkey virtual key codes
inline constexpr int DEFAULT_TOGGLE_KEY = 0x23;          // VK_END - Enable/disable tracking
inline constexpr int DEFAULT_RECENTER_KEY = 0x24;         // VK_HOME - Recenter view
inline constexpr int DEFAULT_POSITION_TOGGLE_KEY = 0x21;  // VK_PRIOR (Page Up) - Toggle position
inline constexpr int DEFAULT_YAW_MODE_KEY = 0x22;         // VK_NEXT (Page Down) - Toggle world/local yaw

} // namespace SkyrimHT
