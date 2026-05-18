#include "pch.h"
#include "game_state.h"
#include "core/logger.h"

namespace SkyrimHT {

bool GameState::Initialize() {
    // Detection is not yet implemented (see game_state.h). Head tracking stays
    // active in all camera states - menus, loading screens, etc.
    Logger::Instance().Warning("Game state detection not implemented - head tracking active in all states");
    return true;
}

bool GameState::IsInGameplay() {
    return true;
}

} // namespace SkyrimHT
