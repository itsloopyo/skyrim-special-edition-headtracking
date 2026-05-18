#include "pch.h"
#include "camera_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "game/skyrim_types.h"
#include "game/game_state.h"
#include "core/rtti_utils.h"
#include <cameraunlock/memory/pattern_scanner.h>

namespace SkyrimHT {

// TESCamera::Update is void Update() - no deltaTime parameter.
typedef void (__fastcall *PlayerCameraUpdate_t)(void* thisCamera);
static PlayerCameraUpdate_t g_originalUpdate = nullptr;

static void* g_hookedAddress = nullptr;
static std::atomic<bool> g_hookEnabled{false};

// cameraRoot rotation snapshots: clean (pre-head-tracking) and tracked
// (post-head-tracking), captured each PlayerCamera::Update tick. Read by the
// PlayerCharacter::Update wrapper and the crosshair reader paths.
//
// Published via a seqlock: the writer bumps g_snapshotsSeq to an odd value
// before touching g_snapshots and to the next even value after. A reader that
// observes an odd value, or a value that changes across its copy, retries.
// This protects the multi-hundred-byte struct copy from torn reads regardless
// of whether a reader ever runs off the game's main thread.
static CameraRootSnapshots g_snapshots = {};
static std::atomic<uint32_t> g_snapshotsSeq{0};
static uint32_t g_writeSeq = 0;  // writer-private; only the camera hook touches it

// Current frame's 6DOF lean offset in game-unit world coords. Set by the
// camera hook after position-tracking processing; read by the projectile
// hook to translate arrow launch origins. Atomic floats so reads from the
// Projectile::Launch hook thread (whichever it is) don't tear.
static std::atomic<float> g_leanX{0.0f};
static std::atomic<float> g_leanY{0.0f};
static std::atomic<float> g_leanZ{0.0f};
static std::atomic<bool>  g_leanValid{false};

bool GetCurrentLeanOffset(float& x, float& y, float& z) {
    if (!g_leanValid.load(std::memory_order_acquire)) return false;
    x = g_leanX.load(std::memory_order_relaxed);
    y = g_leanY.load(std::memory_order_relaxed);
    z = g_leanZ.load(std::memory_order_relaxed);
    return true;
}

// Single-writer publish. The store/copy/store region has no early returns or
// calls, so g_writeSeq always steps even -> odd -> even cleanly.
static void PublishSnapshot(const CameraRootSnapshots& s) {
    g_snapshotsSeq.store(++g_writeSeq, std::memory_order_release);  // odd: in progress
    g_snapshots = s;
    g_snapshotsSeq.store(++g_writeSeq, std::memory_order_release);  // even: complete
}

bool ProjectBodyAimToScreenPixels(
    const CameraRootSnapshots& snap,
    float screenWidthPx,
    float screenHeightPx,
    float& outDxPx,
    float& outDyPx) {
    // Body-aim direction = column 0 of the CLEAN niCamera world rotation; the
    // tracked basis columns are 0=forward, 1=up, 2=right in world space, so
    // dotting expresses the aim in the tracked camera's local frame.
    const float wx = snap.cleanNiCamWorld[0][0];
    const float wy = snap.cleanNiCamWorld[1][0];
    const float wz = snap.cleanNiCamWorld[2][0];
    const float aimFwd   = snap.trackedNiCamWorld[0][0] * wx
                         + snap.trackedNiCamWorld[1][0] * wy
                         + snap.trackedNiCamWorld[2][0] * wz;
    const float aimUp    = snap.trackedNiCamWorld[0][1] * wx
                         + snap.trackedNiCamWorld[1][1] * wy
                         + snap.trackedNiCamWorld[2][1] * wz;
    const float aimRight = snap.trackedNiCamWorld[0][2] * wx
                         + snap.trackedNiCamWorld[1][2] * wy
                         + snap.trackedNiCamWorld[2][2] * wz;

    if (aimFwd < 0.01f || snap.frustumRight <= 0.0f || snap.frustumTop <= 0.0f) {
        return false;
    }

    // Scaleform/Skyrim screen coords: +x = right, +y = down. aimUp > 0 means
    // the body-aim is above centre, so negate for screen-y.
    const float ndcX =  aimRight / aimFwd / snap.frustumRight;
    const float ndcY = -aimUp    / aimFwd / snap.frustumTop;

    outDxPx = ndcX * screenWidthPx  * 0.5f;
    outDyPx = ndcY * screenHeightPx * 0.5f;
    return true;
}

bool GetCameraRootSnapshots(CameraRootSnapshots& out) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t s1 = g_snapshotsSeq.load(std::memory_order_acquire);
        if (s1 == 0 || (s1 & 1u)) return false;  // never published / write in progress
        out = g_snapshots;
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_snapshotsSeq.load(std::memory_order_relaxed) == s1) {
            return out.cameraRoot != 0;  // cameraRoot == 0 is the "invalid frame" marker
        }
        // Sequence moved during the copy - torn read, retry.
    }
    return false;
}

