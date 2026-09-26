#include "pch.h"
#include "input_hook.h"
#include "core/mod.h"
#include "core/logger.h"
#include <cameraunlock/input/hotkey_poller.h>
#include <cameraunlock/input/key_binding_registration.h>
#include <cameraunlock/input/key_bindings.h>

#include <functional>
#include <stdexcept>
#include <string>

// Diagnostics for deriving camera behaviour on a new game build, not something
// a player needs. Configure with -DSKYRIMHT_DEV_HOTKEYS=ON to re-arm them.
#ifndef SKYRIMHT_DEV_HOTKEYS
#define SKYRIMHT_DEV_HOTKEYS 0
#endif

#if SKYRIMHT_DEV_HOTKEYS
#include <cameraunlock/input/chord_hotkeys.h>
#endif

namespace SkyrimHT {

namespace {

// ~60Hz polling - gentle enough that GetAsyncKeyState never misses a quick tap.
constexpr int kPollIntervalMs = 16;

cameraunlock::input::HotkeyPoller g_poller;
std::atomic<bool> g_running{false};
bool g_bindingsRegistered = false;

// A key list from CameraUnlock.ini onto the poller. The table only holds lists its
// hotkey codec read, so one that does not parse here is a bug, not a player's typo.
void Register(const std::string& list, const char* key, std::function<void()> action) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) {
        throw std::logic_error(std::string("[Hotkeys] ") + key + "=" + list + " does not parse: " + parsed.error);
    }
    cameraunlock::input::RegisterKeyBindings(g_poller, parsed.bindings, std::move(action));
}

void RegisterBindings(const Config& config) {
    // Each list holds every key that fires its action, the Ctrl+Shift chord
    // included, and a key without modifiers stays silent while Ctrl and Shift
    // are both held, so one press never fires two actions.
    Register(config.toggle_key_name, "ToggleKey", [] { Mod::Instance().Toggle(); });
    Register(config.cycle_tracking_mode_key_name, "CycleTrackingModeKey", [] { Mod::Instance().CycleDofMode(); });
    Register(config.yaw_mode_key_name, "YawModeKey", [] { Mod::Instance().ToggleYawMode(); });

#if SKYRIMHT_DEV_HOTKEYS
    using cameraunlock::input::NavGuarded;
    // Diagnostics: F8 cycles axis isolation, Insert dumps camera matrices.
    g_poller.AddHotkey(VK_F8,     NavGuarded([] { Mod::Instance().CycleAxisIsolation(); }));
    g_poller.AddHotkey(VK_INSERT, NavGuarded([] { Mod::Instance().DumpMatrices(); }));
#endif
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

    Logger::Instance().Info("Input hook installed - toggle=[%s] cycle tracking mode=[%s] yaw mode=[%s]",
        config.toggle_key_name.c_str(), config.cycle_tracking_mode_key_name.c_str(),
        config.yaw_mode_key_name.c_str());

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
