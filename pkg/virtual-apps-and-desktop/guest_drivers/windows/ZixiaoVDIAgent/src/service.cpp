/*
 * Zixiao VDI Agent - Service Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "service.h"
#include "../display/display_capture.h"
#include "../audio/audio_capture.h"
#include "../input/input_handler.h"
#include "../clipboard/clipboard_manager.h"
#include "../spice/spice_agent.h"
#include "../webrtc/webrtc_agent.h"

#include <iostream>
#include <fstream>
#include <cstdarg>

namespace zixiao::vdi {

//
// Logging implementation
//
static std::mutex g_logMutex;
static LogLevel g_minLogLevel = LogLevel::Info;

void Log(LogLevel level, const std::wstring& message) {
    if (level < g_minLogLevel) return;

    std::lock_guard lock(g_logMutex);

    const wchar_t* levelStr = L"INFO";
    switch (level) {
        case LogLevel::Trace:   levelStr = L"TRACE"; break;
        case LogLevel::Debug:   levelStr = L"DEBUG"; break;
        case LogLevel::Info:    levelStr = L"INFO"; break;
        case LogLevel::Warning: levelStr = L"WARN"; break;
        case LogLevel::Error:   levelStr = L"ERROR"; break;
        case LogLevel::Fatal:   levelStr = L"FATAL"; break;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);

    std::wstring logLine = std::format(
        L"[{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}.{:03d}] [{}] {}\n",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        levelStr, message);

    OutputDebugStringW(logLine.c_str());
    std::wcerr << logLine;
}

void LogF(LogLevel level, const wchar_t* format, ...) {
    if (level < g_minLogLevel) return;

    wchar_t buffer[2048];
    va_list args;
    va_start(args, format);
    vswprintf_s(buffer, format, args);
    va_end(args);

    Log(level, buffer);
}

//
// VDIService implementation
//
VDIService& VDIService::GetInstance() {
    static VDIService instance;
    return instance;
}

VDIService::VDIService() {
    serviceStatus_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    serviceStatus_.dwCurrentState = SERVICE_STOPPED;
    serviceStatus_.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    serviceStatus_.dwWin32ExitCode = NO_ERROR;
    serviceStatus_.dwServiceSpecificExitCode = 0;
    serviceStatus_.dwCheckPoint = 0;
    serviceStatus_.dwWaitHint = 0;
}

VDIService::~VDIService() {
    ShutdownSubsystems();
}

bool VDIService::Initialize() {
    LOG_INFO(L"Initializing Zixiao VDI Agent...");

    // Create stop event
    stopEvent_.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!stopEvent_) {
        LOG_ERROR(L"Failed to create stop event");
        return false;
    }

    // Initialize subsystems
    if (!InitializeSubsystems()) {
        LOG_ERROR(L"Failed to initialize subsystems");
        return false;
    }

    LOG_INFO(L"Zixiao VDI Agent initialized successfully");
    return true;
}

bool VDIService::InitializeSubsystems() {
    // Create display capture
    displayCapture_ = std::make_unique<DisplayCapture>();
    if (!displayCapture_->Initialize()) {
        LOG_ERROR(L"Failed to initialize display capture");
        return false;
    }

    // Create audio capture
    audioCapture_ = std::make_unique<AudioCapture>();
    if (!audioCapture_->Initialize()) {
        LOG_WARNING(L"Failed to initialize audio capture (non-fatal)");
        // Audio is optional, continue
    }

    // Create input handler
    inputHandler_ = std::make_unique<InputHandler>();
    if (!inputHandler_->Initialize()) {
        LOG_ERROR(L"Failed to initialize input handler");
        return false;
    }

    // Create clipboard manager
    clipboardManager_ = std::make_unique<ClipboardManager>();
    if (!clipboardManager_->Initialize()) {
        LOG_WARNING(L"Failed to initialize clipboard manager (non-fatal)");
        // Clipboard is optional, continue
    }

    // Create SPICE agent if enabled
    if (spiceEnabled_) {
        spiceAgent_ = std::make_unique<SpiceAgent>();
        if (!spiceAgent_->Initialize()) {
            LOG_WARNING(L"Failed to initialize SPICE agent - VirtIO-Serial may not be available");
            spiceAgent_.reset();
        }
    }

    // Create WebRTC agent if enabled
    if (webrtcEnabled_) {
        webrtcAgent_ = std::make_unique<WebRTCAgent>();
        if (!webrtcAgent_->Initialize()) {
            LOG_WARNING(L"Failed to initialize WebRTC agent");
            webrtcAgent_.reset();
        }
    }

    // Wire up callbacks between subsystems
    if (spiceAgent_) {
        // Display frames go to SPICE
        displayCapture_->SetFrameCallback([this](const FrameData& frame) {
            if (spiceAgent_) spiceAgent_->SendFrame(frame);
        });

        // Clipboard sync with SPICE
        if (clipboardManager_) {
            clipboardManager_->SetClipboardCallback([this](const ClipboardData& data) {
                if (spiceAgent_) spiceAgent_->SendClipboard(data);
            });
        }

        // Input from SPICE
        spiceAgent_->SetInputCallback([this](const InputEvent& event) {
            if (inputHandler_) inputHandler_->InjectInput(event);
        });
    }

    if (webrtcAgent_) {
        // Display frames go to WebRTC
        displayCapture_->SetFrameCallback([this](const FrameData& frame) {
            if (webrtcAgent_) webrtcAgent_->SendFrame(frame);
        });

        // Audio goes to WebRTC
        if (audioCapture_) {
            audioCapture_->SetAudioCallback([this](const AudioData& data) {
                if (webrtcAgent_) webrtcAgent_->SendAudio(data);
            });
        }

        // Input from WebRTC
        webrtcAgent_->SetInputCallback([this](const InputEvent& event) {
            if (inputHandler_) inputHandler_->InjectInput(event);
        });
    }

    return true;
}

void VDIService::ShutdownSubsystems() {
    LOG_INFO(L"Shutting down subsystems...");

    if (webrtcAgent_) {
        webrtcAgent_->Shutdown();
        webrtcAgent_.reset();
    }

    if (spiceAgent_) {
        spiceAgent_->Shutdown();
        spiceAgent_.reset();
    }

    if (clipboardManager_) {
        clipboardManager_->Shutdown();
        clipboardManager_.reset();
    }

    if (inputHandler_) {
        inputHandler_->Shutdown();
        inputHandler_.reset();
    }

    if (audioCapture_) {
        audioCapture_->Shutdown();
        audioCapture_.reset();
    }

    if (displayCapture_) {
        displayCapture_->Shutdown();
        displayCapture_.reset();
    }

    LOG_INFO(L"Subsystems shutdown complete");
}

void VDIService::Run() {
    running_.store(true);
    SetServiceStatus(SERVICE_RUNNING);

    LOG_INFO(L"Zixiao VDI Agent running");

    MainLoop();

    running_.store(false);
    LOG_INFO(L"Zixiao VDI Agent stopped");
}

void VDIService::MainLoop() {
    while (!stopping_.load()) {
        // Wait for stop event or timeout for periodic tasks
        DWORD result = WaitForSingleObject(stopEvent_.Get(), 1000);

        if (result == WAIT_OBJECT_0) {
            // Stop requested
            break;
        }

        // Periodic maintenance tasks can go here
        // - Health checks
        // - Reconnection attempts
        // - Statistics logging
    }
}

void VDIService::Stop() {
    LOG_INFO(L"Stop requested...");
    stopping_.store(true);
    SetEvent(stopEvent_.Get());
}

void VDIService::HandleControl(DWORD control) {
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            SetServiceStatus(SERVICE_STOP_PENDING);
            Stop();
            break;

        case SERVICE_CONTROL_INTERROGATE:
            SetServiceStatus(serviceStatus_.dwCurrentState);
            break;

        default:
            break;
    }
}

void VDIService::SetServiceStatus(DWORD currentState, DWORD exitCode, DWORD waitHint) {
    static DWORD checkPoint = 1;

    serviceStatus_.dwCurrentState = currentState;
    serviceStatus_.dwWin32ExitCode = exitCode;
    serviceStatus_.dwWaitHint = waitHint;

    if (currentState == SERVICE_START_PENDING) {
        serviceStatus_.dwControlsAccepted = 0;
    } else {
        serviceStatus_.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }

    if (currentState == SERVICE_RUNNING || currentState == SERVICE_STOPPED) {
        serviceStatus_.dwCheckPoint = 0;
    } else {
        serviceStatus_.dwCheckPoint = checkPoint++;
    }

    if (statusHandle_) {
        ::SetServiceStatus(statusHandle_, &serviceStatus_);
    }
}

//
// Service entry points
//
void WINAPI ServiceMain(DWORD argc, LPWSTR* argv) {
    (void)argc;
    (void)argv;

    VDIService& service = VDIService::GetInstance();

    // Register service control handler
    service.statusHandle_ = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (!service.statusHandle_) {
        LOG_ERROR(L"Failed to register service control handler");
        return;
    }

    // Set initial status
    service.SetServiceStatus(SERVICE_START_PENDING);

    // Initialize
    if (!service.Initialize()) {
        service.SetServiceStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR);
        return;
    }

    // Run main loop
    service.Run();

    // Cleanup
    service.ShutdownSubsystems();
    service.SetServiceStatus(SERVICE_STOPPED);
}

void WINAPI ServiceCtrlHandler(DWORD control) {
    VDIService::GetInstance().HandleControl(control);
}

//
// Service management utilities
//
bool InstallService() {
    SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scManager) {
        LOG_ERROR(L"Failed to open service control manager");
        return false;
    }

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    SC_HANDLE service = CreateServiceW(
        scManager,
        SERVICE_NAME,
        SERVICE_DISPLAY_NAME,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        path,
        nullptr, nullptr, nullptr, nullptr, nullptr);

    if (!service) {
        DWORD error = GetLastError();
        CloseServiceHandle(scManager);
        if (error == ERROR_SERVICE_EXISTS) {
            LOG_INFO(L"Service already exists");
            return true;
        }
        LOG_ERROR(L"Failed to create service");
        return false;
    }

    // Set description
    SERVICE_DESCRIPTIONW desc;
    desc.lpDescription = const_cast<LPWSTR>(SERVICE_DESCRIPTION);
    ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    LOG_INFO(L"Service installed successfully");
    return true;
}

bool UninstallService() {
    SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!scManager) {
        LOG_ERROR(L"Failed to open service control manager");
        return false;
    }

    SC_HANDLE service = OpenServiceW(scManager, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!service) {
        CloseServiceHandle(scManager);
        LOG_INFO(L"Service not found");
        return true;
    }

    // Stop if running
    SERVICE_STATUS status;
    ControlService(service, SERVICE_CONTROL_STOP, &status);

    // Delete
    bool result = DeleteService(service) != FALSE;

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    if (result) {
        LOG_INFO(L"Service uninstalled successfully");
    } else {
        LOG_ERROR(L"Failed to uninstall service");
    }

    return result;
}

bool StartServiceManual() {
    SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scManager) return false;

    SC_HANDLE service = OpenServiceW(scManager, SERVICE_NAME, SERVICE_START);
    if (!service) {
        CloseServiceHandle(scManager);
        return false;
    }

    bool result = ::StartServiceW(service, 0, nullptr) != FALSE;

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return result;
}

bool StopServiceManual() {
    SC_HANDLE scManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scManager) return false;

    SC_HANDLE service = OpenServiceW(scManager, SERVICE_NAME, SERVICE_STOP);
    if (!service) {
        CloseServiceHandle(scManager);
        return false;
    }

    SERVICE_STATUS status;
    bool result = ControlService(service, SERVICE_CONTROL_STOP, &status) != FALSE;

    CloseServiceHandle(service);
    CloseServiceHandle(scManager);

    return result;
}

void RunAsConsole() {
    LOG_INFO(L"Running in console mode");

    VDIService& service = VDIService::GetInstance();

    if (!service.Initialize()) {
        LOG_ERROR(L"Failed to initialize");
        return;
    }

    // Set up console handler for Ctrl+C
    SetConsoleCtrlHandler([](DWORD type) -> BOOL {
        if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
            VDIService::GetInstance().Stop();
            return TRUE;
        }
        return FALSE;
    }, TRUE);

    std::wcout << L"Press Ctrl+C to stop..." << std::endl;

    service.Run();
    service.ShutdownSubsystems();
}

} // namespace zixiao::vdi
