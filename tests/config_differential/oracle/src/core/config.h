#pragma once

#include <cstdint>

namespace SkyrimHT {

struct Config {
    // Network settings
    uint16_t udpPort = DEFAULT_UDP_PORT;

    // Sensitivity multipliers
    float yawMultiplier = 1.0f;
    float pitchMultiplier = 1.0f;
    float rollMultiplier = 1.0f;

    // Smoothing. The value used is picked per connection from the packet source
    // address: a tracker on this machine (loopback) uses localSmoothing, a
    // remote network device uses remoteSmoothing. Both cover rotation and
    // position. 0.0 = none, 1.0 = heavy.
    float localSmoothing = 0.0f;
    float remoteSmoothing = 0.15f;

    // Hotkeys (Virtual Key codes)
    int toggleKey = DEFAULT_TOGGLE_KEY;
    int positionToggleKey = DEFAULT_POSITION_TOGGLE_KEY;
    int yawModeKey = DEFAULT_YAW_MODE_KEY;

    // Position settings (6DOF)
    float positionSensitivityX = 1.0f;
    float positionSensitivityY = 1.0f;
    float positionSensitivityZ = 1.0f;
    float positionLimitX = 0.30f;
    float positionLimitY = 0.20f;
    float positionLimitZ = 0.40f;
    float positionLimitZBack = 0.10f;
    bool positionInvertX = true;
    bool positionInvertY = false;
    bool positionInvertZ = true;
    bool positionEnabled = true;

    // General settings
    bool autoEnable = true;
    bool showNotifications = true;
    bool worldSpaceYaw = true;

    // Crosshair overlay
    // showCrosshair = false keeps the game's center reticle as-is.
    bool showCrosshair = true;

    // Load/Save
    bool Load(const char* path);
    bool Save(const char* path) const;
    void SetDefaults();
    void Validate();

private:
    static int ConfigHandler(void* user, const char* section, const char* name, const char* value);
};

} // namespace SkyrimHT
