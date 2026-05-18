#pragma once

#include <cstddef>
#include <cstdint>

namespace SkyrimHT {

// MSVC RTTI CompleteObjectLocator layout (32-bit RVAs, x64 image).
struct RTTICompleteObjectLocator {
    uint32_t signature;
    uint32_t offset;
    uint32_t cdOffset;
    uint32_t pTypeDescriptor;
    uint32_t pClassDescriptor;
    uint32_t pSelf;
};

// Discover a class's vtable by walking RTTI structures in the game module:
// locate the type's CompleteObjectLocator, then the vtable that references it.
// Returns the address of vtable slot 0, or 0 if discovery fails (logged).
uintptr_t FindVtableByRTTI(uintptr_t moduleBase, size_t moduleSize, const char* className);

} // namespace SkyrimHT
