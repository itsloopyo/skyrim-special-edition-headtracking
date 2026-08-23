#pragma once

namespace SkyrimHT {

// Hook Projectile::Launch. The function is identified by Address Library
// relocation ID 44108 on AE 1.6.x, but nothing is resolved through the Address
// Library at runtime: SSEAddresses::Initialize() adds this build's hardcoded
// RVA to the module base. The hook reads the camera hook's
// current 6DOF lean offset and adds it to LaunchData.origin so projectiles
// fired by the player launch from the leaned camera position. Without this,
// 6DOF lean translates the rendered view but leaves the arrow's launch point
// body-relative - the arrow appears to fly out of empty space beside the
// player and lands away from the reticle.
//
// SSEAddresses::Initialize() must succeed before InstallProjectileHook.
// Returns false if address resolution or MinHook installation fails - the
// rest of the mod still works in that case, just without lean-synced shots.
bool InstallProjectileHook();
void RemoveProjectileHook();

} // namespace SkyrimHT
