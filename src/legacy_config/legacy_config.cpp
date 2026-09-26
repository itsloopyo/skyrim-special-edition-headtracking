// SPDX-License-Identifier: MIT

#include "pch.h"
#include "legacy_config/legacy_config.h"

#include "core/logger.h"

extern "C" {
#include "ini.h"
}

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace SkyrimHT::legacy {

namespace {

void Validate(Config& c) {
    c.yawMultiplier = std::clamp(c.yawMultiplier, 0.1f, 5.0f);
    c.pitchMultiplier = std::clamp(c.pitchMultiplier, 0.1f, 5.0f);
    c.rollMultiplier = std::clamp(c.rollMultiplier, 0.0f, 2.0f);

    c.localSmoothing = std::clamp(c.localSmoothing, 0.0f, 1.0f);
    c.remoteSmoothing = std::clamp(c.remoteSmoothing, 0.0f, 1.0f);

    c.positionSensitivityX = std::clamp(c.positionSensitivityX, 0.1f, 10.0f);
    c.positionSensitivityY = std::clamp(c.positionSensitivityY, 0.1f, 10.0f);
    c.positionSensitivityZ = std::clamp(c.positionSensitivityZ, 0.1f, 10.0f);

    c.positionLimitX = std::clamp(c.positionLimitX, 0.01f, 2.0f);
    c.positionLimitY = std::clamp(c.positionLimitY, 0.01f, 2.0f);
    c.positionLimitZ = std::clamp(c.positionLimitZ, 0.01f, 2.0f);
    c.positionLimitZBack = std::clamp(c.positionLimitZBack, 0.01f, 2.0f);

    if (c.udpPort < 1024) {
        Logger::Instance().Warning("UDP port %d is in reserved range, using default %d",
                                   c.udpPort, kDefaultUdpPort);
        c.udpPort = kDefaultUdpPort;
    }
}

inline bool ParseBool(const char* value) {
    return strcmp(value, "true") == 0 || atoi(value) == 1;
}

inline float ParseFloat(const char* value) {
    return static_cast<float>(atof(value));
}

inline int ParseInt(const char* value) {
    return static_cast<int>(strtol(value, nullptr, 0));
}

// Only reached when the retired key is actually present in the user's file:
// inih only invokes the handler for keys that exist.
//
// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. Both retired
// single-value keys carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    warned = true;
    Logger::Instance().Warning(
        "Config key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

int ConfigHandler(void* user, const char* section, const char* name, const char* value) {
    Config* config = static_cast<Config*>(user);

#define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)

    if (MATCH("Network", "UDPPort")) {
        // atoi truncates silently: "70000" would wrap to 4464, "-1" to 65535.
        // Parse wide, range-check, and keep the default on bad input.
        long port = strtol(value, nullptr, 10);
        if (port >= 1024 && port <= 65535) {
            config->udpPort = static_cast<uint16_t>(port);
        } else {
            Logger::Instance().Warning("UDPPort %ld out of range [1024-65535], keeping %d",
                                       port, config->udpPort);
        }
    }
    else if (MATCH("Sensitivity", "YawMultiplier"))   { config->yawMultiplier   = ParseFloat(value); }
    else if (MATCH("Sensitivity", "PitchMultiplier")) { config->pitchMultiplier = ParseFloat(value); }
    else if (MATCH("Sensitivity", "RollMultiplier"))  { config->rollMultiplier  = ParseFloat(value); }
    else if (MATCH("Sensitivity", "LocalSmoothing"))  { config->localSmoothing  = ParseFloat(value); }
    else if (MATCH("Sensitivity", "RemoteSmoothing")) { config->remoteSmoothing = ParseFloat(value); }

    // Retired keys, both replaced by LocalSmoothing/RemoteSmoothing above. Matched only
    // so the user gets told they are dead instead of the values silently vanishing. The
    // helper's one-shot flag is shared, so an INI carrying both still logs one line.
    else if (MATCH("Sensitivity", "RotationSmoothing")) { WarnRetiredSmoothingKey("Sensitivity", "RotationSmoothing"); }
    else if (MATCH("Position", "Smoothing"))            { WarnRetiredSmoothingKey("Position", "Smoothing"); }

    else if (MATCH("Hotkeys", "ToggleKey"))         { config->toggleKey         = ParseInt(value); }
    else if (MATCH("Hotkeys", "PositionToggleKey")) { config->positionToggleKey = ParseInt(value); }
    else if (MATCH("Hotkeys", "YawModeKey"))        { config->yawModeKey        = ParseInt(value); }

    else if (MATCH("Position", "SensitivityX")) { config->positionSensitivityX = ParseFloat(value); }
    else if (MATCH("Position", "SensitivityY")) { config->positionSensitivityY = ParseFloat(value); }
    else if (MATCH("Position", "SensitivityZ")) { config->positionSensitivityZ = ParseFloat(value); }
    else if (MATCH("Position", "LimitX"))       { config->positionLimitX       = ParseFloat(value); }
    else if (MATCH("Position", "LimitY"))       { config->positionLimitY       = ParseFloat(value); }
    else if (MATCH("Position", "LimitZ"))       { config->positionLimitZ       = ParseFloat(value); }
    else if (MATCH("Position", "LimitZBack"))   { config->positionLimitZBack   = ParseFloat(value); }
    else if (MATCH("Position", "InvertX"))      { config->positionInvertX      = ParseBool(value);  }
    else if (MATCH("Position", "InvertY"))      { config->positionInvertY      = ParseBool(value);  }
    else if (MATCH("Position", "InvertZ"))      { config->positionInvertZ      = ParseBool(value);  }
    else if (MATCH("Position", "Enabled"))      { config->positionEnabled      = ParseBool(value);  }

    else if (MATCH("General", "AutoEnable"))        { config->autoEnable        = ParseBool(value); }
    else if (MATCH("General", "ShowNotifications")) { config->showNotifications = ParseBool(value); }
    else if (MATCH("General", "WorldSpaceYaw"))     { config->worldSpaceYaw     = ParseBool(value); }

    else if (MATCH("Crosshair", "Show"))             { config->showCrosshair    = ParseBool(value); }

#undef MATCH

    return 1;
}

}  // namespace

ReadStatus Read(const char* path, Config& cfg) {
    cfg = Config{};

    int result = ini_parse(path, ConfigHandler, &cfg);
    if (result < 0) {
        Logger::Instance().Warning("Could not load config from %s, using defaults", path);
        return ReadStatus::Absent;
    }
    if (result > 0) {
        Logger::Instance().Warning("Config parse error on line %d", result);
    }

    Validate(cfg);
    Logger::Instance().Info("Config loaded from %s", path);
    return ReadStatus::Read;
}

std::vector<cameraunlock::config::LegacyKey> ReadKeys() {
    return {
        {"Network", "UDPPort"},
        {"Sensitivity", "YawMultiplier"},
        {"Sensitivity", "PitchMultiplier"},
        {"Sensitivity", "RollMultiplier"},
        {"Sensitivity", "LocalSmoothing"},
        {"Sensitivity", "RemoteSmoothing"},
        {"Hotkeys", "ToggleKey"},
        {"Hotkeys", "PositionToggleKey"},
        {"Hotkeys", "YawModeKey"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "InvertX"},
        {"Position", "InvertY"},
        {"Position", "InvertZ"},
        {"Position", "Enabled"},
        {"General", "AutoEnable"},
        {"General", "ShowNotifications"},
        {"General", "WorldSpaceYaw"},
        {"Crosshair", "Show"},
    };
}

}  // namespace SkyrimHT::legacy
