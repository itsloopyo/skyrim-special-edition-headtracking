#pragma once

#include <cstdint>

namespace SkyrimHT {

// Initialize once at startup. Returns false if the CrosshairPickData
// singleton couldn't be located - the override is a no-op in that case.
bool InitializeCrosshairOverride();

// Install a MinHook on PlayerCharacter::UpdateCrosshairs. The hook wraps the
// engine raycast: pre-call restores clean cameraRoot/niCamera so the raycast
// runs in body-aim direction; post-call restores the head-tracked state for
// the renderer.
bool InstallUpdateCrosshairsHook();
void RemoveUpdateCrosshairsHook();

} // namespace SkyrimHT
