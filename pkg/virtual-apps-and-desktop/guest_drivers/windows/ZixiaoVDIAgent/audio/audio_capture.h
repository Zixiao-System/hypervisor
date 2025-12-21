/*
 * Zixiao VDI Agent - Audio Capture (WASAPI)
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "../src/common.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>

namespace zixiao::vdi {

using Microsoft::WRL::ComPtr;

//
// WASAPI audio capture (loopback mode for desktop audio)
//
class AudioCapture : public ISubsystem {
public:
    AudioCapture();
    ~AudioCapture() override;

    // ISubsystem
    bool Initialize() override;
    void Shutdown() override;
    bool IsRunning() const override { return running_.load(); }
    const wchar_t* GetName() const override { return L"AudioCapture"; }

    // Configuration
    void SetAudioCallback(AudioCallback callback) { audioCallback_ = std::move(callback); }
    void SetCaptureLoopback(bool loopback) { captureLoopback_ = loopback; }

    // Control
    bool Start();
    void Stop();

    // Audio format info
    uint32_t GetSampleRate() const { return sampleRate_; }
    uint16_t GetChannels() const { return channels_; }
    uint16_t GetBitsPerSample() const { return bitsPerSample_; }

private:
    bool InitializeDevice();
    void CaptureLoop();

    // WASAPI objects
    ComPtr<IMMDeviceEnumerator> deviceEnumerator_;
    ComPtr<IMMDevice> device_;
    ComPtr<IAudioClient> audioClient_;
    ComPtr<IAudioCaptureClient> captureClient_;
    ScopedHandle audioEvent_;

    // Audio format
    uint32_t sampleRate_ = 48000;
    uint16_t channels_ = 2;
    uint16_t bitsPerSample_ = 16;
    uint32_t bufferFrameCount_ = 0;

    // Configuration
    bool captureLoopback_ = true;  // Capture desktop audio by default

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::thread captureThread_;
    AudioCallback audioCallback_;
};

} // namespace zixiao::vdi
