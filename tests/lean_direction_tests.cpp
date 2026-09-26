// Behaviour lock for the 6DOF lean boundary: which way the camera moves for a
// physical lean, and how much of the asymmetric budget each direction gets.
//
// The bug this guards against shipped once: Config carried InvertZ=true, which
// the processor applies BEFORE its [-LimitZ, +LimitZBack] clamp, so a forward
// lean was cut off at the 0.10m backward allowance and a backward lean was
// handed the 0.40m forward one. Nothing in the render path notices; the camera
// just refuses to lean in.
//
// It also holds the lateral negation CameraLocalLeanOffset does now to the
// [Position] InvertX=true every build before the canonical config shipped,
// which the processor applied: the two give the same lean bit for bit.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

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

uint32_t Bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

// The processor's z runs negative for a forward lean, and depth drives the
// node's first component.
void ForwardLeanMovesCameraForward() {
    const SkyrimHT::NiPoint3 fwd = SkyrimHT::CameraLocalLeanOffset(0.0f, 0.0f, -0.25f);
    Check(fwd.x < 0.0f, "forward lean (processor z < 0) drives depth negative");

    const SkyrimHT::NiPoint3 back = SkyrimHT::CameraLocalLeanOffset(0.0f, 0.0f, 0.25f);
    Check(back.x > 0.0f, "backward lean (processor z > 0) drives depth positive");
}

void UpMapsStraightThroughAndLateralIsNegated() {
    const SkyrimHT::NiPoint3 up = SkyrimHT::CameraLocalLeanOffset(0.0f, 0.1f, 0.0f);
    CheckNear(up.y, 0.1f * SkyrimHT::UNITS_PER_METER, "up maps to the second component");

    const SkyrimHT::NiPoint3 right = SkyrimHT::CameraLocalLeanOffset(0.1f, 0.0f, 0.0f);
    CheckNear(right.z, -0.1f * SkyrimHT::UNITS_PER_METER, "lateral maps, negated, to the third component");
}

// Drives the real processor with the settings this build hands it, so a
// reintroduced InvertZ or a swapped pair of limits fails here rather than in the game.
cameraunlock::PositionSettings RuntimeSettings() {
    return SkyrimHT::MakeConfigTable().defaults().position;
}

cameraunlock::math::Vec3 Settle(const cameraunlock::PositionSettings& settings, const cameraunlock::PositionData& raw) {
    cameraunlock::PositionProcessor processor;
    processor.SetSettings(settings);
    // Two ticks so the exponential smoothing has settled on the clamped value.
    processor.Process(raw, cameraunlock::math::Quat4::Identity(), 1.0f);
    return processor.Process(raw, cameraunlock::math::Quat4::Identity(), 1.0f);
}

float SaturatedForwardUnits(float rawZ) {
    const cameraunlock::math::Vec3 out = Settle(RuntimeSettings(), cameraunlock::PositionData(0.0f, 0.0f, rawZ));
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

// The same raw lateral leans through the processor with InvertX on and the old
// boundary, and through this build's settings and boundary, over several
// frames so the smoothing and both clamps take part.
void LateralNegationMatchesTheShippedInvertX() {
    cameraunlock::PositionSettings shipped = RuntimeSettings();
    shipped.invert_x = true;
    cameraunlock::PositionProcessor before;
    before.SetSettings(shipped);
    cameraunlock::PositionProcessor after;
    after.SetSettings(RuntimeSettings());
    Check(!RuntimeSettings().invert_x, "this build hands the processor no x inversion");

    const float leans[] = {0.05f, -0.12f, 0.31f, -0.9f, 0.0f, 0.2999f, -0.3001f, 0.17f};
    for (float lean : leans) {
        const cameraunlock::PositionData raw(lean, 0.02f, -0.1f);
        const cameraunlock::math::Vec3 a = before.Process(raw, cameraunlock::math::Quat4::Identity(), 0.016f);
        const cameraunlock::math::Vec3 b = after.Process(raw, cameraunlock::math::Quat4::Identity(), 0.016f);
        // The pre-canonical boundary took x straight through.
        const float shippedLateral = a.x * SkyrimHT::UNITS_PER_METER;
        const float lateral = SkyrimHT::CameraLocalLeanOffset(b.x, b.y, b.z).z;
        Check(Bits(shippedLateral) == Bits(lateral), "the lateral lean matches the shipped InvertX bit for bit");
    }
}

} // namespace

int main() {
    ForwardLeanMovesCameraForward();
    UpMapsStraightThroughAndLateralIsNegated();
    LeanBudgetsAreNotReversed();
    LateralNegationMatchesTheShippedInvertX();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all lean-direction checks passed\n");
    return 0;
}
