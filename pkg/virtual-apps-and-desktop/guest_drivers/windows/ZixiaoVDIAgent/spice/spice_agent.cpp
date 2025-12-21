/*
 * Zixiao VDI Agent - SPICE Agent Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spice_agent.h"
#include <setupapi.h>

#pragma comment(lib, "setupapi.lib")

namespace zixiao::vdi {

//
// VirtIO-Serial GUID for SPICE port
//
// {6FDE7547-1B65-48AE-B628-80BE62016026} - vioserial class
static const GUID GUID_VIOSERIAL = {
    0x6FDE7547, 0x1B65, 0x48AE,
    { 0xB6, 0x28, 0x80, 0xBE, 0x62, 0x01, 0x60, 0x26 }
};

// SPICE vdagent message header
#pragma pack(push, 1)
struct VDAgentHeader {
    uint32_t port;
    uint32_t size;
};

struct VDAgentMessage {
    uint32_t type;
    uint32_t opaque;
    uint32_t size;
    // data follows
};

struct VDAgentMouseState {
    uint32_t x;
    uint32_t y;
    uint32_t buttons;
    uint8_t  display_id;
};

struct VDAgentMonitorsConfig {
    uint32_t num_monitors;
    uint32_t flags;
    // VDAgentMonitor monitors[] follows
};

struct VDAgentMonitor {
    uint32_t height;
    uint32_t width;
    int32_t  depth;
    int32_t  x;
    int32_t  y;
};

struct VDAgentAnnounceCapabilities {
    uint32_t request;
    uint32_t caps[1];  // Variable size
};

struct VDAgentClipboardGrab {
    uint32_t types[1];  // Variable size, terminated by 0
};

struct VDAgentClipboard {
    uint32_t type;
    // data follows
};
#pragma pack(pop)

// Clipboard types
enum VDAgentClipboardType : uint32_t {
    VD_AGENT_CLIPBOARD_NONE = 0,
    VD_AGENT_CLIPBOARD_UTF8_TEXT = 1,
    VD_AGENT_CLIPBOARD_IMAGE_PNG = 2,
    VD_AGENT_CLIPBOARD_IMAGE_BMP = 3,
    VD_AGENT_CLIPBOARD_IMAGE_TIFF = 4,
    VD_AGENT_CLIPBOARD_IMAGE_JPG = 5,
    VD_AGENT_CLIPBOARD_FILE_LIST = 6,
};

// Capabilities
enum VDAgentCap {
    VD_AGENT_CAP_MOUSE_STATE = 0,
    VD_AGENT_CAP_MONITORS_CONFIG = 1,
    VD_AGENT_CAP_REPLY = 2,
    VD_AGENT_CAP_CLIPBOARD = 3,
    VD_AGENT_CAP_DISPLAY_CONFIG = 4,
    VD_AGENT_CAP_CLIPBOARD_BY_DEMAND = 5,
    VD_AGENT_CAP_CLIPBOARD_SELECTION = 6,
    VD_AGENT_CAP_SPARSE_MONITORS_CONFIG = 7,
    VD_AGENT_CAP_GUEST_LINEEND_LF = 8,
    VD_AGENT_CAP_GUEST_LINEEND_CRLF = 9,
    VD_AGENT_CAP_MAX_CLIPBOARD = 10,
    VD_AGENT_CAP_AUDIO_VOLUME_SYNC = 11,
    VD_AGENT_CAP_MONITORS_CONFIG_POSITION = 12,
    VD_AGENT_CAP_FILE_XFER_DISABLED = 13,
    VD_AGENT_CAP_FILE_XFER_DETAILED_ERRORS = 14,
    VD_AGENT_CAP_GRAPHICS_DEVICE_INFO = 15,
    VD_AGENT_CAP_CLIPBOARD_NO_RELEASE_ON_REGRAB = 16,
    VD_AGENT_CAP_CLIPBOARD_GRAB_SERIAL = 17,
};

#define VD_AGENT_PORT 1

//
// VirtIOSerial implementation
//
VirtIOSerial::VirtIOSerial() = default;

VirtIOSerial::~VirtIOSerial() {
    Close();
}

bool VirtIOSerial::Open(const std::wstring& portName) {
    Close();

    handle_ = CreateFileW(
        portName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (handle_ == INVALID_HANDLE_VALUE) {
        LogF(LogLevel::Error, L"Failed to open VirtIO-Serial port %s: %s",
            portName.c_str(), GetLastErrorMessage().c_str());
        return false;
    }

    LogF(LogLevel::Info, L"Opened VirtIO-Serial port: %s", portName.c_str());
    return true;
}

void VirtIOSerial::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool VirtIOSerial::Write(const void* data, size_t size) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesWritten = 0;
    if (!WriteFile(handle_, data, static_cast<DWORD>(size), &bytesWritten, nullptr)) {
        LogF(LogLevel::Error, L"VirtIO-Serial write failed: %s", GetLastErrorMessage().c_str());
        return false;
    }

    return bytesWritten == size;
}

bool VirtIOSerial::Read(void* buffer, size_t size, size_t& bytesRead) {
    if (handle_ == INVALID_HANDLE_VALUE) {
        bytesRead = 0;
        return false;
    }

    DWORD read = 0;
    if (!ReadFile(handle_, buffer, static_cast<DWORD>(size), &read, nullptr)) {
        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING) {
            bytesRead = 0;
            return false;
        }
    }

    bytesRead = read;
    return true;
}

bool VirtIOSerial::SetReadTimeout(uint32_t timeoutMs) {
    COMMTIMEOUTS timeouts = {};
    timeouts.ReadIntervalTimeout = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = timeoutMs;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 1000;

    return SetCommTimeouts(handle_, &timeouts) != FALSE;
}

//
// SpiceAgent implementation
//
SpiceAgent::SpiceAgent() = default;

SpiceAgent::~SpiceAgent() {
    Shutdown();
}

bool SpiceAgent::Initialize() {
    LOG_INFO(L"Initializing SPICE agent...");

    if (!FindVirtIOPort()) {
        LOG_WARNING(L"VirtIO-Serial port not found - SPICE agent disabled");
        return false;
    }

    if (!serial_.Open(portName_)) {
        LOG_ERROR(L"Failed to open VirtIO-Serial port");
        return false;
    }

    serial_.SetReadTimeout(100);

    LOG_INFO(L"SPICE agent initialized");
    return true;
}

bool SpiceAgent::FindVirtIOPort() {
    // Look for SPICE vdagent port
    // Common names: \\.\Global\com.redhat.spice.0, \\.\VIOSER00000001

    // Try common SPICE port names
    std::vector<std::wstring> portNames = {
        L"\\\\.\\Global\\com.redhat.spice.0",
        L"\\\\.\\Global\\org.zixiao.vdi.0",
        L"\\\\.\\VIOSER00000001",
    };

    for (const auto& name : portNames) {
        HANDLE h = CreateFileW(name.c_str(), GENERIC_READ, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            portName_ = name;
            LogF(LogLevel::Info, L"Found VirtIO-Serial port: %s", name.c_str());
            return true;
        }
    }

    // Enumerate VirtIO-Serial devices
    HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_VIOSERIAL, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) {
        return false;
    }

    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &GUID_VIOSERIAL, i, &ifData); i++) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &size, nullptr);

        if (size == 0) continue;

        std::vector<uint8_t> buffer(size);
        auto detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, size, nullptr, nullptr)) {
            portName_ = detail->DevicePath;
            LogF(LogLevel::Info, L"Found VirtIO-Serial device: %s", portName_.c_str());
            SetupDiDestroyDeviceInfoList(devInfo);
            return true;
        }
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return false;
}

void SpiceAgent::Shutdown() {
    Stop();
    serial_.Close();
    LOG_INFO(L"SPICE agent shutdown");
}

bool SpiceAgent::Start() {
    if (running_.load()) {
        return true;
    }

    if (!serial_.IsOpen()) {
        LOG_ERROR(L"VirtIO-Serial port not open");
        return false;
    }

    LOG_INFO(L"Starting SPICE agent...");

    stopping_.store(false);
    running_.store(true);

    // Send capabilities announcement
    SendCapabilities();

    readThread_ = std::thread(&SpiceAgent::ReadLoop, this);

    LOG_INFO(L"SPICE agent started");
    return true;
}

void SpiceAgent::Stop() {
    if (!running_.load()) {
        return;
    }

    LOG_INFO(L"Stopping SPICE agent...");

    stopping_.store(true);
    running_.store(false);

    if (readThread_.joinable()) {
        readThread_.join();
    }

    LOG_INFO(L"SPICE agent stopped");
}

void SpiceAgent::ReadLoop() {
    std::vector<uint8_t> buffer(4096);
    std::vector<uint8_t> messageBuffer;

    while (!stopping_.load()) {
        size_t bytesRead = 0;
        if (!serial_.Read(buffer.data(), buffer.size(), bytesRead)) {
            if (stopping_.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (bytesRead == 0) {
            continue;
        }

        // Append to message buffer
        messageBuffer.insert(messageBuffer.end(), buffer.begin(), buffer.begin() + bytesRead);

        // Process complete messages
        while (messageBuffer.size() >= sizeof(VDAgentHeader)) {
            auto header = reinterpret_cast<VDAgentHeader*>(messageBuffer.data());
            size_t totalSize = sizeof(VDAgentHeader) + header->size;

            if (messageBuffer.size() < totalSize) {
                break;  // Need more data
            }

            ProcessMessage(std::vector<uint8_t>(
                messageBuffer.begin() + sizeof(VDAgentHeader),
                messageBuffer.begin() + totalSize));

            messageBuffer.erase(messageBuffer.begin(), messageBuffer.begin() + totalSize);
        }
    }
}

void SpiceAgent::ProcessMessage(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(VDAgentMessage)) {
        return;
    }

    auto msg = reinterpret_cast<const VDAgentMessage*>(data.data());
    auto msgType = static_cast<VDAgentMessageType>(msg->type);

    LogF(LogLevel::Debug, L"SPICE message type: %u, size: %u", msg->type, msg->size);

    switch (msgType) {
        case VDAgentMessageType::MouseState: {
            if (msg->size >= sizeof(VDAgentMouseState)) {
                auto mouse = reinterpret_cast<const VDAgentMouseState*>(data.data() + sizeof(VDAgentMessage));
                if (inputCallback_) {
                    InputEvent event;
                    event.type = InputEventType::MouseMove;
                    event.mouse.x = mouse->x;
                    event.mouse.y = mouse->y;
                    event.mouse.button = mouse->buttons;
                    inputCallback_(event);
                }
            }
            break;
        }

        case VDAgentMessageType::AnnounceCapabilities: {
            if (msg->size >= sizeof(VDAgentAnnounceCapabilities)) {
                auto caps = reinterpret_cast<const VDAgentAnnounceCapabilities*>(
                    data.data() + sizeof(VDAgentMessage));
                hostCapabilities_ = caps->caps[0];
                LogF(LogLevel::Info, L"Host capabilities: 0x%08X", hostCapabilities_);

                if (caps->request) {
                    SendCapabilities();
                }
            }
            break;
        }

        case VDAgentMessageType::ClipboardGrab: {
            LOG_DEBUG(L"Clipboard grab from host");
            // Host wants to set clipboard - wait for clipboard data
            break;
        }

        case VDAgentMessageType::ClipboardRequest: {
            LOG_DEBUG(L"Clipboard request from host");
            // Host is requesting clipboard data - should trigger GetClipboard
            break;
        }

        default:
            LogF(LogLevel::Debug, L"Unhandled SPICE message type: %u", msg->type);
            break;
    }
}

void SpiceAgent::SendCapabilities() {
    guestCapabilities_ =
        (1 << VD_AGENT_CAP_MOUSE_STATE) |
        (1 << VD_AGENT_CAP_MONITORS_CONFIG) |
        (1 << VD_AGENT_CAP_REPLY) |
        (1 << VD_AGENT_CAP_CLIPBOARD) |
        (1 << VD_AGENT_CAP_CLIPBOARD_BY_DEMAND) |
        (1 << VD_AGENT_CAP_GUEST_LINEEND_CRLF) |
        (1 << VD_AGENT_CAP_MONITORS_CONFIG_POSITION);

    VDAgentAnnounceCapabilities caps = {};
    caps.request = 1;  // Request host capabilities
    caps.caps[0] = guestCapabilities_;

    SendMessage(VDAgentMessageType::AnnounceCapabilities, &caps, sizeof(caps));
    LogF(LogLevel::Info, L"Sent guest capabilities: 0x%08X", guestCapabilities_);
}

bool SpiceAgent::SendMessage(VDAgentMessageType type, const void* data, size_t size) {
    std::vector<uint8_t> buffer(sizeof(VDAgentHeader) + sizeof(VDAgentMessage) + size);

    auto header = reinterpret_cast<VDAgentHeader*>(buffer.data());
    header->port = VD_AGENT_PORT;
    header->size = static_cast<uint32_t>(sizeof(VDAgentMessage) + size);

    auto msg = reinterpret_cast<VDAgentMessage*>(buffer.data() + sizeof(VDAgentHeader));
    msg->type = static_cast<uint32_t>(type);
    msg->opaque = 0;
    msg->size = static_cast<uint32_t>(size);

    if (size > 0 && data) {
        memcpy(buffer.data() + sizeof(VDAgentHeader) + sizeof(VDAgentMessage), data, size);
    }

    return serial_.Write(buffer.data(), buffer.size());
}

bool SpiceAgent::SendFrame(const FrameData& /*frame*/) {
    // SPICE uses different mechanism for display (QXL driver)
    // The vdagent is primarily for auxiliary features
    return true;
}

