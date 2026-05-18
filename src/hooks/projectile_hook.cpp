#include "pch.h"
#include "projectile_hook.h"

#include "camera_hook.h"
#include "player_hook.h"
#include "core/logger.h"
#include "core/mod.h"
#include "core/sse_addresses.h"

#include <MinHook.h>

namespace SkyrimHT {

namespace {

// Projectile::Launch(ProjectileHandle* result, LaunchData& data).
// Both args are pointer-sized; we treat them as opaque. The LaunchData
// layout we depend on (matched against CommonLibSSE-NG):
//   +0x08: NiPoint3 origin   (3 floats, world coordinates)
//   +0x28: TESObjectREFR* shooter
constexpr uintptr_t kLaunchDataOriginOffset  = 0x08;
constexpr uintptr_t kLaunchDataShooterOffset = 0x28;

typedef void* (__fastcall* ProjectileLaunch_t)(void* result, void* data);
ProjectileLaunch_t g_originalLaunch = nullptr;
void* g_hookedLaunch = nullptr;

void* __fastcall ProjectileLaunchHook(void* result, void* data) {
    if (!data) return g_originalLaunch(result, data);

    __try {
        // Only translate player projectiles. NPC arrows fly under their own
        // physics with no 6DOF tracking - shifting their origins by our lean
        // offset would teleport every arrow in the world.
        const void* shooter = *reinterpret_cast<const void* const*>(
            reinterpret_cast<uintptr_t>(data) + kLaunchDataShooterOffset);
        if (shooter && shooter == GetCachedPlayer()) {
            float lx, ly, lz;
            if (GetCurrentLeanOffset(lx, ly, lz)) {
                float* origin = reinterpret_cast<float*>(
                    reinterpret_cast<uintptr_t>(data) + kLaunchDataOriginOffset);
                origin[0] += lx;
                origin[1] += ly;
                origin[2] += lz;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Bogus LaunchData layout - bail without modifying and let the
        // original run unmolested.
    }

    return g_originalLaunch(result, data);
}

} // namespace

bool InstallProjectileHook() {
    if (g_hookedLaunch) return true;
    if (!SSEAddresses::IsInitialized()) {
        Logger::Instance().Error(
            "ProjectileHook: SSE addresses not initialized");
        return false;
    }
    const uintptr_t addr = SSEAddresses::ProjectileLaunch_Function();
    if (addr == 0) {
        Logger::Instance().Error(
            "ProjectileHook: Projectile::Launch RVA not resolved");
        return false;
    }
    g_hookedLaunch = reinterpret_cast<void*>(addr);

    MH_STATUS status = MH_CreateHook(
        g_hookedLaunch,
        reinterpret_cast<LPVOID>(&ProjectileLaunchHook),
        reinterpret_cast<LPVOID*>(&g_originalLaunch));
    if (status != MH_OK) {
        Logger::Instance().Error("ProjectileHook: MH_CreateHook failed: %d",
                                  static_cast<int>(status));
        g_hookedLaunch = nullptr;
        return false;
    }
    status = MH_EnableHook(g_hookedLaunch);
    if (status != MH_OK) {
        Logger::Instance().Error("ProjectileHook: MH_EnableHook failed: %d",
                                  static_cast<int>(status));
        MH_RemoveHook(g_hookedLaunch);
        g_hookedLaunch = nullptr;
        return false;
    }
    Logger::Instance().Info("ProjectileHook: installed at 0x%llX",
                            (unsigned long long)addr);
    return true;
}

void RemoveProjectileHook() {
    if (g_hookedLaunch) {
        MH_DisableHook(g_hookedLaunch);
        MH_RemoveHook(g_hookedLaunch);
        g_hookedLaunch = nullptr;
        g_originalLaunch = nullptr;
    }
}

} // namespace SkyrimHT
