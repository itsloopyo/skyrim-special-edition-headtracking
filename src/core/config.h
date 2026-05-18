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

    // Rotation smoothing factor. 0.0 = minimal (a 0.15 baseline floor is still
    // applied internally - see cameraunlock-core SmoothingUtils). Push above
    // 0.0 only if your tracker is jittery; pushing higher trades crispness for
    // noise rejection.
    float rotationSmoothing = 0.0f;

    // Hotkeys (Virtual Key codes)
    int toggleKey = DEFAULT_TOGGLE_KEY;
    int recenterKey = DEFAULT_RECENTER_KEY;
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
    float positionSmoothing = 0.15f;
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
