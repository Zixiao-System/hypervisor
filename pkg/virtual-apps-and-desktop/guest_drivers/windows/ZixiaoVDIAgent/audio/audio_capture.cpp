/*
 * Zixiao VDI Agent - Audio Capture Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "audio_capture.h"
#include <functiondiscoverykeys_devpkey.h>

#pragma comment(lib, "ole32.lib")

namespace zixiao::vdi {

// WASAPI buffer duration in 100-nanosecond units (20ms)
static const REFERENCE_TIME BUFFER_DURATION = 200000;

AudioCapture::AudioCapture() = default;

AudioCapture::~AudioCapture() {
    Shutdown();
}

bool AudioCapture::Initialize() {
    LOG_INFO(L"Initializing audio capture...");

    if (!InitializeDevice()) {
        LOG_ERROR(L"Failed to initialize audio device");
        return false;
    }

    LOG_INFO(L"Audio capture initialized");
    return true;
}

bool AudioCapture::InitializeDevice() {
    // Create device enumerator
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(deviceEnumerator_.GetAddressOf()));

    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to create device enumerator: 0x%08X", hr);
        return false;
    }

    // Get default audio endpoint
    EDataFlow dataFlow = captureLoopback_ ? eRender : eCapture;
    hr = deviceEnumerator_->GetDefaultAudioEndpoint(dataFlow, eConsole, &device_);
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to get default audio endpoint: 0x%08X", hr);
        return false;
    }

    // Log device name
    ComPtr<IPropertyStore> props;
    if (SUCCEEDED(device_->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT varName;
        PropVariantInit(&varName);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName))) {
            LogF(LogLevel::Info, L"Audio device: %s", varName.pwszVal);
            PropVariantClear(&varName);
        }
    }

    // Activate audio client
    hr = device_->Activate(
        __uuidof(IAudioClient),
        CLSCTX_ALL,
        nullptr,
        reinterpret_cast<void**>(audioClient_.GetAddressOf()));

    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to activate audio client: 0x%08X", hr);
        return false;
    }

    // Get mix format
    WAVEFORMATEX* mixFormat = nullptr;
    hr = audioClient_->GetMixFormat(&mixFormat);
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to get mix format: 0x%08X", hr);
        return false;
    }

    sampleRate_ = mixFormat->nSamplesPerSec;
    channels_ = mixFormat->nChannels;
    bitsPerSample_ = mixFormat->wBitsPerSample;

    LogF(LogLevel::Info, L"Audio format: %uHz, %u channels, %u bits",
        sampleRate_, channels_, bitsPerSample_);

    // Initialize audio client
    DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (captureLoopback_) {
        streamFlags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    }

    hr = audioClient_->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        BUFFER_DURATION,
        0,
        mixFormat,
        nullptr);

    CoTaskMemFree(mixFormat);

    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to initialize audio client: 0x%08X", hr);
        return false;
    }

    // Get buffer size
    hr = audioClient_->GetBufferSize(&bufferFrameCount_);
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to get buffer size: 0x%08X", hr);
        return false;
    }

    // Create event for audio capture
    audioEvent_.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!audioEvent_) {
        LOG_ERROR(L"Failed to create audio event");
        return false;
    }

    hr = audioClient_->SetEventHandle(audioEvent_.Get());
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to set event handle: 0x%08X", hr);
        return false;
    }

    // Get capture client
    hr = audioClient_->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(captureClient_.GetAddressOf()));

    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to get capture client: 0x%08X", hr);
        return false;
    }

    return true;
}

void AudioCapture::Shutdown() {
    Stop();

    captureClient_.Reset();
    audioClient_.Reset();
    device_.Reset();
    deviceEnumerator_.Reset();

    LOG_INFO(L"Audio capture shutdown");
}

bool AudioCapture::Start() {
    if (running_.load()) {
        return true;
    }

    if (!audioClient_) {
        LOG_ERROR(L"Audio client not initialized");
        return false;
    }

    LOG_INFO(L"Starting audio capture...");

    // Start audio client
    HRESULT hr = audioClient_->Start();
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to start audio client: 0x%08X", hr);
        return false;
    }

    stopping_.store(false);
    running_.store(true);

    captureThread_ = std::thread(&AudioCapture::CaptureLoop, this);

    LOG_INFO(L"Audio capture started");
    return true;
}

void AudioCapture::Stop() {
    if (!running_.load()) {
        return;
    }

    LOG_INFO(L"Stopping audio capture...");

    stopping_.store(true);
    running_.store(false);

    // Signal event to wake capture thread
    SetEvent(audioEvent_.Get());

    if (captureThread_.joinable()) {
        captureThread_.join();
    }

    if (audioClient_) {
        audioClient_->Stop();
    }

    LOG_INFO(L"Audio capture stopped");
}

void AudioCapture::CaptureLoop() {
    while (!stopping_.load()) {
        // Wait for audio data
        DWORD waitResult = WaitForSingleObject(audioEvent_.Get(), 100);

        if (stopping_.load()) {
            break;
        }

        if (waitResult != WAIT_OBJECT_0) {
            continue;
        }

        // Get available frames
        UINT32 packetLength = 0;
        HRESULT hr = captureClient_->GetNextPacketSize(&packetLength);
        if (FAILED(hr)) {
            continue;
        }

        while (packetLength > 0) {
            BYTE* data = nullptr;
            UINT32 numFramesAvailable = 0;
            DWORD flags = 0;

            hr = captureClient_->GetBuffer(&data, &numFramesAvailable, &flags, nullptr, nullptr);
            if (FAILED(hr)) {
                break;
            }

            if (numFramesAvailable > 0 && audioCallback_) {
                AudioData audioData;
                audioData.sampleRate = sampleRate_;
                audioData.channels = channels_;
                audioData.bitsPerSample = bitsPerSample_;
                audioData.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();

                size_t dataSize = numFramesAvailable * channels_ * (bitsPerSample_ / 8);

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    // Insert silence
                    audioData.data.resize(dataSize, 0);
                } else {
                    audioData.data.resize(dataSize);
                    memcpy(audioData.data.data(), data, dataSize);
                }

                audioCallback_(audioData);
            }

            captureClient_->ReleaseBuffer(numFramesAvailable);

            hr = captureClient_->GetNextPacketSize(&packetLength);
            if (FAILED(hr)) {
                break;
            }
        }
    }
}

} // namespace zixiao::vdi
