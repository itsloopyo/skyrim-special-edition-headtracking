#include "pch.h"
#include "hud_menu_hook.h"
#include "hooks/camera_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "core/constants.h"
#include "core/rtti_utils.h"
#include "hooks/gfx_value.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cmath>

namespace SkyrimHT {

namespace {

enum class GFxSetVarType : uint32_t {
    kSticky    = 0,
    kPermanent = 1,
};

typedef bool (__fastcall *GFxMovieView_SetVariable_t)(
    void* thisMovie, const char* pathToVar, const GFxValue* value, GFxSetVarType setType);
typedef bool (__fastcall *GFxMovieView_GetVariable_t)(
    void* thisMovie, GFxValue* outValue, const char* pathToVar);

constexpr int kGFxMovieView_SetVariable_VtableIndex = 16;
constexpr int kGFxMovieView_GetVariable_VtableIndex = 17;
constexpr uintptr_t kIMenu_uiMovie_Offset = 0x10;

// HUDMenu vtable slot that fires once per frame while the HUD is up. Found by
// probing candidate slots {4,5,7,9} against a HW write breakpoint; slot 4 is
// the per-frame advance and the only one we drive the crosshair from.
constexpr int kHUDMenuAdvanceVtableIndex = 4;

bool CallSetVariableNumber(void* movieView, const char* path, double number) {
    if (!movieView) return false;
    const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(movieView);
    auto setVar = reinterpret_cast<GFxMovieView_SetVariable_t>(
        *reinterpret_cast<uintptr_t*>(vtable + kGFxMovieView_SetVariable_VtableIndex * 8));

    GFxValue v{};
    v.type = GFxValueType::kNumber;
    v.value.number = number;
    return setVar(movieView, path, &v, GFxSetVarType::kSticky);
}

bool CallGetVariableNumber(void* movieView, const char* path, double& outValue) {
    if (!movieView) return false;
    const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(movieView);
    auto getVar = reinterpret_cast<GFxMovieView_GetVariable_t>(
        *reinterpret_cast<uintptr_t*>(vtable + kGFxMovieView_GetVariable_VtableIndex * 8));
    GFxValue v{};
    if (!getVar(movieView, &v, path)) return false;
    if (v.type != GFxValueType::kNumber) return false;
    outValue = v.value.number;
    return true;
}

uintptr_t SafeReadQword(uintptr_t addr) {
    __try {
        return *reinterpret_cast<uintptr_t*>(addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

std::atomic<bool> g_baselineCaptured{false};
float g_crosshairBaselineX = 0.0f;
float g_crosshairBaselineY = 0.0f;
float g_stageAuthoredWidth  = 0.0f;
float g_stageAuthoredHeight = 0.0f;
bool  g_crosshairMoved = false;

void CaptureBaselineOnce(void* movieView) {
    // Relaxed-load fast path: once captured this is hit every HUD frame for the
    // rest of the session, so keep the locked compare-exchange RMW off the
    // steady-state path.
    if (g_baselineCaptured.load(std::memory_order_acquire)) return;

    double cx = 0.0, cy = 0.0, sw = 0.0, sh = 0.0;
    const bool okCX = CallGetVariableNumber(movieView, "HUDMovieBaseInstance.Crosshair._x", cx);
    const bool okCY = CallGetVariableNumber(movieView, "HUDMovieBaseInstance.Crosshair._y", cy);
    const bool okSW = CallGetVariableNumber(movieView, "Stage.width",  sw);
    const bool okSH = CallGetVariableNumber(movieView, "Stage.height", sh);

    if (!okCX || !okCY || !okSW || !okSH || sw < 1.0 || sh < 1.0) {
        // Don't publish the flag - leave g_baselineCaptured false so the next
        // frame retries. Previously this set the flag true via CAS, then reset
        // it to false on failure, which let other threads briefly observe a
        // captured state with uninitialised baseline values.
        Logger::Instance().Error("Native crosshair baseline capture failed (cx=%d cy=%d sw=%d sh=%d)",
                                  okCX, okCY, okSW, okSH);
        return;
    }

    // Write the baseline values first, then publish the flag with release
    // ordering so a reader doing acquire-load sees fully-initialised fields.
    g_crosshairBaselineX  = static_cast<float>(cx);
    g_crosshairBaselineY  = static_cast<float>(cy);
    g_stageAuthoredWidth  = static_cast<float>(sw);
    g_stageAuthoredHeight = static_cast<float>(sh);
    g_baselineCaptured.store(true, std::memory_order_release);
    Logger::Instance().Info("Native crosshair baseline: vanilla=(%.2f, %.2f), authored stage=%.0fx%.0f",
                            g_crosshairBaselineX, g_crosshairBaselineY,
                            g_stageAuthoredWidth, g_stageAuthoredHeight);
}

// Skyrim stretches the SWF from its authored stage size (Stage.width/height,
// typically 1280x720) to fill the screen, so projecting through the authored
// size gives the aim offset in stage units directly. Zero while tracking is not
// applied, or when the aim has rolled behind the tracked camera.
void AimOffsetStageUnits(const CameraRootSnapshots& snap, bool tracking, double& dx, double& dy) {
    dx = 0.0;
    dy = 0.0;
    float fx = 0.0f;
    float fy = 0.0f;
    if (tracking && ProjectBodyAimToScreen(snap, g_stageAuthoredWidth, g_stageAuthoredHeight, fx, fy)) {
        dx = fx;
        dy = fy;
    }
}

// A position written to the crosshair stays until something writes another, so
// stopping the per-frame writes leaves it wherever the last one put it. Once
// tracking stops it is put back on the vanilla position once and then left alone.
void UpdateNativeCrosshair(void* movieView, bool tracking, double dx, double dy) {
    if (!tracking && !g_crosshairMoved) return;
    CallSetVariableNumber(movieView, "HUDMovieBaseInstance.Crosshair._x", g_crosshairBaselineX + dx);
    CallSetVariableNumber(movieView, "HUDMovieBaseInstance.Crosshair._y", g_crosshairBaselineY + dy);
    g_crosshairMoved = tracking;
}

// Screen-anchored HUD elements that sit at a fixed offset from the (centred)
// crosshair when tracking is off. Each frame we read the current _x/_y, undo
// the offset we wrote last frame to recover the engine's intended baseline,
// then write base + aim-offset so the element follows the compensated reticle.
// Pattern matches CompensateFloatingMarkerPath but uses the simple aim-offset
// (no world reprojection) because these elements track the screen-space
// crosshair, not a world point.
struct ScreenAnchoredPathState {
    const char* xPath;
    const char* yPath;
    bool        hasLastOffset;
    bool        logged;
    double      lastDx;
    double      lastDy;
};

ScreenAnchoredPathState g_screenAnchoredPaths[] = {
    {
        "HUDMovieBaseInstance.RolloverText._x",
        "HUDMovieBaseInstance.RolloverText._y",
        false, false, 0.0, 0.0,
    },
    // The activate-glyph TextField is named "RolloverButton_tf" in hudmenu.swf,
    // referenced near RefreshActivateButtonArt. Same
    // compensation pattern as RolloverText - read current, undo last delta,
    // write base + new delta.
    {
        "HUDMovieBaseInstance.RolloverButton_tf._x",
        "HUDMovieBaseInstance.RolloverButton_tf._y",
        false, false, 0.0, 0.0,
    },
};

// Floating quest markers. Ghidra (FUN_140923800 on 1.6.1170) showed the engine
// attaches up to 48 marker clips named target0..target47 (class
// "quest target_HUD") under FloatingQuestMarkerInstance, and positions each at
// the quest target's CLEAN screen projection. Verified at runtime: target0._x/._y
// holds the real off-centre position and does NOT move with the head (the engine
// projects with the clean, body-locked camera; player_hook restores it during
// Update). So we reproject the REAL marker exactly: read its clean stage
// position, recover the world direction through the clean camera basis, then
// re-project that direction through the tracked basis and write it back.
//
// This is exact for every marker on all axes (yaw/pitch/roll, off-centre) in
// BOTH yaw modes - no per-axis approximation, no yaw-mode special-casing - and
// is version-independent (pure GFx member paths, no hardcoded RVAs).
//
// Inactive markers sit parked at the origin; we skip those. The engine rewrites
// each active marker every frame at its clean projection, so the engine-write
// detector distinguishes "engine refreshed the clean value" from "our reprojected
// write still stands", preventing us from reprojecting our own output (a spiral).
constexpr int kMaxFloatingMarkers = 12;

struct FloatingMarkerPaths {
    char x[80];
    char y[80];
};

const std::array<FloatingMarkerPaths, kMaxFloatingMarkers>& GetFloatingMarkerPaths() {
    static const std::array<FloatingMarkerPaths, kMaxFloatingMarkers> paths = [] {
        std::array<FloatingMarkerPaths, kMaxFloatingMarkers> p{};
        for (int i = 0; i < kMaxFloatingMarkers; ++i) {
            snprintf(p[i].x, sizeof(p[i].x), "HUDMovieBaseInstance.FloatingQuestMarkerInstance.target%d._x", i);
            snprintf(p[i].y, sizeof(p[i].y), "HUDMovieBaseInstance.FloatingQuestMarkerInstance.target%d._y", i);
        }
        return p;
    }();
    return paths;
}

struct FloatingMarkerState {
    bool   hasBase;
    double baseX;
    double baseY;
    double lastWrittenX;
    double lastWrittenY;
};
FloatingMarkerState g_floatingMarkers[kMaxFloatingMarkers] = {};

bool ReprojectFloatingMarker(
    void* movieView,
    const CameraRootSnapshots& snap,
    const FloatingMarkerPaths& paths,
    FloatingMarkerState& state) {
    const char* xPath = paths.x;
    const char* yPath = paths.y;

    double currentX = 0.0;
    double currentY = 0.0;
    if (!CallGetVariableNumber(movieView, xPath, currentX)) return false;  // clip absent
    if (!CallGetVariableNumber(movieView, yPath, currentY)) return false;

    // Engine refreshed the clean value iff it differs from what we last wrote.
    // Loose epsilon swallows Scaleform twip rounding of our own writes; engine
    // marker moves are tens-to-hundreds of stage units.
    constexpr double kEngineWriteEpsilon = 5.0;
    const bool engineRewrote = !state.hasBase
        || std::abs(currentX - state.lastWrittenX) > kEngineWriteEpsilon
        || std::abs(currentY - state.lastWrittenY) > kEngineWriteEpsilon;
    if (engineRewrote) {
        state.baseX = currentX;
        state.baseY = currentY;
        state.hasBase = true;
    }
    const double baseX = state.baseX;
    const double baseY = state.baseY;

    // Inactive marker parked at the origin - leave it and reset so a future
    // activation re-bases cleanly.
    if (std::abs(baseX) < 0.5 && std::abs(baseY) < 0.5) {
        state.hasBase = false;
        return false;
    }

    const double halfStageW = static_cast<double>(g_stageAuthoredWidth)  * 0.5;
    const double halfStageH = static_cast<double>(g_stageAuthoredHeight) * 0.5;

    const double cleanRight =  (baseX / halfStageW) * snap.frustumRight;
    const double cleanUp    = -(baseY / halfStageH) * snap.frustumTop;

    // Reconstruct the world direction from the marker's clean stage position,
    // then re-project through the tracked basis.
    const double wx = snap.cleanNiCamWorld[0][0] + snap.cleanNiCamWorld[0][1]*cleanUp + snap.cleanNiCamWorld[0][2]*cleanRight;
    const double wy = snap.cleanNiCamWorld[1][0] + snap.cleanNiCamWorld[1][1]*cleanUp + snap.cleanNiCamWorld[1][2]*cleanRight;
    const double wz = snap.cleanNiCamWorld[2][0] + snap.cleanNiCamWorld[2][1]*cleanUp + snap.cleanNiCamWorld[2][2]*cleanRight;

    const double trackedFwd   = snap.trackedNiCamWorld[0][0]*wx + snap.trackedNiCamWorld[1][0]*wy + snap.trackedNiCamWorld[2][0]*wz;
    const double trackedUp    = snap.trackedNiCamWorld[0][1]*wx + snap.trackedNiCamWorld[1][1]*wy + snap.trackedNiCamWorld[2][1]*wz;
    const double trackedRight = snap.trackedNiCamWorld[0][2]*wx + snap.trackedNiCamWorld[1][2]*wy + snap.trackedNiCamWorld[2][2]*wz;

    if (trackedFwd < 0.01) return false;  // behind tracked camera; leave alone

    const double ndcX = trackedRight / trackedFwd / snap.frustumRight;
    const double ndcY = -trackedUp   / trackedFwd / snap.frustumTop;
    const double targetX = ndcX * halfStageW;
    const double targetY = ndcY * halfStageH;

    if (!CallSetVariableNumber(movieView, xPath, targetX)) return false;
    if (!CallSetVariableNumber(movieView, yPath, targetY)) return false;
    state.lastWrittenX = targetX;
    state.lastWrittenY = targetY;
    return true;
}

void UpdateFloatingMarkerReprojection(void* movieView, const CameraRootSnapshots& snap) {
    if (snap.frustumRight <= 0.0f || snap.frustumTop <= 0.0f) return;
    const auto& paths = GetFloatingMarkerPaths();
    for (int i = 0; i < kMaxFloatingMarkers; ++i) {
        ReprojectFloatingMarker(movieView, snap, paths[i], g_floatingMarkers[i]);
    }
}

std::atomic<bool> g_loggedScreenAnchoredMiss{false};

bool CompensateScreenAnchoredPath(
    void* movieView,
    ScreenAnchoredPathState& state,
    double targetDx,
    double targetDy) {
    double currentX = 0.0;
    double currentY = 0.0;
    if (!CallGetVariableNumber(movieView, state.xPath, currentX)) return false;
    if (!CallGetVariableNumber(movieView, state.yPath, currentY)) return false;

    const double baseX = state.hasLastOffset ? currentX - state.lastDx : currentX;
    const double baseY = state.hasLastOffset ? currentY - state.lastDy : currentY;
    const bool okX = CallSetVariableNumber(movieView, state.xPath, baseX + targetDx);
    const bool okY = CallSetVariableNumber(movieView, state.yPath, baseY + targetDy);
    if (!okX || !okY) return false;

    state.lastDx = targetDx;
    state.lastDy = targetDy;
    state.hasLastOffset = true;
    if (!state.logged) {
        state.logged = true;
        Logger::Instance().Info("Screen-anchored compensation captured %s base=(%.2f, %.2f)",
                                state.xPath, baseX, baseY);
    }
    return true;
}

// Once tracking stops, one pass with a zero offset takes back the last one we
// added, then the paths are left to the game.
void UpdateScreenAnchoredCompensation(void* movieView, bool tracking, double dx, double dy) {
    int shifted = 0;
    for (auto& path : g_screenAnchoredPaths) {
        if (!tracking) {
            if (path.hasLastOffset) CompensateScreenAnchoredPath(movieView, path, 0.0, 0.0);
            path.hasLastOffset = false;
            continue;
        }
        shifted += CompensateScreenAnchoredPath(movieView, path, dx, dy) ? 1 : 0;
    }

    if (tracking && shifted == 0 && !g_loggedScreenAnchoredMiss.exchange(true, std::memory_order_acq_rel)) {
        Logger::Instance().Info("Screen-anchored compensation found no writable paths yet");
    }
}

typedef uint64_t (__fastcall *HUDMenuAdvance_t)(void* thisMenu, uint64_t a1, uint64_t a2, uint64_t a3);

HUDMenuAdvance_t g_originalAdvance = nullptr;
void* g_hookedAddress = nullptr;

uint64_t __fastcall HUDMenuAdvanceHook(void* thisMenu, uint64_t a1, uint64_t a2, uint64_t a3) {
    const uint64_t result = g_originalAdvance(thisMenu, a1, a2, a3);
    if (!thisMenu) return result;
    void* movieView = reinterpret_cast<void*>(
        SafeReadQword(reinterpret_cast<uintptr_t>(thisMenu) + kIMenu_uiMovie_Offset));
    if (!movieView) return result;

    // Captured whether or not tracking is on, so the baseline is ready the
    // moment it is switched on.
    CaptureBaselineOnce(movieView);
    if (!g_baselineCaptured.load(std::memory_order_acquire)) return result;

    // The camera hook publishes an empty snapshot for every frame it leaves the
    // camera clean (toggled off, menu, alt-tab), so this is false on exactly
    // the frames the view is untracked.
    CameraRootSnapshots snap;
    const bool tracking = Mod::Instance().IsEnabled() && GetCameraRootSnapshots(snap) && snap.niCamera != 0;

    double dx = 0.0;
    double dy = 0.0;
    AimOffsetStageUnits(snap, tracking, dx, dy);
    UpdateNativeCrosshair(movieView, tracking, dx, dy);
    UpdateScreenAnchoredCompensation(movieView, tracking, dx, dy);
    if (tracking) UpdateFloatingMarkerReprojection(movieView, snap);
    return result;
}

} // namespace

bool InstallHUDMenuHook() {
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);
    if (!gameModule) {
        Logger::Instance().Error("HUDMenu hook: game module handle null");
        return false;
    }

    MODULEINFO modInfo = {};
    if (!GetModuleInformation(GetCurrentProcess(), gameModule, &modInfo, sizeof(modInfo))) {
        Logger::Instance().Error("HUDMenu hook: GetModuleInformation failed: %lu", GetLastError());
        return false;
    }
    const uintptr_t moduleBase = reinterpret_cast<uintptr_t>(gameModule);
    const size_t moduleSize = modInfo.SizeOfImage;

    const uintptr_t vtable = FindVtableByRTTI(moduleBase, moduleSize, ".?AVHUDMenu@@");
    if (vtable == 0) {
        Logger::Instance().Error("HUDMenu hook: RTTI vtable lookup failed");
        return false;
    }
    Logger::Instance().Info("HUDMenu vtable at: 0x%llX (RVA 0x%llX)", vtable, vtable - moduleBase);

    const uintptr_t target = *reinterpret_cast<uintptr_t*>(vtable + kHUDMenuAdvanceVtableIndex * 8);
    if (target < moduleBase || target >= moduleBase + moduleSize) {
        Logger::Instance().Error("HUDMenu hook: vtable[%d] out of module: 0x%llX",
                                  kHUDMenuAdvanceVtableIndex, target);
        return false;
    }

    g_hookedAddress = reinterpret_cast<void*>(target);
    MH_STATUS status = MH_CreateHook(
        g_hookedAddress,
        reinterpret_cast<LPVOID>(&HUDMenuAdvanceHook),
        reinterpret_cast<LPVOID*>(&g_originalAdvance));
    if (status != MH_OK) {
        Logger::Instance().Error("HUDMenu hook: MH_CreateHook(vtable[%d]) failed: %d",
                                  kHUDMenuAdvanceVtableIndex, static_cast<int>(status));
        g_hookedAddress = nullptr;
        return false;
    }

    Logger::Instance().Info("HUDMenu hook installed on vtable[%d] at 0x%llX (RVA 0x%llX)",
                            kHUDMenuAdvanceVtableIndex, target, target - moduleBase);
    return true;
}

void RemoveHUDMenuHook() {
    if (g_hookedAddress) {
        MH_DisableHook(g_hookedAddress);
        MH_RemoveHook(g_hookedAddress);
        g_hookedAddress = nullptr;
        g_originalAdvance = nullptr;
    }
    Logger::Instance().Info("HUDMenu hook removed");
}

} // namespace SkyrimHT
