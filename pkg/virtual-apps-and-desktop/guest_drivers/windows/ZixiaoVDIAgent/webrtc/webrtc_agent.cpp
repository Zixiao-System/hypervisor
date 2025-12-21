/*
 * Zixiao VDI Agent - WebRTC Agent Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Note: This is a simplified implementation. For production use,
 * integrate with libwebrtc or a similar WebRTC library.
 */

#include "webrtc_agent.h"
#include <winhttp.h>
#include <mfapi.h>
#include <mftransform.h>
#include <codecapi.h>
#include <mferror.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")

namespace zixiao::vdi {

//
// SignalingClient implementation
//
SignalingClient::SignalingClient() = default;

SignalingClient::~SignalingClient() {
    Disconnect();
}

bool SignalingClient::Connect(const std::wstring& url) {
    url_ = url;

    // Parse WebSocket URL
    URL_COMPONENTS urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = -1;
    urlComp.dwUrlPathLength = -1;

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &urlComp)) {
        LOG_ERROR(L"Failed to parse signaling URL");
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);

    // Open WinHTTP session
    HINTERNET session = WinHttpOpen(
        L"ZixiaoVDIAgent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!session) {
        LOG_ERROR(L"WinHttpOpen failed");
        return false;
    }

    // Connect
    HINTERNET connect = WinHttpConnect(
        session,
        host.c_str(),
        urlComp.nPort,
        0);

    if (!connect) {
        WinHttpCloseHandle(session);
        LOG_ERROR(L"WinHttpConnect failed");
        return false;
    }

    // Open WebSocket request
    HINTERNET request = WinHttpOpenRequest(
        connect,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        urlComp.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);

    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        LOG_ERROR(L"WinHttpOpenRequest failed");
        return false;
    }

    // Set WebSocket upgrade
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        LOG_ERROR(L"Failed to set WebSocket upgrade option");
        return false;
    }

    // Send request
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        LOG_ERROR(L"WinHttpSendRequest failed");
        return false;
    }

    // Receive response
    if (!WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        LOG_ERROR(L"WinHttpReceiveResponse failed");
        return false;
    }

    // Complete WebSocket upgrade
    websocket_ = WinHttpWebSocketCompleteUpgrade(request, 0);
    WinHttpCloseHandle(request);

    if (!websocket_) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        LOG_ERROR(L"WebSocket upgrade failed");
        return false;
    }

    connected_.store(true);
    stopping_.store(false);

    // Start receive thread
    receiveThread_ = std::thread(&SignalingClient::ReceiveLoop, this);

    LOG_INFO(L"Connected to signaling server");
    return true;
}

void SignalingClient::Disconnect() {
    stopping_.store(true);
    connected_.store(false);

    if (websocket_) {
        WinHttpWebSocketClose(websocket_, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
        WinHttpCloseHandle(websocket_);
        websocket_ = nullptr;
    }

    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
}

bool SignalingClient::Send(const std::string& message) {
    if (!websocket_ || !connected_.load()) {
        return false;
    }

    DWORD error = WinHttpWebSocketSend(
        websocket_,
        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<char*>(message.data()),
        static_cast<DWORD>(message.size()));

    return error == NO_ERROR;
}

void SignalingClient::ReceiveLoop() {
    std::vector<char> buffer(65536);

    while (!stopping_.load() && connected_.load()) {
        DWORD bytesRead = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType;

        DWORD error = WinHttpWebSocketReceive(
            websocket_,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &bytesRead,
            &bufferType);

        if (error != NO_ERROR) {
            if (!stopping_.load()) {
                LOG_ERROR(L"WebSocket receive error");
                connected_.store(false);
            }
            break;
        }

        if (bufferType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
            LOG_INFO(L"WebSocket closed by server");
            connected_.store(false);
            break;
        }

        if (bytesRead > 0 && messageCallback_) {
            std::string message(buffer.data(), bytesRead);
            messageCallback_(message);
        }
    }
}

//
// VideoEncoder implementation (using Media Foundation H.264)
//
VideoEncoder::VideoEncoder() = default;

VideoEncoder::~VideoEncoder() {
    Shutdown();
}

bool VideoEncoder::Initialize(uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate) {
    width_ = width;
    height_ = height;
    fps_ = fps;
    bitrate_ = bitrate;

    // Initialize Media Foundation
    HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"MFStartup failed: 0x%08X", hr);
        return false;
    }

    // Create H.264 encoder MFT
    // Note: This is a simplified implementation
    // In production, use MFTEnumEx to find the encoder

    LogF(LogLevel::Info, L"Video encoder initialized: %ux%u @ %u fps, %u kbps",
        width, height, fps, bitrate / 1000);

    initialized_ = true;
    return true;
}

