#include "pch.h"
#include "input_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include "core/hotkey_utils.h"

#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/input/chord_hotkeys.h>

namespace SkyrimHT {

namespace {

// ~60Hz polling - gentle enough that GetAsyncKeyState never misses a quick tap.
constexpr int kPollIntervalMs = 16;

cameraunlock::input::HotkeyPoller g_poller;
std::atomic<bool> g_running{false};
bool g_bindingsRegistered = false;

void RegisterBindings(const Config& config) {
    using cameraunlock::input::NavGuarded;
    using cameraunlock::input::ChordGuarded;

    // Nav-cluster bindings (configurable via HeadTracking.ini). NavGuarded
    // suppresses them while Ctrl+Shift is held so a single keypress can't fire
    // two actions on layouts where a chord letter aliases a nav-cluster scancode.
    g_poller.AddHotkey(config.toggleKey,         NavGuarded([] { Mod::Instance().Toggle(); }));
    g_poller.AddHotkey(config.positionToggleKey, NavGuarded([] { Mod::Instance().CycleDofMode(); }));
    g_poller.AddHotkey(config.yawModeKey,        NavGuarded([] { Mod::Instance().ToggleYawMode(); }));

    // Ctrl+Shift+<letter> chord alternatives per the CameraUnlock standard:
    // Y=Toggle, G=Position, H=4th toggle (yaw mode).
    g_poller.AddHotkey('Y', ChordGuarded([] { Mod::Instance().Toggle(); }));
    g_poller.AddHotkey('G', ChordGuarded([] { Mod::Instance().CycleDofMode(); }));
    g_poller.AddHotkey('H', ChordGuarded([] { Mod::Instance().ToggleYawMode(); }));

    // Diagnostics: F8 cycles axis isolation, Insert dumps camera matrices.
    g_poller.AddHotkey(VK_F8,     NavGuarded([] { Mod::Instance().CycleAxisIsolation(); }));
    g_poller.AddHotkey(VK_INSERT, NavGuarded([] { Mod::Instance().DumpMatrices(); }));
}

} // namespace

bool InstallInputHook() {
    if (g_running.load()) {
        return true;
    }

    const Config& config = Mod::Instance().GetConfig();
    if (!g_bindingsRegistered) {
        RegisterBindings(config);
        g_bindingsRegistered = true;
    }

    if (!g_poller.Start(kPollIntervalMs)) {
        Logger::Instance().Error("Hotkey poller failed to start");
        return false;
    }
    g_running.store(true);

    Logger::Instance().Info("Input hook installed - Toggle: %s",
        VirtualKeyToString(config.toggleKey));

    return true;
}

void RemoveInputHook() {
    if (!g_running.load()) {
        return;
    }

    g_poller.Stop();
    g_running.store(false);
    Logger::Instance().Info("Input hook removed");
}

} // namespace SkyrimHT
