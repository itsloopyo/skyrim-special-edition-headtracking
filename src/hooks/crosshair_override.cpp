#include "pch.h"
#include "crosshair_override.h"

#include "core/constants.h"
#include "core/logger.h"
#include "core/mod.h"
#include "core/sse_addresses.h"
#include "game/skyrim_types.h"
#include "hooks/camera_hook.h"

#include <MinHook.h>

namespace SkyrimHT {

// ============================================================================
// Crosshair pick hook
// ============================================================================
//
// We hook the function at RVA 0x402C60. It IS the engine's pick-ray writer:
// the first argument is a CrosshairPickData* and the function fires a havok
// ray, writing the resulting REFR handle to self->target (+0x04) and the
// world-coord hit point to self->collisionPoint (+0x10..+0x1B). Layout
// matches CommonLibSSE-NG (no version conditionals across SE/AE 1.6.x).
//
// Signature:
//   void Update(CrosshairPickData* self,
//               bhkWorld*          world,
//               float*             origin,      // vec3 world position
//               float*             direction);  // vec3 world direction
//
// Two things happen in this hook:
//
//   1. Direction override. We replace `direction` with the body-aim direction
//      (col 0 of the CLEAN niCamera world rotation) before the call so the
//      ray fires where the mouse is aimed, not where the head-tracked view
//      is centered. This makes activate-target / projectile-aim track the
//      true crosshair.
//
//   2. Hit-point publish. After the call, self->collisionPoint holds the
//      world-coord point the ray hit. We publish it via a seqlock so the
//      crosshair overlay can project it through the *tracked* camera matrix
//      and draw the reticle exactly where the bullet will land - no parallax
//      drift under lean, no distance dependence.
// ============================================================================

namespace {

typedef void (__fastcall *UpdatePickData_t)(
    void* self, void* world, float* origin, float* direction);
UpdatePickData_t g_originalUpdatePickData = nullptr;
void* g_hookedUpdatePickData = nullptr;

bool ResolveCrosshairPickDataSingleton() {
    if (!SSEAddresses::Initialize()) {
        Logger::Instance().Warning(
            "Crosshair override: SSE address resolution failed.");
        return false;
    }
    return true;
}

void __fastcall UpdatePickDataHook(
    void* self, void* world, float* origin, float* direction) {

    CameraRootSnapshots snap;
    const bool wrapping = Mod::Instance().IsEnabled()
                       && GetCameraRootSnapshots(snap)
                       && snap.niCamera != 0
                       && direction != nullptr;

    if (!wrapping) {
        g_originalUpdatePickData(self, world, origin, direction);
        return;
    }

    // Save caller's direction (head-tracked forward).
    float saved[3] = { direction[0], direction[1], direction[2] };

    // Body-aim direction = column 0 of the CLEAN niCamera world rotation.
    // The game's own aim direction is column 0 of the (head-tracked) niCamera
    // matrix - verified at runtime by matching the caller's `direction` arg
    // against the tracked niCamera columns. cameraRoot is the yaw-only parent
    // node and carries no pitch, so its columns can't be used here; niCamera
    // is the node that holds the full pitched aim. Taking the clean (pre-head-
    // tracking) niCamera column yields the mouse-aimed direction.
    direction[0] = snap.cleanNiCamWorld[0][0];
    direction[1] = snap.cleanNiCamWorld[1][0];
    direction[2] = snap.cleanNiCamWorld[2][0];

    g_originalUpdatePickData(self, world, origin, direction);

    // Restore caller's direction so we don't perturb anything else.
    direction[0] = saved[0];
    direction[1] = saved[1];
    direction[2] = saved[2];
}

} // namespace

bool InitializeCrosshairOverride() {
    return ResolveCrosshairPickDataSingleton();
}

bool InstallUpdateCrosshairsHook() {
    if (g_hookedUpdatePickData) return true;
    if (!SSEAddresses::IsInitialized()) {
        Logger::Instance().Error("UpdatePickData hook: SSE addresses not initialized");
        return false;
    }
    uintptr_t addr = SSEAddresses::UpdatePickData_Function();
    if (addr == 0) {
        Logger::Instance().Error("UpdatePickData hook: function RVA not resolved");
        return false;
    }
    g_hookedUpdatePickData = reinterpret_cast<void*>(addr);
    MH_STATUS status = MH_CreateHook(
        g_hookedUpdatePickData,
        reinterpret_cast<LPVOID>(&UpdatePickDataHook),
        reinterpret_cast<LPVOID*>(&g_originalUpdatePickData));
    if (status != MH_OK) {
        Logger::Instance().Error("MH_CreateHook(UpdatePickData) failed: %d", (int)status);
        g_hookedUpdatePickData = nullptr;
        return false;
    }
    status = MH_EnableHook(g_hookedUpdatePickData);
    if (status != MH_OK) {
        Logger::Instance().Error("MH_EnableHook(UpdatePickData) failed: %d", (int)status);
        MH_RemoveHook(g_hookedUpdatePickData);
        g_hookedUpdatePickData = nullptr;
        return false;
    }
    Logger::Instance().Info("UpdatePickData hook installed at 0x%llX",
        (unsigned long long)addr);
    return true;
}

void RemoveUpdateCrosshairsHook() {
    if (g_hookedUpdatePickData) {
        MH_DisableHook(g_hookedUpdatePickData);
        MH_RemoveHook(g_hookedUpdatePickData);
        g_hookedUpdatePickData = nullptr;
        g_originalUpdatePickData = nullptr;
    }
}

} // namespace SkyrimHT
