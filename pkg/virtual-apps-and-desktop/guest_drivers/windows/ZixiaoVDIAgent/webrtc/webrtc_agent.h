/*
 * Zixiao VDI Agent - WebRTC Agent
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "../src/common.h"
#include <nlohmann/json_fwd.hpp>

namespace zixiao::vdi {

//
// WebSocket signaling client
//
class SignalingClient {
public:
    SignalingClient();
    ~SignalingClient();

    // Connection
    bool Connect(const std::wstring& url);
    void Disconnect();
    bool IsConnected() const { return connected_.load(); }

    // Messaging
    bool Send(const std::string& message);
    void SetMessageCallback(std::function<void(const std::string&)> callback) {
        messageCallback_ = std::move(callback);
    }

private:
    void ReceiveLoop();

    HANDLE websocket_ = nullptr;
    std::wstring url_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopping_{false};
    std::thread receiveThread_;
    std::function<void(const std::string&)> messageCallback_;
};

//
// Video encoder using Media Foundation
//
class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    bool Initialize(uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate);
    void Shutdown();

    bool EncodeFrame(const FrameData& frame, std::vector<uint8_t>& encodedData);

    bool IsKeyFrame() const { return lastWasKeyFrame_; }
    void ForceKeyFrame() { forceKeyFrame_ = true; }

private:
    // Media Foundation objects (opaque pointers)
    void* transform_ = nullptr;
    void* inputType_ = nullptr;
    void* outputType_ = nullptr;

    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t fps_ = 30;
    uint32_t bitrate_ = 2000000;

    bool initialized_ = false;
    bool forceKeyFrame_ = false;
    bool lastWasKeyFrame_ = false;
    uint64_t frameIndex_ = 0;
};

//
// WebRTC peer connection (simplified implementation)
//
class WebRTCAgent : public ISubsystem {
public:
    WebRTCAgent();
    ~WebRTCAgent() override;

    // ISubsystem
    bool Initialize() override;
    void Shutdown() override;
    bool IsRunning() const override { return running_.load(); }
    const wchar_t* GetName() const override { return L"WebRTCAgent"; }

    // Configuration
    void SetSignalingUrl(const std::wstring& url) { signalingUrl_ = url; }
    void SetInputCallback(InputCallback callback) { inputCallback_ = std::move(callback); }

    // Send data to remote
    bool SendFrame(const FrameData& frame);
    bool SendAudio(const AudioData& audio);

    // Control
    bool Start();
    void Stop();

    // Connection state
    bool IsConnected() const { return peerConnected_.load(); }

private:
    void OnSignalingMessage(const std::string& message);
    void HandleOffer(const std::string& sdp);
    void HandleAnswer(const std::string& sdp);
    void HandleIceCandidate(const std::string& candidate);
    bool SendSignalingMessage(const std::string& type, const std::string& payload);
    void ProcessDataChannelMessage(const std::vector<uint8_t>& data);

    // Signaling
    SignalingClient signaling_;
    std::wstring signalingUrl_;

    // Video encoding
    VideoEncoder videoEncoder_;

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> peerConnected_{false};
    InputCallback inputCallback_;

    // ICE candidates queue (before peer connection established)
    std::vector<std::string> pendingIceCandidates_;
    std::mutex iceMutex_;
};

} // namespace zixiao::vdi
