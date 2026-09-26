#include "pch.h"
#include "config.h"

#include "legacy_config/legacy_config.h"

#include <cameraunlock/config/head_tracking_config_table.h>
#include <cameraunlock/input/key_bindings.h>
#include <cameraunlock/tracking/tracking_mode.h>

#include <cmath>
#include <utility>
#include <vector>

namespace SkyrimHT {

namespace {

namespace cfg = cameraunlock::config;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

// A legacy hotkey code and the Ctrl+Shift chord every earlier build registered beside it, as
// one key list.
std::string KeyList(int vk, char letter, const char* key, std::vector<cfg::DroppedValue>& dropped) {
    const std::string code = cfg::LegacyVirtualKeyToBindings(vk, "Hotkeys", key, dropped);
    const std::string chord =
        cameraunlock::input::FormatKeyBindings({KeyBinding{KeyModifiers::kCtrl | KeyModifiers::kShift, letter}});
    return code.empty() ? chord : code + ", " + chord;
}

cfg::ImportResult Import(const cfg::LegacyInput& input, Config& out) {
    // The builds before this one opened the file by the ANSI path GetModuleFileNameA gave them,
    // through inih's fopen, so the import opens it the same way.
    legacy::Config c;
    const legacy::ReadStatus status = legacy::Read(input.ansi_path.c_str(), c);

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> shaping;
    const Config defaults = MakeConfigTable().defaults();

    out.udp_port = c.udpPort;
    out.enable_on_startup = c.autoEnable;
    out.world_space_yaw = c.worldSpaceYaw;
    out.show_notifications = c.showNotifications;

    // [Position] Enabled chose only the mode the session started in: the cycle key reached
    // every mode either way.
    const cameraunlock::TrackingModeChannels mode = cameraunlock::EncodeTrackingMode(
        c.positionEnabled ? cameraunlock::TrackingMode::RotationAndPosition
                          : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = mode.rotation_enabled;
    out.position_enabled = mode.position_enabled;

    // The frozen reader holds both smoothings to [0, 1] and every limit to [0.01, 2], but
    // std::clamp hands a NaN back as it is, and N2 imports one as the row's default.
    const auto finite = [&dropped](float value, float rowDefault, const char* section, const char* key) {
        return cfg::LegacyFiniteOrDefault(value, rowDefault, section, key, dropped);
    };
    out.local_smoothing = finite(c.localSmoothing, defaults.local_smoothing, "Sensitivity", "LocalSmoothing");
    out.position.local_smoothing = out.local_smoothing;
    out.remote_smoothing = finite(c.remoteSmoothing, defaults.remote_smoothing, "Sensitivity", "RemoteSmoothing");
    out.position.remote_smoothing = out.remote_smoothing;

    // LimitY bounded both directions, so it becomes both explicit values.
    out.position.limit_x = finite(c.positionLimitX, defaults.position.limit_x, "Position", "LimitX");
    out.position.limit_y = finite(c.positionLimitY, defaults.position.limit_y, "Position", "LimitY");
    out.position.limit_y_down = std::isfinite(c.positionLimitY) ? c.positionLimitY : defaults.position.limit_y_down;
    out.position.limit_z = finite(c.positionLimitZ, defaults.position.limit_z, "Position", "LimitZ");
    out.position.limit_z_back = finite(c.positionLimitZBack, defaults.position.limit_z_back, "Position", "LimitZBack");

    // Every sensitivity shipped at 1.0, identity. InvertX shipped true, and that inversion is
    // now the x negation in CameraLocalLeanOffset. InvertY shipped false. Every release shipped
    // InvertZ true, which ran depth backwards; 3b1145b fixed the direction in that boundary for
    // InvertZ false, the default b285051 set, so InvertZ is measured against false and a
    // published true is dropped. A value the player changed is dropped.
    const auto shape = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, shaping, dropped);
    };
    shape(c.yawMultiplier, legacy::kDefaultMultiplier, "Sensitivity", "YawMultiplier");
    shape(c.pitchMultiplier, legacy::kDefaultMultiplier, "Sensitivity", "PitchMultiplier");
    shape(c.rollMultiplier, legacy::kDefaultMultiplier, "Sensitivity", "RollMultiplier");
    shape(c.positionSensitivityX, legacy::kDefaultPositionSensitivity, "Position", "SensitivityX");
    shape(c.positionSensitivityY, legacy::kDefaultPositionSensitivity, "Position", "SensitivityY");
    shape(c.positionSensitivityZ, legacy::kDefaultPositionSensitivity, "Position", "SensitivityZ");
    shape(c.positionInvertX, legacy::kDefaultPositionInvertX, "Position", "InvertX");
    shape(c.positionInvertY, legacy::kDefaultPositionInvertY, "Position", "InvertY");
    shape(c.positionInvertZ, legacy::kDefaultPositionInvertZ, "Position", "InvertZ");

    // The game's crosshair now always follows the aim.
    if (!c.showCrosshair) dropped.push_back({cfg::DropRule::Reticle, "Crosshair", "Show", "false"});

    out.toggle_key_name = KeyList(c.toggleKey, 'Y', "ToggleKey", dropped);
    out.cycle_tracking_mode_key_name = KeyList(c.positionToggleKey, 'G', "PositionToggleKey", dropped);
    out.yaw_mode_key_name = KeyList(c.yawModeKey, 'H', "YawModeKey", dropped);

    return status == legacy::ReadStatus::Absent ? cfg::ImportResult::Absent(std::move(dropped), std::move(shaping))
                                                : cfg::ImportResult::Imported(std::move(dropped), std::move(shaping));
}

}  // namespace

cfg::ConfigTable<Config> MakeConfigTable() {
    using C = cfg::schema::Concept;
    cfg::ConfigTable<Config> table = cfg::HeadTrackingConfigTable<Config>(
        {C::UdpPort, C::EnableOnStartup, C::WorldSpaceYaw, C::RotationEnabled, C::LocalSmoothing,
         C::RemoteSmoothing, C::PositionEnabled, C::PositionLimitX, C::PositionLimitY, C::PositionLimitYDown,
         C::PositionLimitZ, C::PositionLimitZBack, C::ToggleKey, C::CycleTrackingModeKey, C::YawModeKey});
    table.Select(C::WorldSpaceYaw).Writable()
        .Select(C::RotationEnabled).Writable()
        .Select(C::PositionEnabled).Writable();
    table.Local("General", "ShowNotifications", &Config::show_notifications, cfg::BoolCodec(),
                "true: write the mod's notices (tracking on or off, a mode change) to HeadTracking.log.");
    return table;
}

cfg::LegacyImport<Config> MakeLegacyImport() {
    return {&Import, legacy::ReadKeys()};
}

cfg::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(const std::wstring& folder, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = folder + kConfigFileName;
    options.legacy_path = folder + kLegacyConfigFileName;
    options.table = MakeConfigTable();
    options.import = MakeLegacyImport();
    options.header.display_name = kConfigDisplayName;
    options.defaults = std::move(defaults);
    return options;
}

} // namespace SkyrimHT
