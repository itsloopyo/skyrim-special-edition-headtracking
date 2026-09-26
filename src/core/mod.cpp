#include "pch.h"
#include "mod.h"
#include "logger.h"
#include "path_utils.h"
#include "hooks/hook_manager.h"
#include "hooks/camera_hook.h"
#include "hooks/input_hook.h"
#include "hooks/player_hook.h"
#include "hooks/hud_menu_hook.h"
#include "hooks/crosshair_override.h"
#include "hooks/projectile_hook.h"
#include "ui/notification.h"
#include "ui/crosshair_overlay.h"

#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/tracking/tracking_mode.h>

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

const char* DofModeName(cameraunlock::TrackingMode mode) {
    switch (mode) {
        case cameraunlock::TrackingMode::RotationAndPosition: return "6DOF (rotation + position)";
        case cameraunlock::TrackingMode::RotationOnly: return "3DOF rotation only";
        case cameraunlock::TrackingMode::PositionOnly: return "3DOF position only";
    }
    throw std::logic_error("tracking mode");
}

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
        Logger::Instance().Warning("Settings are not saved this session; the lines above say why");
    }

    Logger::Instance().Info("Smoothing: local=%.2f remote=%.2f", m_config.local_smoothing, m_config.remote_smoothing);

    m_worldSpaceYaw.store(m_config.world_space_yaw);
    Logger::Instance().Info("Yaw mode: %s", m_worldSpaceYaw.load() ? "horizon-locked (world)" : "camera-local");

    // The table reads a pair that names no mode as its defaults, so every loaded pair decodes.
    m_session.SetMode(cameraunlock::DecodeTrackingMode(m_config.rotation_enabled, m_config.position_enabled).value());
    m_session.GetPositionProcessor().SetSettings(m_config.position);

    // Smoothing goes in after SetSettings, which would otherwise overwrite it.
    // The session feeds both the rotation and the position processor and picks
    // between the two values per connection from the receiver's
    // IsRemoteConnection(), re-read on every Update().
    static_assert(decltype(m_session)::kHasRemoteConnection,
                  "receiver must expose IsRemoteConnection() or smoothing silently stays local");
    m_session.SetLocalSmoothing(m_config.local_smoothing);
    m_session.SetRemoteSmoothing(m_config.remote_smoothing);
    const cameraunlock::PositionSettings& limits = m_config.position;
    Logger::Instance().Info("Position processor initialized (%s, limits x=%.2f up=%.2f down=%.2f forward=%.2f back=%.2f)",
                            DofModeName(m_session.GetMode()), limits.limit_x, limits.limit_y, limits.limit_y_down,
                            limits.limit_z, limits.limit_z_back);

    // Initialize hooks
    if (!InitializeHooks()) {
        Logger::Instance().Warning("Some hooks failed to initialize - mod may have limited functionality");
    }

    m_udpReceiver.SetLog([](const std::string& msg) {
        Logger::Instance().Info("%s", msg.c_str());
    });
    if (m_udpReceiver.Start(static_cast<uint16_t>(m_config.udp_port))) {
        Logger::Instance().Info("UDP receiver started on port %d", m_config.udp_port);
    } else {
        Logger::Instance().Warning("UDP port %d is held by another process - receiver will retry in the background",
                                   m_config.udp_port);
    }

    // Set initial enabled state
    if (m_config.enable_on_startup) {
        m_enabled.store(true);
        SetCameraHookEnabled(true);
        Logger::Instance().Info("Head tracking enabled at startup");
    } else {
        m_enabled.store(false);
        SetCameraHookEnabled(false);
        Logger::Instance().Info("Head tracking disabled at startup (EnableOnStartup is off)");
    }

    m_initialized.store(true);

    Logger::Instance().Info("Initialization complete (camera:%s, input:%s)",
                            m_cameraHookInstalled ? "OK" : "FAILED",
                            m_inputHookInstalled ? "OK" : "FAILED");

    // Crosshair overlay rides the camera hook - without it, there's nothing to
    // compensate, so we skip the overlay entirely if the camera hook didn't take.
    if (m_cameraHookInstalled) {
        if (!InitializeCrosshairOverlay()) {
            Logger::Instance().Warning("Crosshair overlay failed to install - native reticle will remain at screen center");
        }
    }

    if (m_config.show_notifications) {
        std::string startupMsg = "Skyrim SE Head Tracking v";
        startupMsg += VERSION;
        startupMsg += " - ";
        startupMsg += m_enabled.load() ? "ENABLED" : "DISABLED";
        ShowNotification(startupMsg.c_str());

        std::string hotkeyHint = "Toggle: " + m_config.toggle_key_name;
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
    namespace cfg = cameraunlock::config;
    const std::wstring folder = GetModuleDirectoryW();
    if (folder.empty()) {
        // Module directory lookup failed - refuse to fall back to a CWD-relative
        // config, since that would silently read/write the wrong file.
        Logger::Instance().Error("Could not resolve module directory for CameraUnlock.ini - using built-in "
                                 "defaults, and nothing is saved this session");
        m_config = MakeConfigTable().defaults();
        return false;
    }

    cfg::ConfigOwnerOptions<Config> options = MakeConfigOwnerOptions(folder, cfg::DefaultsFile::PerUser());
    // The mod has no overlay, so a message for the player goes where every other
    // notice of this mod goes.
    options.status_sink = [](const std::string& message) { ShowNotification(message.c_str()); };
    m_configOwner.emplace(std::move(options));

    const cfg::ConfigLoadResult<Config> loaded = m_configOwner->Load();
    for (const std::string& line : loaded.log) Logger::Instance().Info("%s", line.c_str());
    Logger::Instance().Info("Config: %s", cfg::ConfigLoadStatusName(loaded.status));
    // Every status hands back the settings to run on.
    m_config = loaded.config;
    return loaded.status == cfg::ConfigLoadStatus::Canonical || loaded.status == cfg::ConfigLoadStatus::Migrated ||
           loaded.status == cfg::ConfigLoadStatus::Created;
}

