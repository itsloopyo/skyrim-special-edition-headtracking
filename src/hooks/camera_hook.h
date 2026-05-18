#pragma once

#include <cstdint>

namespace SkyrimHT {

// Discover the PlayerCamera (or TESCamera) vtable via RTTI and hook its
// Update slot (index 2). Synchronous - returns false if RTTI discovery fails.
bool InstallCameraHook();

// Disable and unregister the MinHook for PlayerCamera::Update.
void RemoveCameraHook();

// Toggle whether the hook's tracking code runs. The hook itself stays installed;
// only the per-frame tracking application is gated. Safe to call at any time.
void SetCameraHookEnabled(bool enabled);

// Camera-root rotation snapshots captured each frame before/after head
// tracking is applied. The PlayerCharacter::Update save/restore wrapper uses
// these to make game logic observe the clean (body-aim) rotation while the
// renderer sees the head-tracked rotation.
struct CameraRootSnapshots {
    uintptr_t cameraRoot;        // NiNode*; 0 if no frame processed yet
    uintptr_t niCamera;          // NiCamera*; 0 if not captured
    // Each rotation matrix is 36 bytes (NiMatrix33). Copied by value.
    float cleanLocal[3][3];
    float cleanWorld[3][3];
    float trackedLocal[3][3];
    float trackedWorld[3][3];
    // niCamera world rotation snapshots so PlayerCharacter::Update wrap can
    // also save/restore it (activate-target raycast reads niCamera, not just
    // cameraRoot).
    float cleanNiCamWorld[3][3];
    float trackedNiCamWorld[3][3];
    // worldToCam (the renderer's view matrix) is also modified by head tracking;
    // save the full 4x4 so the player-update wrap can restore it. Snapshot is
    // valid iff niCamera != 0.
    float cleanWorldToCam[4][4];
    float trackedWorldToCam[4][4];
    // NiCamera view-frustum extents, normalised to near=1: frustumRight =
    // tan(HFOV/2), frustumTop = tan(VFOV/2). Lets the crosshair overlay project
    // body-aim to screen using the live FOV instead of guessing it.
    float frustumRight;
    float frustumTop;
};

// Returns false if no frame has been processed yet.
bool GetCameraRootSnapshots(CameraRootSnapshots& out);

// Project the body-aim direction (column 0 of cleanNiCamWorld) through the
// tracked NiCamera basis and frustum, returning the screen-pixel offset from
// the screen centre at which the body-aim direction would render under head
// tracking. Returns false (and leaves outputs unchanged) when the projection
// is degenerate - aim has rolled behind the camera, or the frustum hasn't
// been published yet. `snap` must have been filled from GetCameraRootSnapshots
// (snap.niCamera != 0).
bool ProjectBodyAimToScreenPixels(
    const CameraRootSnapshots& snap,
    float screenWidthPx,
    float screenHeightPx,
    float& outDxPx,
    float& outDyPx);

// Current 6DOF lean offset in WORLD coordinates (game units), as applied to
// niCamera's world translate this frame. The projectile hook reads this to
// translate the arrow's launch origin by the same amount so the arrow flies
// from the leaned camera position - keeping arrow and reticle in sync when
// the player slides side-to-side in their seat. Returns false if no lean
// has been applied (no 6DOF data this frame).
bool GetCurrentLeanOffset(float& x, float& y, float& z);

} // namespace SkyrimHT
