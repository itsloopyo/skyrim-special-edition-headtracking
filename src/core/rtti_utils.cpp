#include "pch.h"
#include "rtti_utils.h"

#include "core/constants.h"
#include "core/logger.h"

#include <cameraunlock/memory/pattern_scanner.h>

namespace SkyrimHT {

uintptr_t FindVtableByRTTI(uintptr_t moduleBase, size_t moduleSize, const char* className) {
    HMODULE gameModule = GetModuleHandleA(GAME_EXE);

    void* typeDesc = cameraunlock::memory::FindRTTIDescriptor(gameModule, className);
    if (!typeDesc) {
        Logger::Instance().Error("RTTI TypeDescriptor not found for: %s", className);
        return 0;
    }

    const uint32_t typeDescRVA =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(typeDesc) - moduleBase);
    const uint8_t* scanStart = reinterpret_cast<const uint8_t*>(moduleBase);
    uintptr_t colAddr = 0;

    for (size_t i = 0; i + sizeof(RTTICompleteObjectLocator) <= moduleSize; i += 4) {
        const auto* candidate = reinterpret_cast<const RTTICompleteObjectLocator*>(scanStart + i);
        if (candidate->signature == 1 &&
            candidate->offset == 0 &&
            candidate->pTypeDescriptor == typeDescRVA &&
            candidate->pSelf == static_cast<uint32_t>(i)) {
            colAddr = moduleBase + i;
            break;
        }
    }

    if (colAddr == 0) {
        Logger::Instance().Error("CompleteObjectLocator not found for: %s", className);
        return 0;
    }

    for (size_t i = 0; i + 8 <= moduleSize; i += 8) {
        if (*reinterpret_cast<const uintptr_t*>(scanStart + i) == colAddr) {
            return moduleBase + i + 8;
        }
    }

    Logger::Instance().Error("Vtable reference not found for: %s", className);
    return 0;
}

} // namespace SkyrimHT
