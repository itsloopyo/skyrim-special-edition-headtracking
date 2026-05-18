#pragma once

namespace SkyrimHT {

// Installs a DX11 Present-phase hook. Each frame it reads the head pose and
// FOV from Mod::Instance(), projects the body-aim direction into the head-
// tracked view, and hands the resulting screen offset to the HUD menu hook,
// which repositions the game's native crosshair to match. Nothing is drawn
// here directly - the native reticle is moved, not replaced.
bool InitializeCrosshairOverlay();
void ShutdownCrosshairOverlay();

} // namespace SkyrimHT
