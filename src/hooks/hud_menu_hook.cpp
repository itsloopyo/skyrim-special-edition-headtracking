#include "pch.h"
#include "hud_menu_hook.h"
#include "hooks/camera_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "core/constants.h"
#include "core/rtti_utils.h"
#include "hooks/gfx_value.h"
#include <MinHook.h>
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

std::atomic<float> g_aimDxFromCenterPx{0.0f};
std::atomic<float> g_aimDyFromCenterPx{0.0f};
std::atomic<float> g_screenWidthPx{0.0f};
std::atomic<float> g_screenHeightPx{0.0f};

std::atomic<bool> g_baselineCaptured{false};
float g_crosshairBaselineX = 0.0f;
float g_crosshairBaselineY = 0.0f;
float g_stageAuthoredWidth  = 0.0f;
float g_stageAuthoredHeight = 0.0f;

void RefreshAimOffsetFromCameraSnapshot(const CameraRootSnapshots& snap, bool snapValid,
                                         float screenW, float screenH) {
    float dxPx = 0.0f;
    float dyPx = 0.0f;
    if (!snapValid || snap.niCamera == 0
        || !ProjectBodyAimToScreenPixels(snap, screenW, screenH, dxPx, dyPx)) {
        g_aimDxFromCenterPx.store(0.0f, std::memory_order_relaxed);
        g_aimDyFromCenterPx.store(0.0f, std::memory_order_relaxed);
        return;
    }
    g_aimDxFromCenterPx.store(dxPx, std::memory_order_relaxed);
    g_aimDyFromCenterPx.store(dyPx, std::memory_order_relaxed);
}

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

void UpdateNativeCrosshairPerFrame(void* movieView,
                                   const CameraRootSnapshots& snap, bool snapValid) {
    if (!movieView) return;
    // When tracking is toggled off, don't keep writing the crosshair every
    // frame - leave Scaleform's vanilla state untouched so a "toggle off"
    // means truly "behave like vanilla". The baseline capture still happens
    // once on first HUD frame so the values are ready when re-enabled.
    CaptureBaselineOnce(movieView);
    if (!Mod::Instance().IsEnabled()) return;
    if (!g_baselineCaptured.load(std::memory_order_acquire)) return;

    const float screenW = g_screenWidthPx.load(std::memory_order_relaxed);
    const float screenH = g_screenHeightPx.load(std::memory_order_relaxed);
    if (screenW < 1.0f || screenH < 1.0f) return;  // overlay hasn't reported yet
    RefreshAimOffsetFromCameraSnapshot(snap, snapValid, screenW, screenH);

    const float dxPx = g_aimDxFromCenterPx.load(std::memory_order_relaxed);
    const float dyPx = g_aimDyFromCenterPx.load(std::memory_order_relaxed);

    // Skyrim renders the SWF stretched from its authored stage size (Stage.width/height,
    // typically 1280x720) to fill the screen, so 1 stage unit = screen/authored pixels.
    // Inverse for the screen-pixel -> stage-unit conversion.
    const float stagesPerPixelX = g_stageAuthoredWidth  / screenW;
    const float stagesPerPixelY = g_stageAuthoredHeight / screenH;
    const float crosshairX = g_crosshairBaselineX + dxPx * stagesPerPixelX;
    const float crosshairY = g_crosshairBaselineY + dyPx * stagesPerPixelY;

    CallSetVariableNumber(movieView, "HUDMovieBaseInstance.Crosshair._x", crosshairX);
    CallSetVariableNumber(movieView, "HUDMovieBaseInstance.Crosshair._y", crosshairY);
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
    // The activate-glyph TextField, discovered in the decompiled hudmenu.swf
    // as "RolloverButton_tf" (referenced near RefreshActivateButtonArt). Same
    // compensation pattern as RolloverText - read current, undo last delta,
    // write base + new delta.
    {
        "HUDMovieBaseInstance.RolloverButton_tf._x",
        "HUDMovieBaseInstance.RolloverButton_tf._y",
        false, false, 0.0, 0.0,
    },
};

// Floating quest marker: engine projects it through the CLEAN niCamera basis
// (restored by player_hook during PlayerCharacter::Update). To make it ride
// with the tracked view, we recover the marker's world direction from its
// stage position using the clean basis, then re-project that direction
// through the tracked basis and write back the new stage position. Exact for
// any marker position; screen-anchored aim-offset would only be exact for
// markers on the body-aim axis and degrades with off-axis angle.
// We compensate the floating marker per HUD frame by reading the engine's
// clean stage projection and reprojecting through the tracked basis. The
// engine writes the marker irregularly (not necessarily every frame), so we
// have to distinguish "engine just rewrote this" from "marker still holds
// our last write" - otherwise reprojecting our own writes compounds into a
// spiral. The trick: remember what we wrote. If the current value matches
// (within epsilon), engine didn't touch it - reuse our remembered base. If
// it differs, the engine wrote fresh - adopt the new value as base.
struct FloatingMarkerPathState {
    const char*  xPath;
    const char*  yPath;
    bool         hasBase;
    double       baseX;
    double       baseY;
    double       lastWrittenX;
    double       lastWrittenY;
};

