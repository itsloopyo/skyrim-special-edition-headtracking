#pragma once

#include "game/skyrim_types.h"

namespace SkyrimHT {

// Skyrim measures the world in units, the position processor in metres.
inline constexpr float UNITS_PER_METER = 70.0f;

// Maps a processed position offset (metres, X=right, Y=up, Z=depth) onto the
// NiCamera node's local axes, in Skyrim units. Multiply the result by the
// camera's world rotation to get a world offset; the scale factor commutes
// with that rotation.
//
// The axis order is the one the mod has always shipped: the node's first
// component is the one depth drives, the second up, the third lateral.
//
// Depth is negated here, at the engine boundary, rather than through the
// processor's InvertZ. The processor inverts BEFORE its asymmetric clamp of
// [-LimitZ, +LimitZBack], so flipping the sign there hands the generous 0.40m
// allowance to the backward lean and the 0.10m anti-clipping allowance to the
// forward one. Negative z is the forward lean throughout the library.
inline NiPoint3 CameraLocalLeanOffset(float posX, float posY, float posZ) {
    return NiPoint3(-posZ * UNITS_PER_METER,
                     posY * UNITS_PER_METER,
                     posX * UNITS_PER_METER);
}

} // namespace SkyrimHT
