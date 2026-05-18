#include "pch.h"
#include "input_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "core/hotkey_utils.h"

namespace SkyrimHT {

namespace {

// ~60Hz polling - matches the old sleep value and is gentle enough that
// GetAsyncKeyState never misses a quick tap.
constexpr DWORD kPollIntervalMs = 16;

// Action identifiers keyed by the 4 mod commands the user can bind.
enum class Action { Toggle, Recenter, CycleDofMode, ToggleYawMode, CycleAxisIsolation, DumpMatrices };

void Dispatch(Action a) {
    switch (a) {
        case Action::Toggle:             Mod::Instance().Toggle(); break;
        case Action::Recenter:           Mod::Instance().Recenter(); break;
        case Action::CycleDofMode:     Mod::Instance().CycleDofMode(); break;
        case Action::ToggleYawMode:      Mod::Instance().ToggleYawMode(); break;
        case Action::CycleAxisIsolation: Mod::Instance().CycleAxisIsolation(); break;
        case Action::DumpMatrices:       Mod::Instance().DumpMatrices(); break;
    }
}

bool IsCtrlShiftHeld() {
    const bool ctrl  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetAsyncKeyState(VK_SHIFT)   & 0x8000) != 0;
    return ctrl && shift;
}

// One entry per key-binding the polling thread watches. Nav-cluster bindings
// carry a runtime-configurable atomic VK code (`configurableVk`); chord
// bindings carry a fixed VK code (`fixedVk`) for the Ctrl+Shift+T/Y/G/H
// letters per the CameraUnlock standard. Exactly one of the two is in use
// per entry, signalled by `configurableVk != nullptr`.
struct Binding {
    std::atomic<int>*  configurableVk; // non-null for nav-cluster; null for chords
    int                fixedVk;        // VK code used when configurableVk is null
    const char*        fixedLabel;     // human-readable label for the fixed VK
    bool               requireChord;   // true => only fires with Ctrl+Shift held
    Action             action;
    std::atomic<bool>  down{false};
};

std::atomic<int> g_toggleKey        {DEFAULT_TOGGLE_KEY};
std::atomic<int> g_recenterKey      {DEFAULT_RECENTER_KEY};
std::atomic<int> g_positionToggleKey{DEFAULT_POSITION_TOGGLE_KEY};
std::atomic<int> g_yawModeKey       {DEFAULT_YAW_MODE_KEY};

// Binding table. Nav-cluster entries have a live atomic vkCode so Reload could
// rebind them at runtime; chord entries are fixed letters. Chord action letters
// match the `Ctrl+Shift+<letter>` cluster standard (T/Y/G/H).
Binding g_bindings[] = {
    { &g_toggleKey,         0,   nullptr,       false, Action::Toggle         },
    { &g_recenterKey,       0,   nullptr,       false, Action::Recenter       },
    { &g_positionToggleKey, 0,   nullptr,       false, Action::CycleDofMode },
    { &g_yawModeKey,        0,   nullptr,       false, Action::ToggleYawMode  },
    { nullptr,              'Y', "Ctrl+Shift+Y", true,  Action::Toggle         },
    { nullptr,              'T', "Ctrl+Shift+T", true,  Action::Recenter       },
    { nullptr,              'G', "Ctrl+Shift+G", true,  Action::CycleDofMode },
    { nullptr,              'H', "Ctrl+Shift+H", true,  Action::ToggleYawMode  },
    { nullptr,              VK_F8, "F8",          false, Action::CycleAxisIsolation },
    { nullptr,              VK_INSERT, "Insert",   false, Action::DumpMatrices       },
};

void PollBinding(Binding& b, bool chordActive) {
    const int vk = b.configurableVk ? b.configurableVk->load() : b.fixedVk;
    bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
    if (b.requireChord) {
        pressed = pressed && chordActive;
    } else {
        // Nav-cluster keys are suppressed while Ctrl+Shift is held so the
        // chord path is the sole trigger for chord combos and a single
        // keypress can't fire two actions on layouts where the chord
        // letter aliases a nav-cluster scancode.
        pressed = pressed && !chordActive;
    }

    const bool wasDown = b.down.load();
    if (pressed && !wasDown) {
        b.down.store(true);
        Logger::Instance().Debug("%s pressed", b.configurableVk ? VirtualKeyToString(vk) : b.fixedLabel);
        Dispatch(b.action);
    } else if (!pressed && wasDown) {
        b.down.store(false);
    }
}

std::thread         g_inputThread;
std::atomic<bool>   g_stopFlag{false};
std::atomic<bool>   g_running{false};

void InputPollingThread() {
    Logger::Instance().Debug("Input polling thread started");
    while (!g_stopFlag.load()) {
        const bool chordActive = IsCtrlShiftHeld();
        for (Binding& b : g_bindings) {
            PollBinding(b, chordActive);
        }
        Sleep(kPollIntervalMs);
    }
    Logger::Instance().Debug("Input polling thread stopped");
}

} // namespace

bool InstallInputHook() {
    if (g_running.load()) {
        return true;
    }

    const Config& config = Mod::Instance().GetConfig();
    g_toggleKey.store(config.toggleKey);
    g_recenterKey.store(config.recenterKey);
    g_positionToggleKey.store(config.positionToggleKey);
    g_yawModeKey.store(config.yawModeKey);

    for (Binding& b : g_bindings) b.down.store(false);

    g_stopFlag.store(false);
    g_running.store(true);
    g_inputThread = std::thread(InputPollingThread);

    Logger::Instance().Info("Input hook installed - Toggle: %s, Recenter: %s",
        VirtualKeyToString(g_toggleKey.load()), VirtualKeyToString(g_recenterKey.load()));

    return true;
}

void RemoveInputHook() {
    if (!g_running.load()) {
        return;
    }

    g_stopFlag.store(true);

    if (g_inputThread.joinable()) {
        g_inputThread.join();
    }

    g_running.store(false);
    Logger::Instance().Info("Input hook removed");
}

} // namespace SkyrimHT
