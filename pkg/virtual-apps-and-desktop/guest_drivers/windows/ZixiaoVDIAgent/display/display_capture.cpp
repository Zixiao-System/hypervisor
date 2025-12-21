/*
 * Zixiao VDI Agent - Display Capture Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display_capture.h"
#include <dxgi1_6.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

namespace zixiao::vdi {

DisplayCapture::DisplayCapture() {
    frameInterval_ = std::chrono::microseconds(1000000 / targetFps_);
}

DisplayCapture::~DisplayCapture() {
    Shutdown();
}

bool DisplayCapture::Initialize() {
    LOG_INFO(L"Initializing display capture...");

    if (!InitializeD3D()) {
        LOG_ERROR(L"Failed to initialize D3D11");
        return false;
    }

    if (!InitializeDuplication()) {
        LOG_ERROR(L"Failed to initialize desktop duplication");
        return false;
    }

    LOG_INFO(L"Display capture initialized");
    return true;
}

bool DisplayCapture::InitializeD3D() {
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &device_,
        &featureLevel,
        &context_);

    if (FAILED(hr)) {
        // Try without debug flag
        createFlags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            createFlags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            &device_,
            &featureLevel,
            &context_);
    }

    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"D3D11CreateDevice failed: 0x%08X", hr);
        return false;
    }

    LogF(LogLevel::Debug, L"D3D11 device created, feature level: 0x%X", featureLevel);
    return true;
}

bool DisplayCapture::InitializeDuplication() {
    // Get DXGI device
    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = device_.As(&dxgiDevice);
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to get DXGI device: 0x%08X", hr);
        return false;
    }

    // Get adapter
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to get adapter: 0x%08X", hr);
        return false;
    }

    // Get output (monitor)
    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(monitorIndex_, &output);
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to get output %u: 0x%08X", monitorIndex_, hr);
        return false;
    }

    // Get output1 for desktop duplication
    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to get IDXGIOutput1: 0x%08X", hr);
        return false;
    }

    // Get output description
    DXGI_OUTPUT_DESC outputDesc;
    output->GetDesc(&outputDesc);

    monitorInfo_.id = monitorIndex_;
    monitorInfo_.x = outputDesc.DesktopCoordinates.left;
    monitorInfo_.y = outputDesc.DesktopCoordinates.top;
    monitorInfo_.width = outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    monitorInfo_.height = outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;
    monitorInfo_.name = outputDesc.DeviceName;

    LogF(LogLevel::Info, L"Monitor %u: %s (%ux%u at %d,%d)",
        monitorIndex_, outputDesc.DeviceName,
        monitorInfo_.width, monitorInfo_.height,
        monitorInfo_.x, monitorInfo_.y);

    // Create desktop duplication
    hr = output1->DuplicateOutput(device_.Get(), &duplication_);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE) {
            LOG_ERROR(L"Desktop duplication not available - may be in use by another app");
        } else if (hr == DXGI_ERROR_UNSUPPORTED) {
            LOG_ERROR(L"Desktop duplication not supported on this system");
        } else {
            LogF(LogLevel::Error, L"DuplicateOutput failed: 0x%08X", hr);
        }
        return false;
    }

    // Create staging texture for CPU access
    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = monitorInfo_.width;
    stagingDesc.Height = monitorInfo_.height;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    hr = device_->CreateTexture2D(&stagingDesc, nullptr, &stagingTexture_);
    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"Failed to create staging texture: 0x%08X", hr);
        return false;
    }

    return true;
}

void DisplayCapture::Shutdown() {
    Stop();

    duplication_.Reset();
    stagingTexture_.Reset();
    context_.Reset();
    device_.Reset();

    LOG_INFO(L"Display capture shutdown");
}

bool DisplayCapture::Start() {
    if (running_.load()) {
        return true;
    }

    LOG_INFO(L"Starting display capture...");

    frameInterval_ = std::chrono::microseconds(1000000 / targetFps_);
    stopping_.store(false);
    running_.store(true);

    captureThread_ = std::thread(&DisplayCapture::CaptureLoop, this);

    LOG_INFO(L"Display capture started");
    return true;
}

void DisplayCapture::Stop() {
    if (!running_.load()) {
        return;
    }

    LOG_INFO(L"Stopping display capture...");

    stopping_.store(true);
    running_.store(false);

    if (captureThread_.joinable()) {
        captureThread_.join();
    }

    LOG_INFO(L"Display capture stopped");
}

void DisplayCapture::CaptureLoop() {
    lastFrameTime_ = std::chrono::steady_clock::now();

    while (!stopping_.load()) {
        auto frameStart = std::chrono::steady_clock::now();

        FrameData frame;
        if (AcquireFrame(frame)) {
            if (frameCallback_) {
                frameCallback_(frame);
            }
            frameCount_++;
        }

        // Rate limiting
        auto elapsed = std::chrono::steady_clock::now() - frameStart;
        if (elapsed < frameInterval_) {
            std::this_thread::sleep_for(frameInterval_ - elapsed);
        }
    }
}

bool DisplayCapture::AcquireFrame(FrameData& frame) {
    if (!duplication_) {
        return false;
    }

    ComPtr<IDXGIResource> resource;
    DXGI_OUTDUPL_FRAME_INFO frameInfo;

    // Try to acquire frame with timeout
    HRESULT hr = duplication_->AcquireNextFrame(100, &frameInfo, &resource);

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        // No new frame available
        return false;
    }

    if (hr == DXGI_ERROR_ACCESS_LOST) {
        // Desktop duplication lost - need to reinitialize
        LOG_WARNING(L"Desktop duplication access lost, reinitializing...");
        duplication_.Reset();
        if (!InitializeDuplication()) {
            return false;
        }
        return false;
    }

    if (FAILED(hr)) {
        LogF(LogLevel::Error, L"AcquireNextFrame failed: 0x%08X", hr);
        return false;
    }

    // Get texture from resource
    ComPtr<ID3D11Texture2D> texture;
    hr = resource.As(&texture);
    if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        return false;
    }

    // Copy to staging texture
    context_->CopyResource(stagingTexture_.Get(), texture.Get());

    // Map staging texture for CPU read
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context_->Map(stagingTexture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        duplication_->ReleaseFrame();
        return false;
    }

    // Copy data to frame
    frame.width = monitorInfo_.width;
    frame.height = monitorInfo_.height;
    frame.stride = mapped.RowPitch;
    frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    frame.keyFrame = (frameCount_ % 30 == 0);  // Every 30 frames is a keyframe

    size_t dataSize = mapped.RowPitch * monitorInfo_.height;
    frame.data.resize(dataSize);
    memcpy(frame.data.data(), mapped.pData, dataSize);

    context_->Unmap(stagingTexture_.Get(), 0);
    duplication_->ReleaseFrame();

    return true;
}

void DisplayCapture::ReleaseFrame() {
    if (duplication_) {
        duplication_->ReleaseFrame();
    }
}

bool DisplayCapture::CaptureFrame(FrameData& frame) {
    return AcquireFrame(frame);
}

std::vector<MonitorInfo> DisplayCapture::EnumerateMonitors() {
    std::vector<MonitorInfo> monitors;

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return monitors;
    }

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT adapterIndex = 0;
         factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND;
         adapterIndex++) {

        ComPtr<IDXGIOutput> output;
        for (UINT outputIndex = 0;
             adapter->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND;
             outputIndex++) {

            DXGI_OUTPUT_DESC desc;
            output->GetDesc(&desc);

            MonitorInfo info;
            info.id = static_cast<uint32_t>(monitors.size());
            info.x = desc.DesktopCoordinates.left;
            info.y = desc.DesktopCoordinates.top;
            info.width = desc.DesktopCoordinates.right - desc.DesktopCoordinates.left;
            info.height = desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top;
            info.name = desc.DeviceName;
            info.primary = (info.x == 0 && info.y == 0);

            monitors.push_back(info);
            output.Reset();
        }
        adapter.Reset();
    }

    return monitors;
}

MonitorInfo DisplayCapture::GetCurrentMonitor() const {
    return monitorInfo_;
}

} // namespace zixiao::vdi
