#include "pch.h"
#include "config.h"
#include "logger.h"

extern "C" {
#include "ini.h"
}

#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace SkyrimHT {

// Inline member initializers on the Config struct are the single source of truth
// for defaults. SetDefaults() resets the whole struct to its freshly-constructed state.
void Config::SetDefaults() {
    *this = Config{};
}

void Config::Validate() {
    yawMultiplier = std::clamp(yawMultiplier, 0.1f, 5.0f);
    pitchMultiplier = std::clamp(pitchMultiplier, 0.1f, 5.0f);
    rollMultiplier = std::clamp(rollMultiplier, 0.0f, 2.0f);

    localSmoothing = std::clamp(localSmoothing, 0.0f, 1.0f);
    remoteSmoothing = std::clamp(remoteSmoothing, 0.0f, 1.0f);

    positionSensitivityX = std::clamp(positionSensitivityX, 0.1f, 10.0f);
    positionSensitivityY = std::clamp(positionSensitivityY, 0.1f, 10.0f);
    positionSensitivityZ = std::clamp(positionSensitivityZ, 0.1f, 10.0f);

    positionLimitX = std::clamp(positionLimitX, 0.01f, 2.0f);
    positionLimitY = std::clamp(positionLimitY, 0.01f, 2.0f);
    positionLimitZ = std::clamp(positionLimitZ, 0.01f, 2.0f);
    positionLimitZBack = std::clamp(positionLimitZBack, 0.01f, 2.0f);

    if (udpPort < 1024) {
        Logger::Instance().Warning("UDP port %d is in reserved range, using default %d",
                                   udpPort, DEFAULT_UDP_PORT);
        udpPort = DEFAULT_UDP_PORT;
    }
}

namespace {

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

} // namespace

int Config::ConfigHandler(void* user, const char* section, const char* name, const char* value) {
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

bool Config::Load(const char* path) {
    SetDefaults();

    int result = ini_parse(path, ConfigHandler, this);
    if (result < 0) {
        Logger::Instance().Warning("Could not load config from %s, using defaults", path);
        return false;
    }
    if (result > 0) {
        Logger::Instance().Warning("Config parse error on line %d", result);
    }

    Validate();
    Logger::Instance().Info("Config loaded from %s", path);
    return true;
}

bool Config::Save(const char* path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        Logger::Instance().Error("Failed to save config to %s", path);
        return false;
    }

    file << "; Skyrim SE Head Tracking Configuration\n";
    file << "; Delete this file to reset to defaults\n\n";

    file << "[Network]\n";
    file << "; UDP port for OpenTrack data (default: 4242)\n";
    file << "UDPPort=" << udpPort << "\n\n";

    file << "[Sensitivity]\n";
    file << "; Rotation sensitivity multipliers (1.0 = 1:1)\n";
    file << "YawMultiplier=" << yawMultiplier << "\n";
    file << "PitchMultiplier=" << pitchMultiplier << "\n";
    file << "RollMultiplier=" << rollMultiplier << "\n";
    file << "; Smoothing, applied to both rotation and position. The value is picked\n";
    file << "; per connection from the packet source address.\n";
    file << "; LocalSmoothing: tracker running on this machine (loopback).\n";
    file << "; RemoteSmoothing: tracker on a remote network device (phone on WiFi).\n";
    file << "; 0.0 = no smoothing, 1.0 = heavy. Raise for a noisier tracker - it\n";
    file << "; costs perceived latency.\n";
    file << "LocalSmoothing=" << localSmoothing << "\n";
    file << "RemoteSmoothing=" << remoteSmoothing << "\n\n";

    file << "[Position]\n";
    file << "; Position tracking sensitivity (0.1-10.0, higher = more movement)\n";
    file << "SensitivityX=" << positionSensitivityX << "\n";
    file << "SensitivityY=" << positionSensitivityY << "\n";
    file << "SensitivityZ=" << positionSensitivityZ << "\n";
    file << "; Position limits in meters (how far the camera can move)\n";
    file << "LimitX=" << positionLimitX << "\n";
    file << "LimitY=" << positionLimitY << "\n";
    file << "LimitZ=" << positionLimitZ << "\n";
    file << "; Backward lean limit (prevents camera clipping through player model)\n";
    file << "LimitZBack=" << positionLimitZBack << "\n";
    file << "; Invert position axes\n";
    file << "InvertX=" << (positionInvertX ? "true" : "false") << "\n";
    file << "InvertY=" << (positionInvertY ? "true" : "false") << "\n";
    file << "InvertZ=" << (positionInvertZ ? "true" : "false") << "\n";
    file << "; Enable/disable position tracking (6DOF)\n";
    file << "Enabled=" << (positionEnabled ? "true" : "false") << "\n\n";

    file << "[Hotkeys]\n";
    file << "; Virtual key codes (hex)\n";
    file << "ToggleKey=0x" << std::hex << toggleKey << "    ; End - Enable/disable\n";
    file << "PositionToggleKey=0x" << std::hex << positionToggleKey << " ; Page Up - Toggle position\n";
    file << "YawModeKey=0x" << std::hex << yawModeKey << "        ; Page Down - Toggle world/local yaw\n\n";

    file << "[General]\n";
    file << "; Auto-enable tracking on game start\n";
    file << "AutoEnable=" << (autoEnable ? "true" : "false") << "\n";
    file << "; Show on-screen notifications (logged to HeadTracking.log)\n";
    file << "ShowNotifications=" << (showNotifications ? "true" : "false") << "\n";
    file << "; Yaw mode: true = horizon-locked (default), false = camera-local\n";
    file << "WorldSpaceYaw=" << (worldSpaceYaw ? "true" : "false") << "\n\n";

    file << "[Crosshair]\n";
    file << "; Reposition the game's native crosshair to follow your aim once\n";
    file << "; head tracking moves the view. Set false to leave it at centre.\n";
    file << "Show=" << (showCrosshair ? "true" : "false") << "\n";

    file.close();
    Logger::Instance().Info("Config saved to %s", path);
    return true;
}

} // namespace SkyrimHT
