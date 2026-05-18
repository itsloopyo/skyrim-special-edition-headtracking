#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>

namespace SkyrimHT {

// Minimal Skyrim SE type definitions from CommonLibSSE-NG reverse engineering.
// Skyrim coordinate system: X=east(right), Y=north(forward), Z=up
// All offsets verified for SE/AE (identical), VR differs.

struct NiPoint3 {
    float x, y, z;

    NiPoint3() : x(0), y(0), z(0) {}
    NiPoint3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

// Row-major 3x3 rotation matrix
struct NiMatrix33 {
    float entry[3][3];

    NiMatrix33() { SetIdentity(); }

    void SetIdentity() {
        entry[0][0] = 1; entry[0][1] = 0; entry[0][2] = 0;
        entry[1][0] = 0; entry[1][1] = 1; entry[1][2] = 0;
        entry[2][0] = 0; entry[2][1] = 0; entry[2][2] = 1;
    }

    // ZXY rotation order (yaw * pitch * roll).
    void SetFromEulerAngles(float yaw, float pitch, float roll) {
        const float cy = cosf(yaw),   sy = sinf(yaw);
        const float cp = cosf(pitch), sp = sinf(pitch);
        const float cr = cosf(roll),  sr = sinf(roll);
        entry[0][0] = cy * cr - sy * sp * sr;
        entry[0][1] = -sy * cp;
        entry[0][2] = cy * sr + sy * sp * cr;
        entry[1][0] = sy * cr + cy * sp * sr;
        entry[1][1] = cy * cp;
        entry[1][2] = sy * sr - cy * sp * cr;
        entry[2][0] = -cp * sr;
        entry[2][1] = sp;
        entry[2][2] = cp * cr;
    }

    // Factory that skips the default ctor's SetIdentity() pre-pass since every
    // entry is about to be overwritten. Relies on RVO.
    static NiMatrix33 FromEulerAngles(float yaw, float pitch, float roll) {
        NiMatrix33 m;
        m.SetFromEulerAngles(yaw, pitch, roll);
        return m;
    }

    NiMatrix33 operator*(const NiMatrix33& other) const {
        NiMatrix33 result;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                result.entry[i][j] =
                    entry[i][0] * other.entry[0][j] +
                    entry[i][1] * other.entry[1][j] +
                    entry[i][2] * other.entry[2][j];
        return result;
    }

    NiPoint3 operator*(const NiPoint3& v) const {
        return NiPoint3(
            entry[0][0] * v.x + entry[0][1] * v.y + entry[0][2] * v.z,
            entry[1][0] * v.x + entry[1][1] * v.y + entry[1][2] * v.z,
            entry[2][0] * v.x + entry[2][1] * v.y + entry[2][2] * v.z
        );
    }
};

// Row-major 4x4 matrix (used for worldToCam)
struct NiMatrix44 {
    float entry[4][4];

    // Pre-multiply by a 3x3 rotation (upper-left block), leaving translation/perspective intact.
    // Each new row 0..2 depends only on input rows 0..2, so only save those (48 bytes instead of 64).
    void PreMultiplyRotation(const NiMatrix33& rot) {
        float tmp[3][4];
        memcpy(tmp, entry, sizeof(tmp));

        for (int i = 0; i < 3; i++) {
            const float r0 = rot.entry[i][0];
            const float r1 = rot.entry[i][1];
            const float r2 = rot.entry[i][2];
            for (int j = 0; j < 4; j++) {
                entry[i][j] = r0 * tmp[0][j] + r1 * tmp[1][j] + r2 * tmp[2][j];
            }
        }
        // Row 3 stays [0, 0, 0, 1] - unchanged
    }

    // Post-multiply the upper 3x3 block by a 3x3 rotation. Only columns 0..2
    // change; column 3 (translation) is untouched because the rotation has an
    // implicit (0,0,0,1) fourth row/column.
    void PostMultiplyRotation(const NiMatrix33& rot) {
        float tmp[3][3];
        for (int i = 0; i < 3; i++) {
            tmp[i][0] = entry[i][0];
            tmp[i][1] = entry[i][1];
            tmp[i][2] = entry[i][2];
        }
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                entry[i][j] = tmp[i][0] * rot.entry[0][j]
                            + tmp[i][1] * rot.entry[1][j]
                            + tmp[i][2] * rot.entry[2][j];
            }
        }
    }
};
static_assert(sizeof(NiMatrix44) == 0x40, "NiMatrix44 size mismatch");

// ============================================================
// NiAVObject - base class for all scene graph objects
// ============================================================
// Offsets from CommonLibSSE-NG (SE/AE, NOT VR). The NiTransform layout at
// WorldTransform is {NiMatrix33 rotate (36B), NiPoint3 translate (12B), float scale (4B)}.
namespace NiAVObjectOffsets {
    constexpr uintptr_t LocalTransform      = 0x48;
    constexpr uintptr_t WorldTransform      = 0x7C;
    constexpr uintptr_t WorldTranslateDelta = 0x24;  // NiPoint3 offset within an NiTransform
}

// ============================================================
// NiCamera - extends NiAVObject (size 0x188 SE/AE)
// ============================================================
// +0x110: float worldToCam[4][4]  (the matrix D3D uses for rendering)
// +0x150: NiFrustum { float left, right, top, bottom, nearPlane, farPlane }.
//         left/right/top/bottom are view-window extents normalised to a near
//         plane of 1, so right == tan(horizontalFOV/2) and top ==
//         tan(verticalFOV/2) directly. Verified at runtime: symmetric
//         (left == -right), right/top == display aspect ratio.
namespace NiCameraOffsets {
    constexpr uintptr_t WorldToCam   = 0x110;
    constexpr uintptr_t FrustumRight = 0x154;  // NiFrustum + 1 float
    constexpr uintptr_t FrustumTop   = 0x158;  // NiFrustum + 2 floats
}

// ============================================================
// NiNode - extends NiAVObject (size 0x128 SE/AE)
// ============================================================
// Actual in-memory layout of the children NiTObjectArray (confirmed by diagnostic):
//   +0x110: NiTObjectArray vtable (8 bytes)
//   +0x118: T* data (pointer to array of NiPointer<NiAVObject>)
namespace NiNodeOffsets {
    constexpr uintptr_t ChildrenData = 0x118;   // pointer to array of NiAVObject*
}

// ============================================================
// TESCamera - base camera class
// ============================================================
// +0x20: NiPointer<NiNode> cameraRoot
namespace TESCameraOffsets {
    constexpr uintptr_t CameraRoot = 0x20;
}

} // namespace SkyrimHT
