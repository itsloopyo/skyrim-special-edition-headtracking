#pragma once

namespace SkyrimHT {

// Install the input polling thread for hotkey detection
bool InstallInputHook();

// Remove the input polling thread
void RemoveInputHook();

} // namespace SkyrimHT
