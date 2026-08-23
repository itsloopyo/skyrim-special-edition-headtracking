#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>

namespace SkyrimHT {

// Minimal Skyrim SE type definitions. The layouts below are our own; every
// offset was read off the running game and cross-checked against the public
// CommonLibSSE-NG reverse-engineering notes. No code from that project is used.
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

// Row-major 4x4 matrix (used for worldToCam). Head tracking modifies its
// translation directly (6DOF lean) but never its rotation, so no rotation
// helpers are needed.
struct NiMatrix44 {
    float entry[4][4];
};
static_assert(sizeof(NiMatrix44) == 0x40, "NiMatrix44 size mismatch");

// ============================================================
// NiAVObject - base class for all scene graph objects
// ============================================================
// Offsets cross-checked against CommonLibSSE-NG (SE/AE, NOT VR). The NiTransform layout at
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
