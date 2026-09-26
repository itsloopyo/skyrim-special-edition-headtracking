#include "pch.h"
#include "config.h"
#include "logger.h"

namespace SkyrimHT {

// Inline member initializers on the Config struct are the single source of truth
// for defaults. SetDefaults() resets the whole struct to its freshly-constructed state.
void Config::SetDefaults() {
    *this = Config{};
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
