#include "pch.h"
#include "mod.h"
#include "logger.h"
#include "path_utils.h"
#include "hotkey_utils.h"
#include "hooks/hook_manager.h"
#include "hooks/camera_hook.h"
#include "hooks/input_hook.h"
#include "hooks/player_hook.h"
#include "hooks/hud_menu_hook.h"
#include "hooks/crosshair_override.h"
#include "hooks/projectile_hook.h"
#include "ui/notification.h"
#include "ui/crosshair_overlay.h"

namespace SkyrimHT {

namespace {

// Per-frame cache window - if GetProcessedRotation is called twice within this
// interval (e.g. when PlayerCamera::Update fires multiple times per frame for
// shadow/reflection cameras) the second call returns the cached result.
constexpr uint64_t kProcessCacheWindowMicros = 1000;

// Delta-time fallback and clamps. Clamps prevent huge dt after a load/stutter
// from slamming the smoothing filter, and tiny dt from numerical explosion.
constexpr float kDefaultDeltaTime = 0.016f;  // assume ~60Hz if no prior sample
constexpr float kMinDeltaTime     = 0.0001f;
constexpr float kMaxDeltaTime     = 0.1f;

uint64_t GetTimeMicros() {
    static LARGE_INTEGER freq = {};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // QuadPart * 1000000 overflows int64 after ~10 days of system uptime
    // (10 MHz QPC). Split into whole-second and remainder terms so the
    // product never exceeds 64 bits.
    const uint64_t q = static_cast<uint64_t>(now.QuadPart);
    const uint64_t f = static_cast<uint64_t>(freq.QuadPart);
    return (q / f) * 1000000ULL + ((q % f) * 1000000ULL) / f;
}

} // namespace

Mod& Mod::Instance() {
    static Mod instance;
    return instance;
}

bool Mod::Initialize() {
    if (m_initialized.load()) {
        Logger::Instance().Warning("Mod already initialized");
        return true;
    }

    Logger::Instance().Info("Skyrim SE Head Tracking v%s initializing...", VERSION);

    if (!LoadConfig()) {
        Logger::Instance().Warning("Using default configuration");
    }

    // Initialize TrackingProcessor with sensitivity settings
    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = m_config.yawMultiplier;
    sensitivity.pitch = m_config.pitchMultiplier;
    sensitivity.roll = m_config.rollMultiplier;
    m_session.GetProcessor().SetSensitivity(sensitivity);

    Logger::Instance().Info("TrackingProcessor initialized with sensitivity: yaw=%.2f pitch=%.2f roll=%.2f smoothing=local %.2f/remote %.2f",
                            sensitivity.yaw, sensitivity.pitch, sensitivity.roll,
                            m_config.localSmoothing, m_config.remoteSmoothing);

    // Initialize yaw mode from config
    m_worldSpaceYaw.store(m_config.worldSpaceYaw);
    Logger::Instance().Info("Yaw mode: %s", m_worldSpaceYaw.load() ? "horizon-locked (world)" : "camera-local");

    // Initialize position processor. Tracking mode seeds from the legacy
    // positionEnabled config: true -> Full 6DOF, false -> rotation only.
    // Position-only is reachable from either start via the cycle hotkey.
    m_session.SetMode(m_config.positionEnabled
        ? cameraunlock::TrackingMode::RotationAndPosition
        : cameraunlock::TrackingMode::RotationOnly);
    // Assigned by name rather than through the positional constructor: the
    // argument list is long enough that a field added or removed upstream would
    // silently rebind the limits to the wrong slots.
    cameraunlock::PositionSettings posSettings;
    posSettings.sensitivity_x = m_config.positionSensitivityX;
    posSettings.sensitivity_y = m_config.positionSensitivityY;
    posSettings.sensitivity_z = m_config.positionSensitivityZ;
    posSettings.limit_x       = m_config.positionLimitX;
    posSettings.limit_y       = m_config.positionLimitY;
    posSettings.limit_z       = m_config.positionLimitZ;
    posSettings.limit_z_back  = m_config.positionLimitZBack;
    posSettings.invert_x      = m_config.positionInvertX;
    posSettings.invert_y      = m_config.positionInvertY;
    posSettings.invert_z      = m_config.positionInvertZ;
    m_session.GetPositionProcessor().SetSettings(posSettings);

    // Smoothing goes in after SetSettings, which would otherwise overwrite it.
    // The session feeds both the rotation and the position processor and picks
    // between the two values per connection from the receiver's
    // IsRemoteConnection(), re-read on every Update().
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() or smoothing silently stays local");
    m_session.SetLocalSmoothing(m_config.localSmoothing);
    m_session.SetRemoteSmoothing(m_config.remoteSmoothing);
    Logger::Instance().Info("Position processor initialized (%s, sens=%.1f/%.1f/%.1f, limits=%.2f/%.2f/%.2f)",
                            m_session.GetMode() == cameraunlock::TrackingMode::RotationAndPosition ? "6DOF" : "3DOF rotation",
                            posSettings.sensitivity_x, posSettings.sensitivity_y, posSettings.sensitivity_z,
                            posSettings.limit_x, posSettings.limit_y, posSettings.limit_z);

    // Initialize hooks
    if (!InitializeHooks()) {
        Logger::Instance().Warning("Some hooks failed to initialize - mod may have limited functionality");
    }

    m_udpReceiver.SetLog([](const std::string& msg) {
        Logger::Instance().Info("%s", msg.c_str());
    });
    if (m_udpReceiver.Start(m_config.udpPort)) {
        Logger::Instance().Info("UDP receiver started on port %d", m_config.udpPort);
    } else {
        Logger::Instance().Warning("UDP port %d is held by another process - receiver will retry in the background",
                                   m_config.udpPort);
    }

    // Set initial enabled state
    if (m_config.autoEnable) {
        m_enabled.store(true);
        SetCameraHookEnabled(true);
        Logger::Instance().Info("Head tracking auto-enabled at startup");
    } else {
        m_enabled.store(false);
        SetCameraHookEnabled(false);
        Logger::Instance().Info("Head tracking disabled at startup (auto-enable is off)");
    }

    m_initialized.store(true);

    Logger::Instance().Info("Initialization complete (camera:%s, input:%s)",
                            m_cameraHookInstalled ? "OK" : "FAILED",
                            m_inputHookInstalled ? "OK" : "FAILED");

    Logger::Instance().Info("Hotkeys: %s=Toggle", VirtualKeyToString(m_config.toggleKey));

    // Crosshair overlay rides the camera hook - without it, there's nothing to
    // compensate, so we skip the overlay entirely if the camera hook didn't take.
    if (m_config.showCrosshair && m_cameraHookInstalled) {
        if (!InitializeCrosshairOverlay()) {
            Logger::Instance().Warning("Crosshair overlay failed to install - native reticle will remain at screen center");
        }
    }

    if (m_config.showNotifications) {
        std::string startupMsg = "Skyrim SE Head Tracking v";
        startupMsg += VERSION;
        startupMsg += " - ";
        startupMsg += m_enabled.load() ? "ENABLED" : "DISABLED";
        ShowNotification(startupMsg.c_str());

        std::string hotkeyHint = VirtualKeyToString(m_config.toggleKey);
        hotkeyHint += "=Toggle";
        ShowNotification(hotkeyHint.c_str());
    }

    return true;
}

void Mod::Shutdown() {
    if (!m_initialized.load()) {
        return;
    }

    Logger::Instance().Info("Shutting down...");
    ShutdownCrosshairOverlay();
    RemoveProjectileHook();
    RemoveHUDMenuHook();
    m_udpReceiver.Stop();
    ShutdownHooks();
    m_initialized.store(false);
    Logger::Instance().Info("Shutdown complete");
}

bool Mod::LoadConfig() {
    std::string configPath = GetModulePath("HeadTracking.ini");
    if (configPath.empty()) {
        // Module directory lookup failed - refuse to fall back to a CWD-relative
        // config, since that would silently read/write the wrong file.
        Logger::Instance().Error("Could not resolve module directory for HeadTracking.ini - using built-in defaults");
        m_config.SetDefaults();
        return false;
    }

    if (!m_config.Load(configPath.c_str())) {
        m_config.SetDefaults();
        m_config.Save(configPath.c_str());
        return false;
    }

    return true;
}

bool Mod::InitializeHooks() {
    if (!HookManager::Instance().Initialize()) {
        Logger::Instance().Error("MinHook initialization failed");
        return false;
    }

    if (!InstallCameraHook()) {
        Logger::Instance().Warning("Camera hook failed - head tracking disabled");
        m_cameraHookInstalled = false;
    } else {
        m_cameraHookInstalled = true;
        Logger::Instance().Info("Camera hook installed");
    }

    if (!InstallInputHook()) {
        Logger::Instance().Warning("Input hook failed - hotkeys won't work");
        m_inputHookInstalled = false;
    } else {
        m_inputHookInstalled = true;
        Logger::Instance().Info("Input hook installed");
    }

    // Player hook depends on cameraRoot capture from the camera hook,
    // so only install if the camera hook took. Without it, interaction
    // raycasts will follow the head-tracked direction.
    if (m_cameraHookInstalled) {
        if (!InstallPlayerHook()) {
            Logger::Instance().Warning("Player hook failed - interaction will follow head, not body-aim");
            m_playerHookInstalled = false;
        } else {
            m_playerHookInstalled = true;
        }
    }

    if (!InstallHUDMenuHook()) {
        Logger::Instance().Warning("HUDMenu hook failed - native crosshair control unavailable");
    }

    if (InitializeCrosshairOverride()) {
        InstallUpdateCrosshairsHook();
    }

    InstallProjectileHook();

    if (!HookManager::Instance().EnableAllHooks()) {
        Logger::Instance().Warning("Failed to enable some hooks");
    }

    return m_inputHookInstalled;
}

void Mod::ShutdownHooks() {
    if (m_playerHookInstalled) {
        RemovePlayerHook();
        m_playerHookInstalled = false;
    }

    if (m_inputHookInstalled) {
        RemoveInputHook();
        m_inputHookInstalled = false;
    }

    if (m_cameraHookInstalled) {
        RemoveCameraHook();
        m_cameraHookInstalled = false;
    }

    HookManager::Instance().Shutdown();
}

void Mod::SetEnabled(bool enabled) {
    bool wasEnabled = m_enabled.exchange(enabled);
    if (wasEnabled != enabled) {
        SetCameraHookEnabled(enabled);

        if (enabled) {
            Logger::Instance().Info("Head tracking enabled");
            if (m_config.showNotifications) {
                ShowNotification("Head Tracking: ON");
            }
        } else {
            Logger::Instance().Info("Head tracking disabled");
            if (m_config.showNotifications) {
                ShowNotification("Head Tracking: OFF");
            }
        }
    }
}

void Mod::Toggle() {
    SetEnabled(!m_enabled.load());
}

void Mod::DumpMatrices() {
    CameraRootSnapshots snap;
    if (!GetCameraRootSnapshots(snap) || snap.niCamera == 0) {
        Logger::Instance().Info("DumpMatrices: no snapshot available");
        return;
    }
    auto& L = Logger::Instance();
    L.Info("=== MATRIX DUMP ===");
    L.Info("cleanWorld (cameraRoot):");
    for (int i = 0; i < 3; ++i)
        L.Info("  [%+.4f %+.4f %+.4f]",
            snap.cleanWorld[i][0], snap.cleanWorld[i][1], snap.cleanWorld[i][2]);
    L.Info("trackedWorld (cameraRoot):");
    for (int i = 0; i < 3; ++i)
        L.Info("  [%+.4f %+.4f %+.4f]",
            snap.trackedWorld[i][0], snap.trackedWorld[i][1], snap.trackedWorld[i][2]);
    L.Info("cleanNiCamWorld:");
    for (int i = 0; i < 3; ++i)
        L.Info("  [%+.4f %+.4f %+.4f]",
            snap.cleanNiCamWorld[i][0], snap.cleanNiCamWorld[i][1], snap.cleanNiCamWorld[i][2]);
    L.Info("trackedNiCamWorld:");
    for (int i = 0; i < 3; ++i)
        L.Info("  [%+.4f %+.4f %+.4f]",
            snap.trackedNiCamWorld[i][0], snap.trackedNiCamWorld[i][1], snap.trackedNiCamWorld[i][2]);
    float ot_y, ot_p, ot_r;
    if (GetProcessedRotation(ot_y, ot_p, ot_r)) {
        L.Info("OpenTrack yaw=%+.3f pitch=%+.3f roll=%+.3f (deg)", ot_y, ot_p, ot_r);
    }
    L.Info("YawMode: %s", IsWorldSpaceYaw() ? "WORLD" : "LOCAL");
    L.Info("=== END MATRIX DUMP ===");
}

void Mod::CycleDofMode() {
    const cameraunlock::TrackingMode mode = m_session.CycleMode();

    const char* name =
        mode == cameraunlock::TrackingMode::RotationAndPosition ? "6DOF (rotation + position)" :
        mode == cameraunlock::TrackingMode::RotationOnly ? "3DOF rotation only" :
        "3DOF position only";
    Logger::Instance().Info("DOF mode: %s", name);
    if (m_config.showNotifications) {
        std::string msg = "Mode: ";
        msg += name;
        ShowNotification(msg.c_str());
    }
}

void Mod::CycleAxisIsolation() {
    const int cur = m_axisIsolation.load();
    const int next = (cur + 1) % 4;
    m_axisIsolation.store(next);
    const char* name =
        next == 0 ? "normal" :
        next == 1 ? "PITCH only" :
        next == 2 ? "YAW only" :
        "ROLL only";
    Logger::Instance().Info("Axis isolation: %s", name);
    if (m_config.showNotifications) {
        std::string msg = "Axis isolation: ";
        msg += name;
        ShowNotification(msg.c_str());
    }
}

void Mod::ToggleYawMode() {
    bool newValue = !m_worldSpaceYaw.load();
    m_worldSpaceYaw.store(newValue);
    Logger::Instance().Info("Yaw mode: %s", newValue ? "horizon-locked (world)" : "camera-local");
    if (m_config.showNotifications) {
        ShowNotification(newValue ? "Yaw Mode: Horizon-locked" : "Yaw Mode: Camera-local");
    }
}

void Mod::LogFirstTrackerSample() {
    if (m_loggedFirstSample) return;

    // The RAW receiver sample, not the session output: centering, deadzone,
    // smoothing and sensitivity can all render the processed value 0/0/0 on the
    // settle frame, which reads as a tracker sending zeros.
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    if (!m_udpReceiver.GetRotation(yaw, pitch, roll)) return;

    m_loggedFirstSample = true;
    Logger::Instance().Info("First tracker sample received: raw yaw=%.2f pitch=%.2f roll=%.2f (connection is %s)",
                            yaw, pitch, roll,
                            m_udpReceiver.IsRemoteConnection() ? "remote" : "local");
}

bool Mod::GetProcessedRotation(float& yaw, float& pitch, float& roll) {
    // Run the shared pipeline at most once per cache window. PlayerCamera::Update
    // can fire multiple times per frame (shadow/reflection cameras); calls inside
    // the window read the session's cached outputs.
    const uint64_t now = GetTimeMicros();
    if (m_lastProcessTime == 0 || (now - m_lastProcessTime) >= kProcessCacheWindowMicros) {
        float deltaTime = kDefaultDeltaTime;
        if (m_lastProcessTime > 0) {
            deltaTime = (now - m_lastProcessTime) / 1000000.0f;
            if (deltaTime > kMaxDeltaTime) deltaTime = kMaxDeltaTime;
            if (deltaTime < kMinDeltaTime) deltaTime = kMinDeltaTime;
        }
        m_lastProcessTime = now;
        m_session.Update(deltaTime);
    }

    if (!m_session.GetRotation(yaw, pitch, roll)) {
        return false;
    }

    switch (m_axisIsolation.load(std::memory_order_relaxed)) {
        case 1: yaw = 0.0f; roll = 0.0f; break;
        case 2: pitch = 0.0f; roll = 0.0f; break;
        case 3: yaw = 0.0f; pitch = 0.0f; break;
        default: break;
    }

    return true;
}

bool Mod::GetPositionOffset(float& x, float& y, float& z) {
    return m_session.GetPositionOffset(x, y, z);
}

} // namespace SkyrimHT