FloatingMarkerPathState g_floatingMarkerPaths[] = {
    {
        "HUDMovieBaseInstance.FloatingQuestMarker_mc._x",
        "HUDMovieBaseInstance.FloatingQuestMarker_mc._y",
        false, 0.0, 0.0, 0.0, 0.0,
    },
};

bool ReprojectFloatingMarker(
    void* movieView,
    const CameraRootSnapshots& snap,
    FloatingMarkerPathState& state,
    float screenW,
    float screenH) {
    if (snap.niCamera == 0) return false;
    if (snap.frustumRight <= 0.0f || snap.frustumTop <= 0.0f) return false;

    double currentX = 0.0;
    double currentY = 0.0;
    if (!CallGetVariableNumber(movieView, state.xPath, currentX)) return false;
    if (!CallGetVariableNumber(movieView, state.yPath, currentY)) return false;

    // Engine wrote a fresh value iff the current value differs from what we
    // last wrote. Epsilon has to be loose enough to swallow Scaleform's
    // internal twip rounding / tween interpolation of our writes - if
    // round-trip jitter trips this, we falsely adopt our own (already-
    // projected) value as a new base and spiral. 5 stage units is ~7 screen
    // pixels; engine quest-marker updates are tens or hundreds of units.
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

    const double stagesPerPixelX = static_cast<double>(g_stageAuthoredWidth)  / screenW;
    const double stagesPerPixelY = static_cast<double>(g_stageAuthoredHeight) / screenH;

    // Stage-to-NDC assuming the marker container is anchored at stage centre
    // (HUDMovieBaseInstance has its origin offset to stage centre - confirmed
    // empirically by RolloverText sliding correctly with the same convention).
    const double cleanPxX = baseX / stagesPerPixelX;
    const double cleanPxY = baseY / stagesPerPixelY;
    const double cleanRight = (cleanPxX / (screenW * 0.5)) * snap.frustumRight;
    const double cleanUp    = -(cleanPxY / (screenH * 0.5)) * snap.frustumTop;

    // World direction = clean_forward + clean_up * cleanUp + clean_right * cleanRight
    const double wx = snap.cleanNiCamWorld[0][0]
                    + snap.cleanNiCamWorld[0][1] * cleanUp
                    + snap.cleanNiCamWorld[0][2] * cleanRight;
    const double wy = snap.cleanNiCamWorld[1][0]
                    + snap.cleanNiCamWorld[1][1] * cleanUp
                    + snap.cleanNiCamWorld[1][2] * cleanRight;
    const double wz = snap.cleanNiCamWorld[2][0]
                    + snap.cleanNiCamWorld[2][1] * cleanUp
                    + snap.cleanNiCamWorld[2][2] * cleanRight;

    // Project through the full tracked basis (roll included). Off-centre NPCs
    // are rotated around screen centre by the renderer's rolled basis, so the
    // marker must also rotate around screen centre to follow them. For a
    // centred NPC the rotation is a no-op (centred points don't move under
    // rotation around centre), so this is correct in both cases - any
    // perceived "drift" from a non-tilted seated POV is the marker correctly
    // tracking the world-anchored point through the rolled rendering.
    const double trackedFwd   = snap.trackedNiCamWorld[0][0]*wx + snap.trackedNiCamWorld[1][0]*wy + snap.trackedNiCamWorld[2][0]*wz;
    const double trackedUp    = snap.trackedNiCamWorld[0][1]*wx + snap.trackedNiCamWorld[1][1]*wy + snap.trackedNiCamWorld[2][1]*wz;
    const double trackedRight = snap.trackedNiCamWorld[0][2]*wx + snap.trackedNiCamWorld[1][2]*wy + snap.trackedNiCamWorld[2][2]*wz;

    if (trackedFwd < 0.01) return false;  // behind tracked camera; leave alone

    const double ndcX = trackedRight / trackedFwd / snap.frustumRight;
    const double ndcY = -trackedUp   / trackedFwd / snap.frustumTop;
    const double targetX = ndcX * screenW * 0.5 * stagesPerPixelX;
    const double targetY = ndcY * screenH * 0.5 * stagesPerPixelY;

    if (!CallSetVariableNumber(movieView, state.xPath, targetX)) return false;
    if (!CallSetVariableNumber(movieView, state.yPath, targetY)) return false;
    state.lastWrittenX = targetX;
    state.lastWrittenY = targetY;
    return true;
}

