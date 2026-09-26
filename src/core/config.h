#pragma once

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/config/head_tracking_config.h>
#include <cameraunlock/config/legacy_import.h>

#include <string>

namespace SkyrimHT {

// Beside the .asi, which is beside SkyrimSE.exe.
constexpr const wchar_t* kConfigFileName = L"CameraUnlock.ini";
// The file every build before the canonical config format read, imported once while
// CameraUnlock.ini is absent and never written.
constexpr const wchar_t* kLegacyConfigFileName = L"HeadTracking.ini";
// The game's name as cameraunlock-core's data/games.json spells it.
constexpr const char* kConfigDisplayName = "Skyrim Special Edition";

// Core's config, at core's defaults, plus the one setting only this mod has.
struct Config : cameraunlock::HeadTrackingConfig {
    bool show_notifications = true;
};

// The rows of CameraUnlock.ini. Only the tracking mode pair and WorldSpaceYaw are Writable:
// the mode and yaw hotkeys save the player's choice, and End changes the session only.
cameraunlock::config::ConfigTable<Config> MakeConfigTable();

// HeadTracking.ini as the builds before the canonical format read it (legacy_config/), mapped
// into Config.
cameraunlock::config::LegacyImport<Config> MakeLegacyImport();

// The owner's options for CameraUnlock.ini in `folder`, with HeadTracking.ini beside it as the
// legacy file. `folder` ends in a separator. The mod passes DefaultsFile::PerUser() and a test
// a scratch file.
cameraunlock::config::ConfigOwnerOptions<Config> MakeConfigOwnerOptions(
    const std::wstring& folder, cameraunlock::config::DefaultsFile defaults);

} // namespace SkyrimHT
