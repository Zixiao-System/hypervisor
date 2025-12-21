/*
 * Zixiao VDI Agent - Service Framework
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "common.h"
#include <memory>
#include <vector>

namespace zixiao::vdi {

// Forward declarations
class DisplayCapture;
class AudioCapture;
class InputHandler;
class ClipboardManager;
class SpiceAgent;
class WebRTCAgent;

//
// VDI Agent Service
//
class VDIService {
public:
    static VDIService& GetInstance();

    // Service control
    bool Initialize();
    void Run();
    void Stop();
    bool IsRunning() const { return running_.load(); }

    // Service control handler
    void HandleControl(DWORD control);
    void SetServiceStatus(DWORD currentState, DWORD exitCode = NO_ERROR, DWORD waitHint = 0);

    // Subsystem access
    DisplayCapture* GetDisplayCapture() { return displayCapture_.get(); }
    AudioCapture* GetAudioCapture() { return audioCapture_.get(); }
    InputHandler* GetInputHandler() { return inputHandler_.get(); }
    ClipboardManager* GetClipboardManager() { return clipboardManager_.get(); }
    SpiceAgent* GetSpiceAgent() { return spiceAgent_.get(); }
    WebRTCAgent* GetWebRTCAgent() { return webrtcAgent_.get(); }

    // Configuration
    bool IsSpiceEnabled() const { return spiceEnabled_; }
    bool IsWebRTCEnabled() const { return webrtcEnabled_; }
    void SetSpiceEnabled(bool enabled) { spiceEnabled_ = enabled; }
    void SetWebRTCEnabled(bool enabled) { webrtcEnabled_ = enabled; }

private:
    VDIService();
    ~VDIService();

    VDIService(const VDIService&) = delete;
    VDIService& operator=(const VDIService&) = delete;

    bool InitializeSubsystems();
    void ShutdownSubsystems();
    void MainLoop();

    // Service status
    SERVICE_STATUS_HANDLE statusHandle_ = nullptr;
    SERVICE_STATUS serviceStatus_ = {};
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};

    // Configuration
    bool spiceEnabled_ = true;
    bool webrtcEnabled_ = true;

    // Subsystems
    std::unique_ptr<DisplayCapture> displayCapture_;
    std::unique_ptr<AudioCapture> audioCapture_;
    std::unique_ptr<InputHandler> inputHandler_;
    std::unique_ptr<ClipboardManager> clipboardManager_;
    std::unique_ptr<SpiceAgent> spiceAgent_;
    std::unique_ptr<WebRTCAgent> webrtcAgent_;

    // Events
    ScopedHandle stopEvent_;
};

//
// Service entry points (called by SCM)
//
void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
void WINAPI ServiceCtrlHandler(DWORD control);

//
// Service management utilities
//
bool InstallService();
bool UninstallService();
bool StartServiceManual();
bool StopServiceManual();
void RunAsConsole();

} // namespace zixiao::vdi
