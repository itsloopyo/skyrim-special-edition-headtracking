#pragma once

#include <cstdint>

namespace SkyrimHT::SSEAddresses {

struct ExeVersion { int major, minor, patch, build; };

bool GetRunningExeVersion(ExeVersion& out);

// Initialize: reads SkyrimSE.exe version, checks against supported table,
// resolves singleton storage addresses by adding RVA to the loaded module base.
// Returns false if the running game build is unsupported.
bool Initialize();

bool IsInitialized();

// Absolute address of the function that fires the crosshair-pick raycast.
// Zero if Initialize hasn't been called or failed.
uintptr_t UpdatePickData_Function();

// Absolute address of Projectile::Launch(ProjectileHandle*, LaunchData&)
// (CommonLibSSE-NG RELOCATION_ID 44108 for AE 1.6.x). Zero if Initialize
// hasn't been called or failed.
uintptr_t ProjectileLaunch_Function();

// Absolute address of the Scaleform array element setter used by Compass::Update.
uintptr_t ScaleformArraySetElement_Function();

// Absolute address of the Scaleform method invoke helper used by Compass::Update.
uintptr_t ScaleformInvoke_Function();

// Absolute address of Compass::Update.
uintptr_t CompassUpdate_Function();

} // namespace SkyrimHT::SSEAddresses