void Mod::SaveConfig(const std::function<void(Config&)>& change) {
    namespace cfg = cameraunlock::config;
    if (!m_configOwner) {
        Logger::Instance().Warning("Not saved: CameraUnlock.ini has no known folder this session");
        return;
    }
    const cfg::ConfigSaveResult saved = m_configOwner->Save(change);
    for (const std::string& line : saved.log) Logger::Instance().Info("%s", line.c_str());
    if (saved.status != cfg::ConfigSaveStatus::Saved) {
        Logger::Instance().Warning("Config save %s: %s", cfg::ConfigSaveStatusName(saved.status), saved.reason.c_str());
    }
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
            if (m_config.show_notifications) {
                ShowNotification("Head Tracking: ON");
            }
        } else {
            Logger::Instance().Info("Head tracking disabled");
            if (m_config.show_notifications) {
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

    const char* name = DofModeName(mode);
    Logger::Instance().Info("DOF mode: %s", name);
    if (m_config.show_notifications) {
        std::string msg = "Mode: ";
        msg += name;
        ShowNotification(msg.c_str());
    }

    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    SaveConfig([channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
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
    if (m_config.show_notifications) {
        std::string msg = "Axis isolation: ";
        msg += name;
        ShowNotification(msg.c_str());
    }
}

void Mod::ToggleYawMode() {
    bool newValue = !m_worldSpaceYaw.load();
    m_worldSpaceYaw.store(newValue);
    Logger::Instance().Info("Yaw mode: %s", newValue ? "horizon-locked (world)" : "camera-local");
    if (m_config.show_notifications) {
        ShowNotification(newValue ? "Yaw Mode: Horizon-locked" : "Yaw Mode: Camera-local");
    }

    SaveConfig([newValue](Config& c) { c.world_space_yaw = newValue; });
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