void VideoEncoder::Shutdown() {
    if (initialized_) {
        MFShutdown();
        initialized_ = false;
    }
}

bool VideoEncoder::EncodeFrame(const FrameData& frame, std::vector<uint8_t>& encodedData) {
    if (!initialized_) {
        return false;
    }

    // For now, just pass through raw frame data
    // In production, use Media Foundation to encode to H.264

    bool isKeyFrame = forceKeyFrame_ || (frameIndex_ % 30 == 0);
    forceKeyFrame_ = false;
    lastWasKeyFrame_ = isKeyFrame;
    frameIndex_++;

    // Simple pass-through for now
    encodedData = frame.data;

    return true;
}

//
// WebRTCAgent implementation
//
WebRTCAgent::WebRTCAgent() {
    signalingUrl_ = L"ws://localhost:8080/signaling";
}

WebRTCAgent::~WebRTCAgent() {
    Shutdown();
}

bool WebRTCAgent::Initialize() {
    LOG_INFO(L"Initializing WebRTC agent...");

    // Set up signaling message handler
    signaling_.SetMessageCallback([this](const std::string& msg) {
        OnSignalingMessage(msg);
    });

    LOG_INFO(L"WebRTC agent initialized");
    return true;
}

void WebRTCAgent::Shutdown() {
    Stop();
    videoEncoder_.Shutdown();
    LOG_INFO(L"WebRTC agent shutdown");
}

bool WebRTCAgent::Start() {
    if (running_.load()) {
        return true;
    }

    LOG_INFO(L"Starting WebRTC agent...");

    // Connect to signaling server
    if (!signaling_.Connect(signalingUrl_)) {
        LOG_WARNING(L"Failed to connect to signaling server");
        // Continue anyway - might connect later
    }

    stopping_.store(false);
    running_.store(true);

    LOG_INFO(L"WebRTC agent started");
    return true;
}

void WebRTCAgent::Stop() {
    if (!running_.load()) {
        return;
    }

    LOG_INFO(L"Stopping WebRTC agent...");

    stopping_.store(true);
    running_.store(false);
    peerConnected_.store(false);

    signaling_.Disconnect();

    LOG_INFO(L"WebRTC agent stopped");
}

void WebRTCAgent::OnSignalingMessage(const std::string& message) {
    LogF(LogLevel::Debug, L"Signaling message received: %zu bytes", message.size());

    // Parse JSON message
    // Expected format: { "type": "offer|answer|ice", "payload": "..." }

    // Simple JSON parsing (in production, use nlohmann/json or similar)
    size_t typePos = message.find("\"type\"");
    size_t payloadPos = message.find("\"payload\"");

    if (typePos == std::string::npos || payloadPos == std::string::npos) {
        LOG_WARNING(L"Invalid signaling message format");
        return;
    }

    // Extract type
    size_t typeStart = message.find(':', typePos) + 1;
    size_t typeEnd = message.find(',', typeStart);
    if (typeEnd == std::string::npos) {
        typeEnd = message.find('}', typeStart);
    }

    std::string typeStr = message.substr(typeStart, typeEnd - typeStart);
    // Remove quotes and whitespace
    typeStr.erase(std::remove(typeStr.begin(), typeStr.end(), '"'), typeStr.end());
    typeStr.erase(std::remove(typeStr.begin(), typeStr.end(), ' '), typeStr.end());

    // Extract payload
    size_t payloadStart = message.find(':', payloadPos) + 1;
    size_t payloadEnd = message.rfind('}');
    std::string payload = message.substr(payloadStart, payloadEnd - payloadStart);
    // Remove outer quotes if present
    if (!payload.empty() && payload.front() == '"') {
        payload = payload.substr(1);
    }
    if (!payload.empty() && payload.back() == '"') {
        payload.pop_back();
    }

    if (typeStr == "offer") {
        HandleOffer(payload);
    } else if (typeStr == "answer") {
        HandleAnswer(payload);
    } else if (typeStr == "ice") {
        HandleIceCandidate(payload);
    } else if (typeStr == "input") {
        ProcessDataChannelMessage(std::vector<uint8_t>(payload.begin(), payload.end()));
    }
}

