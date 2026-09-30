#include "hooks/marker_projection.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

int main() {
    int failures = 0;
    const auto check = [&](double actual, double expected, const char* label) {
        if (std::abs(actual - expected) < 0.001) return;
        std::printf("FAIL %s: expected %.6f, got %.6f\n", label, expected, actual);
        ++failures;
    };

    for (const double width : {1280.0, 1920.0}) {
        const double height = 720.0;
        for (const double originX : {0.0, 616.65, 700.0}) {
            const double originY = 475.45;
            for (const double roll : {-1.57079632679, -0.4, 0.0, 0.4, 1.57079632679}) {
                SkyrimHT::CameraRootSnapshots snap{};
                for (int i = 0; i < 3; ++i) snap.cleanNiCamWorld[i][i] = 1.0f;
                snap.trackedNiCamWorld[0][0] = 1.0f;
                snap.trackedNiCamWorld[1][1] = static_cast<float>(std::cos(roll));
                snap.trackedNiCamWorld[1][2] = static_cast<float>(-std::sin(roll));
                snap.trackedNiCamWorld[2][1] = static_cast<float>(std::sin(roll));
                snap.trackedNiCamWorld[2][2] = static_cast<float>(std::cos(roll));
                snap.frustumRight = 1.0f;
                snap.frustumTop = static_cast<float>(height / width);

                for (const double dx : {-240.0, 0.0, 180.0}) {
                    for (const double dy : {-160.0, 0.0, 120.0}) {
                        double x = 0.0, y = 0.0;
                        const bool projected = SkyrimHT::ProjectFloatingMarker(snap, width, height,
                            originX, originY, width * 0.5 + dx - originX,
                            height * 0.5 + dy - originY, x, y);
                        check(projected, true, "visible marker projects");
                        check(x + originX - width * 0.5,
                              dx * std::cos(roll) + dy * std::sin(roll), "rolled screen x");
                        check(y + originY - height * 0.5,
                              dy * std::cos(roll) - dx * std::sin(roll), "rolled screen y");
                    }
                }
                snap.trackedNiCamWorld[0][0] = -1.0f;
                double x = 123.0, y = 456.0;
                check(SkyrimHT::ProjectFloatingMarker(snap, width, height, originX, originY,
                    width * 0.5 - originX, height * 0.5 - originY, x, y), false, "behind camera");
                check(x, 123.0, "failed projection preserves x");
                check(y, 456.0, "failed projection preserves y");
            }
        }
    }
    return failures ? 1 : 0;
}