void UpdateFloatingMarkerReprojection(void* movieView,
                                      const CameraRootSnapshots& snap, bool snapValid) {
    if (!movieView || !Mod::Instance().IsEnabled()) return;
    if (!snapValid) return;
    if (!g_baselineCaptured.load(std::memory_order_acquire)) return;
    const float screenW = g_screenWidthPx.load(std::memory_order_relaxed);
    const float screenH = g_screenHeightPx.load(std::memory_order_relaxed);
    if (screenW < 1.0f || screenH < 1.0f) return;

    // Same per-marker reprojection in both yaw modes. Engine writes the
    // marker once (when activated) at the projection through cleanNiCamWorld;
    // we hold onto that base via the engine-write detector and reproject
    // through the current tracked basis each frame so the marker follows the
    // NPC as the head rotates. The basis transform from cleanNiCamWorld to
    // trackedNiCamWorld is exact in both yaw modes (verified algebraically:
    // both yaw modes leave niCamera.world = cleanNiCamWorld * <mode-specific
    // composed rotation>, which is what we capture as trackedNiCamWorld).
    for (auto& path : g_floatingMarkerPaths) {
        ReprojectFloatingMarker(movieView, snap, path, screenW, screenH);
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

void UpdateScreenAnchoredCompensation(void* movieView) {
    if (!movieView || !Mod::Instance().IsEnabled()) return;
    if (!g_baselineCaptured.load(std::memory_order_acquire)) return;

    const float screenW = g_screenWidthPx.load(std::memory_order_relaxed);
    const float screenH = g_screenHeightPx.load(std::memory_order_relaxed);
    if (screenW < 1.0f || screenH < 1.0f) return;

    // Aim offset is refreshed once per HUD tick by UpdateNativeCrosshairPerFrame
    // earlier in HUDMenuAdvanceHook, so just read the published values here.
    const double stagesPerPixelX = static_cast<double>(g_stageAuthoredWidth)  / screenW;
    const double stagesPerPixelY = static_cast<double>(g_stageAuthoredHeight) / screenH;
    const double targetDx = static_cast<double>(g_aimDxFromCenterPx.load(std::memory_order_relaxed)) * stagesPerPixelX;
    const double targetDy = static_cast<double>(g_aimDyFromCenterPx.load(std::memory_order_relaxed)) * stagesPerPixelY;

    int shifted = 0;
    for (auto& path : g_screenAnchoredPaths) {
        shifted += CompensateScreenAnchoredPath(movieView, path, targetDx, targetDy) ? 1 : 0;
    }

    if (shifted == 0 && !g_loggedScreenAnchoredMiss.exchange(true, std::memory_order_acq_rel)) {
        Logger::Instance().Info("Screen-anchored compensation found no writable paths yet");
    }
}

typedef uint64_t (__fastcall *HUDMenuAdvance_t)(void* thisMenu, uint64_t a1, uint64_t a2, uint64_t a3);

HUDMenuAdvance_t g_originalAdvance = nullptr;
void* g_hookedAddress = nullptr;

uint64_t __fastcall HUDMenuAdvanceHook(void* thisMenu, uint64_t a1, uint64_t a2, uint64_t a3) {
    const uint64_t result = g_originalAdvance(thisMenu, a1, a2, a3);
    if (thisMenu) {
        void* movieView = reinterpret_cast<void*>(
            SafeReadQword(reinterpret_cast<uintptr_t>(thisMenu) + kIMenu_uiMovie_Offset));
        // Fetch the camera snapshot once and reuse across both passes. The
        // crosshair update and the marker compensation each previously called
        // GetCameraRootSnapshots independently, and the marker path called it
        // a third time inside its inner per-marker reprojection. Hoisting the
        // 368-byte seqlock copy here drops three per-HUD-frame copies to one.
        CameraRootSnapshots snap;
        const bool snapValid = GetCameraRootSnapshots(snap);
        UpdateNativeCrosshairPerFrame(movieView, snap, snapValid);
        UpdateScreenAnchoredCompensation(movieView);
        UpdateFloatingMarkerReprojection(movieView, snap, snapValid);
    }
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

void SetNativeCrosshairAimPixels(float dxFromCenterPx, float dyFromCenterPx,
                                 float screenWidthPx, float screenHeightPx) {
    g_aimDxFromCenterPx.store(dxFromCenterPx, std::memory_order_relaxed);
    g_aimDyFromCenterPx.store(dyFromCenterPx, std::memory_order_relaxed);
    g_screenWidthPx.store(screenWidthPx,   std::memory_order_relaxed);
    g_screenHeightPx.store(screenHeightPx, std::memory_order_relaxed);
}

} // namespace SkyrimHT
