#pragma once

#include <cstdint>

namespace SkyrimHT {

bool InstallHUDMenuHook();
void RemoveHUDMenuHook();

void SetNativeCrosshairAimPixels(float dxFromCenterPx, float dyFromCenterPx,
                                 float screenWidthPx, float screenHeightPx);

} // namespace SkyrimHT