// Walk from PlayerCamera (thisCamera) to both cameraRoot and the NiCamera child.
// Path: PlayerCamera+0x20 to cameraRoot (NiNode) to children[0] to NiCamera.
// Returns both so the caller doesn't redo the cameraRoot dereference separately.
static bool GetSceneGraph(void* thisCamera, uintptr_t& cameraRoot, uintptr_t& niCamera) {
    cameraRoot = *reinterpret_cast<uintptr_t*>(
        reinterpret_cast<uintptr_t>(thisCamera) + TESCameraOffsets::CameraRoot);
    if (cameraRoot == 0) return false;

    uintptr_t childData = *reinterpret_cast<uintptr_t*>(cameraRoot + NiNodeOffsets::ChildrenData);
    if (childData == 0) return false;

    niCamera = *reinterpret_cast<uintptr_t*>(childData);  // children[0]
    return niCamera != 0;
}

void __fastcall PlayerCameraUpdateHook(void* thisCamera) {
    // Call original - engine positions the camera and computes worldToCam
    g_originalUpdate(thisCamera);

    if (!g_hookEnabled.load(std::memory_order_relaxed)) return;
    Mod& mod = Mod::Instance();
    if (!mod.IsEnabled()) return;
    if (!GameState::IsInGameplay()) return;

    // Built up locally over the frame, then published atomically via the
    // seqlock at the end. A zeroed snapshot (cameraRoot == 0) is the "invalid
    // frame" marker readers treat as "no data this frame".
    CameraRootSnapshots snapshot{};

    __try {
        uintptr_t cameraRoot = 0;
        uintptr_t niCamera = 0;
        if (!GetSceneGraph(thisCamera, cameraRoot, niCamera)) return;

        // Snapshot CLEAN cameraRoot rotations before any modification. Used by
        // the PlayerCharacter::Update wrapper to restore the clean rotation
        // around the original Update call, so UpdateCrosshairs (called inside
        // Update) sees body-aim direction.
        NiMatrix33* rootLocalRot = nullptr;
        NiMatrix33* rootWorldRot = nullptr;
        if (cameraRoot) {
            rootLocalRot = reinterpret_cast<NiMatrix33*>(cameraRoot + NiAVObjectOffsets::LocalTransform);
            rootWorldRot = reinterpret_cast<NiMatrix33*>(cameraRoot + NiAVObjectOffsets::WorldTransform);

            snapshot.cameraRoot = cameraRoot;
            std::memcpy(snapshot.cleanLocal, rootLocalRot->entry, sizeof(snapshot.cleanLocal));
            std::memcpy(snapshot.cleanWorld, rootWorldRot->entry, sizeof(snapshot.cleanWorld));
        }

        // Get head tracking rotation
        float yaw, pitch, roll;
        if (!mod.GetProcessedRotation(yaw, pitch, roll)) {
            PublishSnapshot(CameraRootSnapshots{});  // no tracking this frame
            return;
        }

        constexpr float NEG_DEG_TO_RAD = -DEG_TO_RAD;
        const float yawRad        = yaw   * NEG_DEG_TO_RAD;
        const float rollRad       = roll  * NEG_DEG_TO_RAD;
        const float pitchRad_view = pitch * DEG_TO_RAD;

        // niCamera + worldToCam (renderer-side) use this convention:
        //   X = forward, Y = up, Z = right.
        //   pitch axis = Z (right), so pitch maps to arg1.
        //   roll  axis = X (forward), so roll maps to arg2.
        //   yaw   axis = Y (up), so yaw maps to arg3 (but yaw is applied pre-mult in
        //                                     WORLD frame anyway).
        // headRotView is only consumed on the camera-local path; in world-yaw
        // mode the rotation is split into yawOnly + prOnlyView, so building it
        // there is dead work.
        const bool worldYaw = mod.IsWorldSpaceYaw();
        NiMatrix33 headRotView, yawOnly, prOnlyView;
        if (worldYaw) {
            // yawOnly is applied around WORLD Z (= world up). Convention-
            // independent so the same matrix works for both.
            yawOnly    = NiMatrix33::FromEulerAngles(yawRad, 0.0f, 0.0f);
            prOnlyView = NiMatrix33::FromEulerAngles(pitchRad_view, rollRad, 0.0f);
        } else {
            headRotView = NiMatrix33::FromEulerAngles(pitchRad_view, rollRad, yawRad);
        }

        // cameraRoot's rotation is its child niCamera's rotation expressed in
        // the parent (body) frame: niCamera.world = cameraRoot.world * L, where
        // L = niCamera.local is a fixed basis-change. So the body-frame head
        // rotation must be the view-frame rotation conjugated by L:
        // bodyRot = L * viewRot * L^T. Building it independently from Euler
        // angles disagreed with the view side as roll/pitch grew, so any
        // scene-graph rebuild of niCamera.world (block enter/exit animations
        // trigger one) snapped the view back un-tracked.
        const NiMatrix33* niCamLocal = reinterpret_cast<const NiMatrix33*>(
            niCamera + NiAVObjectOffsets::LocalTransform);
        NiMatrix33 niCamLocalT;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                niCamLocalT.entry[i][j] = niCamLocal->entry[j][i];

        NiMatrix33 headRotBody, prOnlyBody;
        if (worldYaw) {
            prOnlyBody = (*niCamLocal) * prOnlyView * niCamLocalT;
        } else {
            headRotBody = (*niCamLocal) * headRotView * niCamLocalT;
        }

        NiMatrix33* niCamWorldRot = reinterpret_cast<NiMatrix33*>(niCamera + NiAVObjectOffsets::WorldTransform);
        NiMatrix44* worldToCam    = reinterpret_cast<NiMatrix44*>(niCamera + NiCameraOffsets::WorldToCam);

        // Snapshot CLEAN niCamera world rotation + worldToCam matrix so
        // PlayerCharacter::Update can restore them around the original tick
        // (crosshair-pick raycast reads niCamera and/or worldToCam, not just
        // cameraRoot).
        snapshot.niCamera = niCamera;
        snapshot.frustumRight = *reinterpret_cast<const float*>(niCamera + NiCameraOffsets::FrustumRight);
        snapshot.frustumTop   = *reinterpret_cast<const float*>(niCamera + NiCameraOffsets::FrustumTop);
        std::memcpy(snapshot.cleanNiCamWorld, niCamWorldRot->entry, sizeof(snapshot.cleanNiCamWorld));
        std::memcpy(snapshot.cleanWorldToCam, worldToCam->entry, sizeof(snapshot.cleanWorldToCam));

        // Full-state head-tracking modification. Skyrim's render path reads
        // cameraRoot and niCamera, so we update both. The CLEAN cameraRoot
        // rotations were snapshotted above; the TRACKED versions are captured
        // below. PlayerCharacter::Update wraps with restore-clean -> run ->
        // restore-tracked so game logic (notably UpdateCrosshairs) sees the
        // body-aim direction during the player update tick.
        float posX, posY, posZ;
        bool hasPosition = mod.GetPositionOffset(posX, posY, posZ);
        NiPoint3 worldOffset(0.0f, 0.0f, 0.0f);
        if (hasPosition) {
            constexpr float UNITS_PER_METER = 70.0f;
            // Remap OT (X=right, Y=up, Z=fwd) -> Skyrim (X=right, Y=fwd, Z=up)
            NiPoint3 localOffset(posZ, posY, posX);
            worldOffset = *niCamWorldRot * localOffset;
            worldOffset.x *= UNITS_PER_METER;
            worldOffset.y *= UNITS_PER_METER;
            worldOffset.z *= UNITS_PER_METER;
        }
        // Publish for the projectile hook to consume. Mark invalid when
        // no position offset is being applied this frame, so the projectile
        // hook can skip the add and avoid drifting arrows when tracking is
        // off / paused / centered.
        g_leanX.store(worldOffset.x, std::memory_order_relaxed);
        g_leanY.store(worldOffset.y, std::memory_order_relaxed);
        g_leanZ.store(worldOffset.z, std::memory_order_relaxed);
        g_leanValid.store(hasPosition, std::memory_order_release);

        // niCamWorldRot drives rendering; treat it as graphics convention so
        // pitch lands on X = right rather than Y = left/up.
        if (worldYaw) {
            *niCamWorldRot = yawOnly * *niCamWorldRot * prOnlyView;
        } else {
            *niCamWorldRot = *niCamWorldRot * headRotView;
        }

        if (rootLocalRot && rootWorldRot) {
            if (worldYaw) {
                *rootLocalRot = yawOnly * *rootLocalRot * prOnlyBody;
                *rootWorldRot = yawOnly * *rootWorldRot * prOnlyBody;
            } else {
                *rootLocalRot = *rootLocalRot * headRotBody;
                *rootWorldRot = *rootWorldRot * headRotBody;
            }

            std::memcpy(snapshot.trackedLocal, rootLocalRot->entry, sizeof(snapshot.trackedLocal));
            std::memcpy(snapshot.trackedWorld, rootWorldRot->entry, sizeof(snapshot.trackedWorld));
            std::memcpy(snapshot.trackedNiCamWorld, niCamWorldRot->entry, sizeof(snapshot.trackedNiCamWorld));
        }

        // worldToCam is the renderer's view matrix in GRAPHICS convention.
        // In local-yaw mode pre-multiply by headRotView so the renderer's
        // effective view matches our niCamera.world modification - without
        // this, head roll is visible on the rendered scene but our marker
        // reprojection's tracked basis ends up out of sync with the renderer
        // for roll combined with mouse yaw/pitch, producing perpendicular
        // marker drift (sin(roll) * mouse_angle). World-yaw mode must NOT
        // write worldToCam: the split prOnlyView/yawOnly formula leaves it
        // inconsistent with niCamera.world and a culling/lighting pass that
        // reads worldToCam directly rendered a bright vertical band that
        // tracked head yaw.
        if (!worldYaw) {
            worldToCam->PreMultiplyRotation(headRotView);
        }

        if (hasPosition) {
            NiPoint3* niCamWorldPos = reinterpret_cast<NiPoint3*>(
                niCamera + NiAVObjectOffsets::WorldTransform + NiAVObjectOffsets::WorldTranslateDelta);
            niCamWorldPos->x += worldOffset.x;
            niCamWorldPos->y += worldOffset.y;
            niCamWorldPos->z += worldOffset.z;

            worldToCam->entry[0][3] -= worldOffset.x;
            worldToCam->entry[1][3] -= worldOffset.z;
            worldToCam->entry[2][3] -= worldOffset.y;
        }

        // Snapshot the TRACKED worldToCam matrix after all rotation + position
        // mods are applied, so the player-update wrap has a tracked copy to
        // re-install for rendering after restore-clean.
        std::memcpy(snapshot.trackedWorldToCam, worldToCam->entry, sizeof(snapshot.trackedWorldToCam));

        // Publish the fully-built snapshot in one seqlock-guarded write.
        PublishSnapshot(snapshot);

    } __except (EXCEPTION_EXECUTE_HANDLER) {
        const DWORD code = GetExceptionCode();
        static std::atomic<uint64_t> s_exceptionCount{0};
        static std::atomic<DWORD> s_lastCode{0};
        const uint64_t n = s_exceptionCount.fetch_add(1, std::memory_order_relaxed) + 1;
        const DWORD prev = s_lastCode.exchange(code, std::memory_order_relaxed);
        // Log first hit, then every power of two, and on every code change.
        // Repeated AVs from a stable engine layout break flood the log at
        // frame rate otherwise; a code change signals a new failure mode.
        const bool isPow2 = (n & (n - 1)) == 0;
        if (n == 1 || isPow2 || prev != code) {
            Logger::Instance().Warning(
                "Exception in camera hook (code=0x%08X, total=%llu) - skipping frame",
                code, static_cast<unsigned long long>(n));
        }
        PublishSnapshot(CameraRootSnapshots{});
    }
}

