#pragma once

#include "camera_hook.h"

namespace SkyrimHT {

inline bool ProjectFloatingMarker(
    const CameraRootSnapshots& snap,
    double stageWidth, double stageHeight,
    double originX, double originY,
    double baseX, double baseY,
    double& targetX, double& targetY) {
    if (snap.frustumRight <= 0.0f || snap.frustumTop <= 0.0f) return false;

    const double halfWidth = stageWidth * 0.5;
    const double halfHeight = stageHeight * 0.5;
    // Clip positions are relative to the HUD origin, which is not screen centre.
    const double cleanRight = ((baseX + originX - halfWidth) / halfWidth) * snap.frustumRight;
    const double cleanUp = -((baseY + originY - halfHeight) / halfHeight) * snap.frustumTop;
    const double wx = snap.cleanNiCamWorld[0][0] + snap.cleanNiCamWorld[0][1]*cleanUp + snap.cleanNiCamWorld[0][2]*cleanRight;
    const double wy = snap.cleanNiCamWorld[1][0] + snap.cleanNiCamWorld[1][1]*cleanUp + snap.cleanNiCamWorld[1][2]*cleanRight;
    const double wz = snap.cleanNiCamWorld[2][0] + snap.cleanNiCamWorld[2][1]*cleanUp + snap.cleanNiCamWorld[2][2]*cleanRight;

    const double forward = snap.trackedNiCamWorld[0][0]*wx + snap.trackedNiCamWorld[1][0]*wy + snap.trackedNiCamWorld[2][0]*wz;
    const double up = snap.trackedNiCamWorld[0][1]*wx + snap.trackedNiCamWorld[1][1]*wy + snap.trackedNiCamWorld[2][1]*wz;
    const double right = snap.trackedNiCamWorld[0][2]*wx + snap.trackedNiCamWorld[1][2]*wy + snap.trackedNiCamWorld[2][2]*wz;
    if (forward < 0.01) return false;

    targetX = (right / forward / snap.frustumRight + 1.0) * halfWidth - originX;
    targetY = (1.0 - up / forward / snap.frustumTop) * halfHeight - originY;
    return true;
}

}
