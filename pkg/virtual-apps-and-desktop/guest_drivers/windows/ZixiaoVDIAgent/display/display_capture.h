/*
 * Zixiao VDI Agent - Display Capture (DXGI)
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "../src/common.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace zixiao::vdi {

using Microsoft::WRL::ComPtr;

//
// DXGI Desktop Duplication display capture
//
class DisplayCapture : public ISubsystem {
public:
    DisplayCapture();
    ~DisplayCapture() override;

    // ISubsystem
    bool Initialize() override;
    void Shutdown() override;
    bool IsRunning() const override { return running_.load(); }
    const wchar_t* GetName() const override { return L"DisplayCapture"; }

    // Configuration
    void SetTargetFps(uint32_t fps) { targetFps_ = fps; }
    void SetMonitor(uint32_t monitorIndex) { monitorIndex_ = monitorIndex; }
    void SetFrameCallback(FrameCallback callback) { frameCallback_ = std::move(callback); }

    // Control
    bool Start();
    void Stop();

    // Monitor enumeration
    std::vector<MonitorInfo> EnumerateMonitors();
    MonitorInfo GetCurrentMonitor() const;

    // Force capture a frame
    bool CaptureFrame(FrameData& frame);

private:
    bool InitializeD3D();
    bool InitializeDuplication();
    void CaptureLoop();
    bool AcquireFrame(FrameData& frame);
    void ReleaseFrame();

    // D3D11 objects
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGIOutputDuplication> duplication_;
    ComPtr<ID3D11Texture2D> stagingTexture_;

    // Configuration
    uint32_t targetFps_ = 30;
    uint32_t monitorIndex_ = 0;
    MonitorInfo monitorInfo_;

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::thread captureThread_;
    FrameCallback frameCallback_;
    uint64_t frameCount_ = 0;

    // Timing
    std::chrono::steady_clock::time_point lastFrameTime_;
    std::chrono::microseconds frameInterval_;
};

} // namespace zixiao::vdi
