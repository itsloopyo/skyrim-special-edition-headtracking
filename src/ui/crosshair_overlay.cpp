#include "pch.h"
#include "crosshair_overlay.h"
#include "core/mod.h"
#include "core/logger.h"
#include "game/game_state.h"
#include "hooks/camera_hook.h"
#include "hooks/crosshair_override.h"
#include "hooks/hud_menu_hook.h"

#define CAMERAUNLOCK_DX11_OVERLAY_IMPLEMENTATION
#include <cameraunlock/rendering/dx11_overlay.h>

namespace SkyrimHT {

namespace {

cameraunlock::rendering::DX11Overlay g_overlay;

void OverlayLog(const char* msg) {
    Logger::Instance().Info("%s", msg);
}

void RenderCrosshair(cameraunlock::rendering::DX11DrawContext& dc) {
    Mod& mod = Mod::Instance();
    if (!mod.IsEnabled()) return;
    if (!GameState::IsInGameplay()) return;

    const float screenW = static_cast<float>(dc.Width());
    const float screenH = static_cast<float>(dc.Height());

    // Project the true body-aim direction into the head-tracked view using the
    // camera hook's own snapshot matrices - no Euler re-derivation, so this
    // can't drift out of sync with whatever composition camera_hook used, and
    // it needs no world-yaw vs camera-local branch.
    //
    // cleanNiCamWorld col 0 is the body-aim forward direction in world space
    // (same source the activate-ray override uses). trackedNiCamWorld is the
    // head-tracked camera basis the renderer draws with; its columns are
    // 0=forward, 1=up, 2=right in world space. Dotting the world aim against
    // those columns expresses it in the tracked camera's local frame.
    CameraRootSnapshots snap;
    if (!GetCameraRootSnapshots(snap) || snap.niCamera == 0) {
        SetNativeCrosshairAimPixels(0.0f, 0.0f, screenW, screenH);
        return;
    }

    // Reticle position = body-aim DIRECTION projected through the tracked
    // camera ROTATION. We deliberately ignore the tracked camera's position
    // (lean translation) so the reticle's screen position is invariant under
    // lean. The arrow's launch origin also doesn't move with head lean (the
    // bow is attached to the un-leaned player body), so this keeps reticle
    // and arrow in sync under lean - they both point along the same body-aim
    // direction line, regardless of how the rendering camera is offset.
    //
    // Trade-off vs the previous world-hit-point projection: we lose visual
    // "lock-on" to pickable targets (the reticle no longer chases a specific
    // NPC under lean parallax). What we gain is the reticle agreeing with
    // where the arrow actually goes, which is the contract that matters for
    // combat.
    float dxPx = 0.0f;
    float dyPx = 0.0f;
    if (!ProjectBodyAimToScreenPixels(snap, screenW, screenH, dxPx, dyPx)) {
        SetNativeCrosshairAimPixels(0.0f, 0.0f, screenW, screenH);
        return;
    }

    SetNativeCrosshairAimPixels(dxPx, dyPx, screenW, screenH);
}

} // namespace

bool InitializeCrosshairOverlay() {
    cameraunlock::rendering::SetDX11OverlayLogger(&OverlayLog);
    g_overlay.SetRenderCallback(&RenderCrosshair);
    if (!g_overlay.Install()) {
        Logger::Instance().Error("DX11Overlay::Install failed (Present hook unavailable)");
        return false;
    }
    Logger::Instance().Info("Crosshair overlay installed");
    return true;
}

void ShutdownCrosshairOverlay() {
    if (g_overlay.IsInstalled()) {
        g_overlay.Remove();
        Logger::Instance().Info("Crosshair overlay removed");
    }
}

} // namespace SkyrimHT
