#pragma once

namespace SkyrimHT {

// Hook PlayerCharacter::GetEyeVector (vtable index 0xC2) to override the
// activation-raycast direction with the body-aim direction. Without this,
// Skyrim's interaction system reads the head-tracked direction (via
// PlayerCamera::currentState->GetRotation), so the activate target follows
// where you LOOK instead of where you AIM.
//
// Call after InstallCameraHook so cameraRoot capture is in place.
bool InstallPlayerHook();
void RemovePlayerHook();

// PlayerCharacter pointer cached on each Update tick. The projectile hook
// uses this to distinguish player projectiles from NPC projectiles - only
// the player's launches get the 6DOF lean translation applied.
void* GetCachedPlayer();

} // namespace SkyrimHT
