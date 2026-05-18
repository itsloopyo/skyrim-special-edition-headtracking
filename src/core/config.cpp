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

    rotationSmoothing = std::clamp(rotationSmoothing, 0.0f, 0.99f);

    positionSensitivityX = std::clamp(positionSensitivityX, 0.1f, 10.0f);
    positionSensitivityY = std::clamp(positionSensitivityY, 0.1f, 10.0f);
    positionSensitivityZ = std::clamp(positionSensitivityZ, 0.1f, 10.0f);

    positionLimitX = std::clamp(positionLimitX, 0.01f, 2.0f);
    positionLimitY = std::clamp(positionLimitY, 0.01f, 2.0f);
    positionLimitZ = std::clamp(positionLimitZ, 0.01f, 2.0f);
    positionLimitZBack = std::clamp(positionLimitZBack, 0.01f, 2.0f);

    positionSmoothing = std::clamp(positionSmoothing, 0.0f, 0.99f);

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
    else if (MATCH("Sensitivity", "RotationSmoothing")) { config->rotationSmoothing = ParseFloat(value); }

    else if (MATCH("Hotkeys", "ToggleKey"))         { config->toggleKey         = ParseInt(value); }
    else if (MATCH("Hotkeys", "RecenterKey"))       { config->recenterKey       = ParseInt(value); }
    else if (MATCH("Hotkeys", "PositionToggleKey")) { config->positionToggleKey = ParseInt(value); }
    else if (MATCH("Hotkeys", "YawModeKey"))        { config->yawModeKey        = ParseInt(value); }

    else if (MATCH("Position", "SensitivityX")) { config->positionSensitivityX = ParseFloat(value); }
    else if (MATCH("Position", "SensitivityY")) { config->positionSensitivityY = ParseFloat(value); }
    else if (MATCH("Position", "SensitivityZ")) { config->positionSensitivityZ = ParseFloat(value); }
    else if (MATCH("Position", "LimitX"))       { config->positionLimitX       = ParseFloat(value); }
    else if (MATCH("Position", "LimitY"))       { config->positionLimitY       = ParseFloat(value); }
    else if (MATCH("Position", "LimitZ"))       { config->positionLimitZ       = ParseFloat(value); }
    else if (MATCH("Position", "LimitZBack"))   { config->positionLimitZBack   = ParseFloat(value); }
    else if (MATCH("Position", "Smoothing"))    { config->positionSmoothing    = ParseFloat(value); }
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
    file << "; Rotation smoothing (0.0 = snappy, internal 0.15 floor still applied;\n";
    file << "; raise toward 1.0 for noisier trackers - costs perceived latency)\n";
    file << "RotationSmoothing=" << rotationSmoothing << "\n\n";

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
    file << "; Smoothing factor (0.0 = none, 0.99 = maximum)\n";
    file << "Smoothing=" << positionSmoothing << "\n";
    file << "; Invert position axes\n";
    file << "InvertX=" << (positionInvertX ? "true" : "false") << "\n";
    file << "InvertY=" << (positionInvertY ? "true" : "false") << "\n";
    file << "InvertZ=" << (positionInvertZ ? "true" : "false") << "\n";
    file << "; Enable/disable position tracking (6DOF)\n";
    file << "Enabled=" << (positionEnabled ? "true" : "false") << "\n\n";

    file << "[Hotkeys]\n";
    file << "; Virtual key codes (hex)\n";
    file << "ToggleKey=0x" << std::hex << toggleKey << "    ; End - Enable/disable\n";
    file << "RecenterKey=0x" << std::hex << recenterKey << "  ; Home - Recenter view\n";
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
