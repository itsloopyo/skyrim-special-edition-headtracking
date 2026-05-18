#include "pch.h"
#include "player_hook.h"
#include "camera_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "core/rtti_utils.h"
#include "game/skyrim_types.h"

namespace SkyrimHT {

// PlayerCharacter::Update is the canonical save/restore hook point per the
// project guide ("modify camera.transform with save/restore so game logic
// sees the clean rotation"). UpdateCrosshairs runs inside Update, populating
// the activate-target pickref from a raycast that ultimately reads
// cameraRoot.world.rotate. By restoring clean cameraRoot rotations around
// the original Update call, the pickref lands on whatever the body is aiming
// at. After Update returns, we restore the head-tracked rotations so the
// renderer (which runs later in the frame) shows the head-tracked view.
//
// Signature: virtual void Update(float deltaTime)
typedef void (__fastcall *PlayerUpdate_t)(void* thisPlayer, float deltaTime);
static PlayerUpdate_t g_originalUpdate = nullptr;
static void* g_hookedUpdate = nullptr;

// PlayerCharacter pointer captured on each hook tick - the projectile hook
// uses this to identify player-fired projectiles (vs NPC) so it can apply
// the lean offset only to the player's shots.
static std::atomic<void*> g_cachedPlayer{nullptr};
void* GetCachedPlayer() { return g_cachedPlayer.load(std::memory_order_relaxed); }

// vtable index of Actor::Update (TESObjectREFR::Update override). Verified
// against CommonLibSSE-NG; both PlayerCharacter and Character inherit at the
// same slot.
static constexpr int VTABLE_UPDATE_INDEX = 0xAD;

void __fastcall PlayerUpdateHook(void* thisPlayer, float deltaTime) {
    g_cachedPlayer.store(thisPlayer, std::memory_order_relaxed);
    CameraRootSnapshots snap;
    bool wrapping = Mod::Instance().IsEnabled() && GetCameraRootSnapshots(snap) && snap.cameraRoot != 0;

    if (!wrapping) {
        g_originalUpdate(thisPlayer, deltaTime);
        return;
    }

    // Clean/tracked sandwich on cameraRoot ONLY. cameraRoot is the body-aim
    // parent node that gameplay logic reads; restoring it clean around Update
    // keeps interaction/AI body-relative, then we restore tracked for the
    // renderer.
    //
    // We deliberately do NOT sandwich niCamera.world / worldToCam here. Those
    // are renderer-facing: lighting, shadow, and visibility culling that run
    // inside PlayerCharacter::Update read NiCamera, so feeding them the clean
    // matrices while the frame is ultimately drawn with the tracked matrices
    // produced a sharp light/dark seam at the clean-frustum edge. The
    // interaction raycast's direction is overridden independently in
    // crosshair_override.cpp, so the niCamera restore here bought nothing.
    NiMatrix33* rootLocalRot = reinterpret_cast<NiMatrix33*>(snap.cameraRoot + NiAVObjectOffsets::LocalTransform);
    NiMatrix33* rootWorldRot = reinterpret_cast<NiMatrix33*>(snap.cameraRoot + NiAVObjectOffsets::WorldTransform);

    std::memcpy(rootLocalRot->entry, snap.cleanLocal, sizeof(snap.cleanLocal));
    std::memcpy(rootWorldRot->entry, snap.cleanWorld, sizeof(snap.cleanWorld));

    g_originalUpdate(thisPlayer, deltaTime);

    std::memcpy(rootLocalRot->entry, snap.trackedLocal, sizeof(snap.trackedLocal));
    std::memcpy(rootWorldRot->entry, snap.trackedWorld, sizeof(snap.trackedWorld));
}

bool InstallPlayerHook() {
    Logger::Instance().Info("Installing player (Update wrap) hook...");

    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) {
        Logger::Instance().Error("Failed to get game module handle for player hook");
        return false;
    }

    MODULEINFO modInfo = {};
    if (!GetModuleInformation(GetCurrentProcess(), gameModule, &modInfo, sizeof(modInfo))) {
        Logger::Instance().Error("GetModuleInformation failed for player hook: %lu", GetLastError());
        return false;
    }
    const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(gameModule);
    const size_t moduleSize = modInfo.SizeOfImage;

    uintptr_t vtable = FindVtableByRTTI(moduleBase, moduleSize, ".?AVPlayerCharacter@@");
    if (vtable == 0) {
        Logger::Instance().Error("PlayerCharacter vtable not found - Update hook skipped");
        return false;
    }
    Logger::Instance().Info("PlayerCharacter vtable at: 0x%llX", vtable);

    uintptr_t updateFunc = *reinterpret_cast<uintptr_t*>(vtable + VTABLE_UPDATE_INDEX * 8);
    if (updateFunc == 0 || updateFunc < moduleBase || updateFunc >= moduleBase + moduleSize) {
        Logger::Instance().Error("Invalid Update pointer at vtable[0x%X]: 0x%llX",
                                  VTABLE_UPDATE_INDEX, updateFunc);
        return false;
    }
    Logger::Instance().Info("PlayerCharacter::Update at: 0x%llX (RVA 0x%llX)", updateFunc, updateFunc - moduleBase);

    g_hookedUpdate = reinterpret_cast<void*>(updateFunc);
    MH_STATUS status = MH_CreateHook(
        g_hookedUpdate,
        reinterpret_cast<LPVOID>(&PlayerUpdateHook),
        reinterpret_cast<LPVOID*>(&g_originalUpdate)
    );
    if (status != MH_OK) {
        Logger::Instance().Error("MH_CreateHook(PlayerUpdate) failed: %d", static_cast<int>(status));
        g_hookedUpdate = nullptr;
        return false;
    }

    Logger::Instance().Info("Player (Update wrap) hook installed");
    return true;
}

void RemovePlayerHook() {
    if (g_hookedUpdate) {
        MH_DisableHook(g_hookedUpdate);
        MH_RemoveHook(g_hookedUpdate);
        g_hookedUpdate = nullptr;
        g_originalUpdate = nullptr;
        Logger::Instance().Info("Player hook removed");
    }
}

} // namespace SkyrimHT