bool SpiceAgent::SendClipboard(const ClipboardData& data) {
    if (data.format == ClipboardFormat::None) {
        return false;
    }

    // First, grab clipboard
    VDAgentClipboardGrab grab = {};
    switch (data.format) {
        case ClipboardFormat::UnicodeText:
        case ClipboardFormat::Text:
            grab.types[0] = VD_AGENT_CLIPBOARD_UTF8_TEXT;
            break;
        case ClipboardFormat::Image:
            grab.types[0] = VD_AGENT_CLIPBOARD_IMAGE_BMP;
            break;
        default:
            return false;
    }

    SendMessage(VDAgentMessageType::ClipboardGrab, &grab, sizeof(uint32_t) * 2);

    // Then send data
    std::vector<uint8_t> buffer(sizeof(VDAgentClipboard) + data.data.size());
    auto clip = reinterpret_cast<VDAgentClipboard*>(buffer.data());
    clip->type = grab.types[0];
    memcpy(buffer.data() + sizeof(VDAgentClipboard), data.data.data(), data.data.size());

    return SendMessage(VDAgentMessageType::Clipboard, buffer.data(), buffer.size());
}

bool SpiceAgent::SendMonitorConfig(const std::vector<MonitorInfo>& monitors) {
    size_t size = sizeof(VDAgentMonitorsConfig) + monitors.size() * sizeof(VDAgentMonitor);
    std::vector<uint8_t> buffer(size);

    auto config = reinterpret_cast<VDAgentMonitorsConfig*>(buffer.data());
    config->num_monitors = static_cast<uint32_t>(monitors.size());
    config->flags = 0;

    auto mon = reinterpret_cast<VDAgentMonitor*>(buffer.data() + sizeof(VDAgentMonitorsConfig));
    for (const auto& info : monitors) {
        mon->width = info.width;
        mon->height = info.height;
        mon->depth = info.depth;
        mon->x = info.x;
        mon->y = info.y;
        mon++;
    }

    return SendMessage(VDAgentMessageType::MonitorsConfig, buffer.data(), buffer.size());
}

} // namespace zixiao::vdi
