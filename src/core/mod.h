#pragma once

#include "config.h"
#include <cameraunlock/protocol/udp_receiver.h>
#include <cameraunlock/processing/tracking_processor.h>
#include <cameraunlock/processing/pose_interpolator.h>
#include <cameraunlock/processing/position_processor.h>
#include <cameraunlock/processing/position_interpolator.h>

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
    cameraunlock::PoseInterpolator m_poseInterpolator;
    cameraunlock::TrackingProcessor m_processor;
    int64_t m_lastReceiveTimestamp = 0;

    // Position processing (6DOF)
    cameraunlock::PositionProcessor m_positionProcessor;
    cameraunlock::PositionInterpolator m_positionInterpolator;
    // 0 = Full 6DOF, 1 = rotation only, 2 = position only. Cycled by hotkey.
    std::atomic<int> m_dofMode{0};

    // Yaw mode: true = horizon-locked (world), false = camera-local
    std::atomic<bool> m_worldSpaceYaw{true};

    // Axis isolation for diagnostic testing (0=normal, 1=pitch, 2=yaw, 3=roll)
    std::atomic<int> m_axisIsolation{0};

    // Timing for frame-rate independent processing
    uint64_t m_lastProcessTime = 0;
    float m_lastDeltaTime = 0.016f;

    // Cached rotation from last GetProcessedRotation
    float m_cachedYaw = 0.0f;
    float m_cachedPitch = 0.0f;
    float m_cachedRoll = 0.0f;
    bool m_cachedValid = false;

    bool m_hasCentered = false;
    bool m_cameraHookInstalled = false;
    bool m_inputHookInstalled = false;
    bool m_playerHookInstalled = false;
};

} // namespace SkyrimHT
