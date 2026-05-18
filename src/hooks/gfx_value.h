#pragma once

#include <cstdint>

namespace SkyrimHT {

// Scaleform GFx value layout. Matches the in-engine struct - sizeof must stay
// 0x18; the static_assert pins it.
enum class GFxValueType : uint32_t {
    kUndefined     = 0x00,
    kNull          = 0x01,
    kBoolean       = 0x02,
    kNumber        = 0x03,
    kString        = 0x04,
    kStringW       = 0x05,
    kObject        = 0x06,
    kArray         = 0x07,
    kDisplayObject = 0x08,
};

struct GFxValue {
    void*        objectInterface;  // +0x00
    GFxValueType type;             // +0x08
    uint32_t     pad0C;            // +0x0C
    union {
        double      number;
        bool        boolean;
        const char* str;
        void*       obj;
    }            value;            // +0x10
};
static_assert(sizeof(GFxValue) == 0x18, "GFxValue must be 0x18 bytes");

} // namespace SkyrimHT
