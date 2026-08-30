#pragma once

namespace SkyrimHT {

// Gates head tracking to active gameplay. The test is the mouse capture:
// Skyrim hides and clips the OS cursor while the player is playing, and a
// menu that hands the pointer back shows it again. Reading that instead of an
// engine singleton keeps the gate free of per-build addresses to re-derive
// after a game patch.
//
// The foreground-window check is what keeps another application's mouse
// capture from reading as gameplay, and it also rules out an alt-tabbed
// session.
//
// Result is cached briefly, so callers on the render path may ask every frame.
class GameState {
public:
    static bool Initialize();
    static bool IsInGameplay();
};

} // namespace SkyrimHT
