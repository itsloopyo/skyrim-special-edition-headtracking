#pragma once

#include <string>

namespace SkyrimHT {

// Convert virtual key code to human-readable string
const char* VirtualKeyToString(int vkCode);

// Format hotkey configuration as a string for display
std::string FormatHotkeyConfig(int toggleKey, int recenterKey);

} // namespace SkyrimHT