static constexpr int VTABLE_UPDATE_INDEX = 2;

bool InstallCameraHook() {
    Logger::Instance().Info("Installing camera hook...");

    if (!GameState::Initialize()) {
        Logger::Instance().Warning("Game state detection init failed");
    }

    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) {
        Logger::Instance().Error("Failed to get game module handle");
        return false;
    }

    MODULEINFO modInfo = {};
    if (!GetModuleInformation(GetCurrentProcess(), gameModule, &modInfo, sizeof(modInfo))) {
        Logger::Instance().Error("GetModuleInformation failed: %lu", GetLastError());
        return false;
    }
    const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(gameModule);
    const size_t moduleSize = modInfo.SizeOfImage;

    uintptr_t vtable = FindVtableByRTTI(moduleBase, moduleSize, ".?AVPlayerCamera@@");
    if (vtable == 0) {
        Logger::Instance().Warning("PlayerCamera RTTI not found, trying TESCamera...");
        vtable = FindVtableByRTTI(moduleBase, moduleSize, ".?AVTESCamera@@");
    }
    if (vtable == 0) {
        Logger::Instance().Error("Could not find camera vtable via RTTI");
        return false;
    }

    uintptr_t updateFunc = *reinterpret_cast<uintptr_t*>(vtable + VTABLE_UPDATE_INDEX * 8);
    if (updateFunc == 0 || updateFunc < moduleBase || updateFunc >= moduleBase + moduleSize) {
        Logger::Instance().Error("Invalid Update function pointer: 0x%llX", updateFunc);
        return false;
    }

    Logger::Instance().Info("PlayerCamera::Update at: 0x%llX (offset: 0x%llX)",
                            updateFunc, updateFunc - moduleBase);

    g_hookedAddress = reinterpret_cast<void*>(updateFunc);
    MH_STATUS status = MH_CreateHook(
        g_hookedAddress,
        reinterpret_cast<LPVOID>(&PlayerCameraUpdateHook),
        reinterpret_cast<LPVOID*>(&g_originalUpdate)
    );

    if (status != MH_OK) {
        Logger::Instance().Error("MH_CreateHook failed: %d", static_cast<int>(status));
        g_hookedAddress = nullptr;
        return false;
    }

    Logger::Instance().Info("Camera hook installed successfully");
    return true;
}

void RemoveCameraHook() {
    if (g_hookedAddress) {
        MH_DisableHook(g_hookedAddress);
        MH_RemoveHook(g_hookedAddress);
        g_hookedAddress = nullptr;
        g_originalUpdate = nullptr;
        Logger::Instance().Info("Camera hook removed");
    }
}

void SetCameraHookEnabled(bool enabled) {
    g_hookEnabled.store(enabled, std::memory_order_relaxed);
}

} // namespace SkyrimHT
