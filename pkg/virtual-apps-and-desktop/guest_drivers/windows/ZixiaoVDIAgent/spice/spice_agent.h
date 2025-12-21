/*
 * Zixiao VDI Agent - SPICE Agent
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "../src/common.h"

namespace zixiao::vdi {

//
// SPICE vdagent message types (from spice-protocol)
//
enum class VDAgentMessageType : uint32_t {
    MouseState = 1,
    MonitorsConfig = 2,
    Reply = 3,
    Clipboard = 4,
    DisplayConfig = 5,
    AnnounceCapabilities = 6,
    ClipboardGrab = 7,
    ClipboardRequest = 8,
    ClipboardRelease = 9,
    FileXferStart = 10,
    FileXferStatus = 11,
    FileXferData = 12,
    ClientDisconnected = 13,
    MaxClipboard = 14,
    AudioVolumeSync = 15,
    GraphicsDeviceInfo = 16,
};

//
// VirtIO-Serial port for SPICE communication
//
class VirtIOSerial {
public:
    VirtIOSerial();
    ~VirtIOSerial();

    bool Open(const std::wstring& portName);
    void Close();
    bool IsOpen() const { return handle_ != INVALID_HANDLE_VALUE; }

    bool Write(const void* data, size_t size);
    bool Read(void* buffer, size_t size, size_t& bytesRead);
    bool SetReadTimeout(uint32_t timeoutMs);

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

//
// SPICE vdagent implementation
//
class SpiceAgent : public ISubsystem {
public:
    SpiceAgent();
    ~SpiceAgent() override;

    // ISubsystem
    bool Initialize() override;
    void Shutdown() override;
    bool IsRunning() const override { return running_.load(); }
    const wchar_t* GetName() const override { return L"SpiceAgent"; }

    // Callbacks
    void SetInputCallback(InputCallback callback) { inputCallback_ = std::move(callback); }

    // Send data to host
    bool SendFrame(const FrameData& frame);
    bool SendClipboard(const ClipboardData& data);
    bool SendMonitorConfig(const std::vector<MonitorInfo>& monitors);

    // Control
    bool Start();
    void Stop();

private:
    bool FindVirtIOPort();
    void ReadLoop();
    void ProcessMessage(const std::vector<uint8_t>& data);
    void SendCapabilities();
    bool SendMessage(VDAgentMessageType type, const void* data, size_t size);

    // VirtIO-Serial port
    VirtIOSerial serial_;
    std::wstring portName_;

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::thread readThread_;
    InputCallback inputCallback_;

    // Capabilities
    uint32_t hostCapabilities_ = 0;
    uint32_t guestCapabilities_ = 0;
};

} // namespace zixiao::vdi