void WebRTCAgent::HandleOffer(const std::string& sdp) {
    LOG_INFO(L"Received SDP offer");

    // In a full implementation:
    // 1. Parse the SDP offer
    // 2. Create peer connection
    // 3. Set remote description
    // 4. Create answer
    // 5. Send answer via signaling

    // For now, just acknowledge
    peerConnected_.store(true);

    // Initialize video encoder with default settings
    if (!videoEncoder_.Initialize(1920, 1080, 30, 4000000)) {
        LOG_WARNING(L"Failed to initialize video encoder");
    }

    // Send a simple answer
    std::string answer = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n";
    SendSignalingMessage("answer", answer);
}

void WebRTCAgent::HandleAnswer(const std::string& sdp) {
    LOG_INFO(L"Received SDP answer");
    (void)sdp;

    peerConnected_.store(true);

    // Process any pending ICE candidates
    std::lock_guard lock(iceMutex_);
    for (const auto& candidate : pendingIceCandidates_) {
        // Add ICE candidate to peer connection
        (void)candidate;
    }
    pendingIceCandidates_.clear();
}

void WebRTCAgent::HandleIceCandidate(const std::string& candidate) {
    LogF(LogLevel::Debug, L"Received ICE candidate");

    std::lock_guard lock(iceMutex_);

    if (!peerConnected_.load()) {
        // Queue for later
        pendingIceCandidates_.push_back(candidate);
    } else {
        // Add to peer connection
        (void)candidate;
    }
}

bool WebRTCAgent::SendSignalingMessage(const std::string& type, const std::string& payload) {
    // Simple JSON formatting
    std::string message = "{\"type\":\"" + type + "\",\"payload\":\"" + payload + "\"}";
    return signaling_.Send(message);
}

void WebRTCAgent::ProcessDataChannelMessage(const std::vector<uint8_t>& data) {
    if (data.empty() || !inputCallback_) {
        return;
    }

    // Parse input event from data channel
    // Expected format: type(1) + data(variable)

    InputEvent event;

    switch (data[0]) {
        case 1: // Mouse move
            if (data.size() >= 9) {
                event.type = InputEventType::MouseMove;
                event.mouse.x = *reinterpret_cast<const int32_t*>(&data[1]);
                event.mouse.y = *reinterpret_cast<const int32_t*>(&data[5]);
                inputCallback_(event);
            }
            break;

        case 2: // Mouse button
            if (data.size() >= 3) {
                event.type = InputEventType::MouseButton;
                event.mouse.button = data[1];
                event.mouse.pressed = data[2] != 0;
                inputCallback_(event);
            }
            break;

        case 3: // Key
            if (data.size() >= 6) {
                event.type = data[5] ? InputEventType::KeyDown : InputEventType::KeyUp;
                event.key.scanCode = *reinterpret_cast<const uint16_t*>(&data[1]);
                event.key.virtualKey = *reinterpret_cast<const uint16_t*>(&data[3]);
                event.key.pressed = data[5] != 0;
                inputCallback_(event);
            }
            break;

        case 4: // Mouse wheel
            if (data.size() >= 5) {
                event.type = InputEventType::MouseWheel;
                event.mouse.wheelDelta = *reinterpret_cast<const int32_t*>(&data[1]);
                inputCallback_(event);
            }
            break;

        default:
            break;
    }
}

bool WebRTCAgent::SendFrame(const FrameData& frame) {
    if (!running_.load() || !peerConnected_.load()) {
        return false;
    }

    // Encode frame
    std::vector<uint8_t> encodedData;
    if (!videoEncoder_.EncodeFrame(frame, encodedData)) {
        return false;
    }

    // In a full implementation, send via RTP over the peer connection
    // For now, this is a placeholder

    return true;
}

bool WebRTCAgent::SendAudio(const AudioData& audio) {
    if (!running_.load() || !peerConnected_.load()) {
        return false;
    }

    // In a full implementation:
    // 1. Encode audio (Opus)
    // 2. Send via RTP

    (void)audio;
    return true;
}

} // namespace zixiao::vdi
