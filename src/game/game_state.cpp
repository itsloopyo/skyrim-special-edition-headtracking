#include "pch.h"
#include "game_state.h"
#include "core/logger.h"

namespace SkyrimHT {

namespace {

// True when the cursor is confined to something smaller than the whole desktop.
// Windows reports an unclipped cursor as the full virtual screen, so anything
// narrower means an application has taken the mouse.
bool CursorIsClipped() {
    RECT clip{};
    if (!GetClipCursor(&clip)) return false;
    const long vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const long vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const long vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const long vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return !(clip.left <= vx && clip.top <= vy &&
             clip.right >= vx + vw && clip.bottom >= vy + vh);
}

// Either half of the mouse capture is enough to mean "playing". Requiring both
// would drop tracking the moment any overlay in the session shows a cursor,
// because cursor visibility is global desktop state rather than per-window;
// requiring only visibility would miss a state that shows a cursor without
// releasing the clip.
bool GameplayGate() {
    constexpr ULONGLONG kGameStateIntervalMs = 100;
    static ULONGLONG s_lastCheckMs = 0;
    static bool      s_lastResult  = false;

    const ULONGLONG now = GetTickCount64();
    if (now - s_lastCheckMs < kGameStateIntervalMs) return s_lastResult;
    s_lastCheckMs = now;

    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid != GetCurrentProcessId()) {
        s_lastResult = false;
        return false;
    }

    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci)) {
        static bool s_failLogged = false;
        if (!s_failLogged) {
            Logger::Instance().Warning(
                "GetCursorInfo failed (%lu) - falling back to the cursor clip alone to tell "
                "gameplay from a menu", GetLastError());
            s_failLogged = true;
        }
        s_lastResult = CursorIsClipped();
        return s_lastResult;
    }

    s_lastResult = ((ci.flags & CURSOR_SHOWING) == 0) || CursorIsClipped();
    return s_lastResult;
}

} // namespace

bool GameState::Initialize() {
    Logger::Instance().Info("Game state gate: cursor capture + foreground window");
    return true;
}

bool GameState::IsInGameplay() {
    return GameplayGate();
}

} // namespace SkyrimHT
