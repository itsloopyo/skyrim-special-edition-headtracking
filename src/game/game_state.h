#pragma once

namespace SkyrimHT {

// Gates head tracking to actual gameplay (not menus/loading/paused). Detection
// is not yet implemented - it needs RTTI-based singleton discovery for
// PlayerCamera and UI - so IsInGameplay() currently returns true unconditionally
// and tracking runs in every camera state.
//
// Reverse-engineered offsets for when that lands (SE/AE 1.6.x):
//   UI singleton:  numPausesGame int32 @ +0x10C (>0 => pausing menu open),
//                  isLoading bool @ +0x104
//   PlayerCamera:  cameraStateId uint32 @ +0x58
//   CameraStateId: FirstPerson=0 IronSights=4 ThirdPerson=8 Mount=9 (the
//                  states head tracking should stay active in)
class GameState {
public:
    static bool Initialize();
    static bool IsInGameplay();
};

} // namespace SkyrimHT
