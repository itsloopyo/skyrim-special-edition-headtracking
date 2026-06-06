#pragma once

#include "config.h"
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/tracking/head_tracking_session.h>

namespace SkyrimHT {

class Mod {
public:
    static Mod& Instance();

    bool Initialize();
    void Shutdown();

    bool IsEnabled() const { return m_enabled.load(); }
    void SetEnabled(bool enabled);
    void Toggle();

    void Recenter();
    void CycleDofMode();
    void ToggleYawMode();
    bool IsWorldSpaceYaw() const { return m_worldSpaceYaw.load(); }

    // F8 cycles axis isolation for diagnostic testing.
    // 0 = normal, 1 = pitch-only, 2 = yaw-only, 3 = roll-only.
    void CycleAxisIsolation();
    int  GetAxisIsolation() const { return m_axisIsolation.load(); }

    void DumpMatrices();

    Config& GetConfig() { return m_config; }
    const Config& GetConfig() const { return m_config; }

    // Get processed (smoothed) rotation values for rendering
    bool GetProcessedRotation(float& yaw, float& pitch, float& roll);

    // Get processed position offset (meters)
    bool GetPositionOffset(float& x, float& y, float& z);

    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

private:
    Mod() = default;
    ~Mod() = default;

    bool LoadConfig();
    bool InitializeHooks();
    void ShutdownHooks();

    std::atomic<bool> m_enabled{false};
    std::atomic<bool> m_initialized{false};

    Config m_config;
    cameraunlock::UdpReceiver m_udpReceiver;
    // Shared per-frame pipeline (interpolation, processing, 6DOF, mode cycling,
    // stabilized auto-recenter). Updated at most once per cache window from
    // GetProcessedRotation; hotkey-thread calls (Recenter/CycleDofMode) follow
    // the session's documented threading model.
    cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver> m_session{m_udpReceiver};

    // Yaw mode: true = horizon-locked (world), false = camera-local
    std::atomic<bool> m_worldSpaceYaw{true};

    // Axis isolation for diagnostic testing (0=normal, 1=pitch, 2=yaw, 3=roll)
    std::atomic<int> m_axisIsolation{0};

    // Timing for frame-rate independent processing
    uint64_t m_lastProcessTime = 0;

    bool m_cameraHookInstalled = false;
    bool m_inputHookInstalled = false;
    bool m_playerHookInstalled = false;
};

} // namespace SkyrimHT
