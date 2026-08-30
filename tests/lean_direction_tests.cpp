// Behaviour lock for the 6DOF lean boundary: which way the camera moves for a
// physical lean, and how much of the asymmetric budget each direction gets.
//
// The bug this guards against shipped once: Config carried InvertZ=true, which
// the processor applies BEFORE its [-LimitZ, +LimitZBack] clamp, so a forward
// lean was cut off at the 0.10m backward allowance and a backward lean was
// handed the 0.40m forward one. Nothing in the render path notices; the camera
// just refuses to lean in.

#include <cmath>
#include <cstdio>

#include "core/constants.h"
#include "core/config.h"
#include "hooks/camera_boundary.h"

#include <cameraunlock/processing/position_processor.h>

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void CheckNear(float actual, float expected, const char* what) {
    if (std::fabs(actual - expected) <= 1e-3f) return;
    std::printf("FAIL: %s (expected %.6f, got %.6f)\n", what, expected, actual);
    ++g_failures;
}

// The processor's z runs negative for a forward lean, and depth drives the
// node's first component.
void ForwardLeanMovesCameraForward() {
    const SkyrimHT::NiPoint3 fwd = SkyrimHT::CameraLocalLeanOffset(0.0f, 0.0f, -0.25f);
    Check(fwd.x < 0.0f, "forward lean (processor z < 0) drives depth negative");

    const SkyrimHT::NiPoint3 back = SkyrimHT::CameraLocalLeanOffset(0.0f, 0.0f, 0.25f);
    Check(back.x > 0.0f, "backward lean (processor z > 0) drives depth positive");
}

void UpAndLateralMapStraightThrough() {
    const SkyrimHT::NiPoint3 up = SkyrimHT::CameraLocalLeanOffset(0.0f, 0.1f, 0.0f);
    CheckNear(up.y, 0.1f * SkyrimHT::UNITS_PER_METER, "up maps to the second component");

    const SkyrimHT::NiPoint3 right = SkyrimHT::CameraLocalLeanOffset(0.1f, 0.0f, 0.0f);
    CheckNear(right.z, 0.1f * SkyrimHT::UNITS_PER_METER, "lateral maps to the third component");
}

// Drives the real processor with the shipped defaults, so a reintroduced
// InvertZ or a swapped pair of limits fails here rather than in the game.
cameraunlock::PositionSettings ShippedSettings() {
    const SkyrimHT::Config config;
    cameraunlock::PositionSettings s;
    s.sensitivity_x = config.positionSensitivityX;
    s.sensitivity_y = config.positionSensitivityY;
    s.sensitivity_z = config.positionSensitivityZ;
    s.limit_x       = config.positionLimitX;
    s.limit_y       = config.positionLimitY;
    s.limit_z       = config.positionLimitZ;
    s.limit_z_back  = config.positionLimitZBack;
    s.invert_x      = config.positionInvertX;
    s.invert_y      = config.positionInvertY;
    s.invert_z      = config.positionInvertZ;
    return s;
}

float SaturatedForwardUnits(float rawZ) {
    cameraunlock::PositionProcessor processor;
    processor.SetSettings(ShippedSettings());
    const cameraunlock::PositionData raw(0.0f, 0.0f, rawZ);
    // Two ticks so the exponential smoothing has settled on the clamped value.
    cameraunlock::math::Vec3 out = processor.Process(raw, cameraunlock::math::Quat4::Identity(), 1.0f);
    out = processor.Process(raw, cameraunlock::math::Quat4::Identity(), 1.0f);
    return SkyrimHT::CameraLocalLeanOffset(out.x, out.y, out.z).x;
}

void LeanBudgetsAreNotReversed() {
    // A metre of physical lean either way: far past both limits, so the output
    // is whichever budget that direction actually got.
    CheckNear(SaturatedForwardUnits(-1.0f), -0.40f * SkyrimHT::UNITS_PER_METER,
              "forward lean gets the 0.40m budget");
    CheckNear(SaturatedForwardUnits(1.0f), 0.10f * SkyrimHT::UNITS_PER_METER,
              "backward lean gets the 0.10m budget");
}

} // namespace

int main() {
    ForwardLeanMovesCameraForward();
    UpAndLateralMapStraightThrough();
    LeanBudgetsAreNotReversed();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all lean-direction checks passed\n");
    return 0;
}
