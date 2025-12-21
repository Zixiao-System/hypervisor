/*
 * Zixiao VDI Agent - Common Definitions
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <winsvc.h>
#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <condition_variable>
#include <chrono>
#include <format>
#include <optional>
#include <span>

namespace zixiao::vdi {

//
// Service name and display name
//
constexpr const wchar_t* SERVICE_NAME = L"ZixiaoVDIAgent";
constexpr const wchar_t* SERVICE_DISPLAY_NAME = L"Zixiao VDI Agent";
constexpr const wchar_t* SERVICE_DESCRIPTION = L"Provides VDI capabilities for Zixiao Hypervisor guests";

//
// Version
//
constexpr uint32_t VERSION_MAJOR = 1;
constexpr uint32_t VERSION_MINOR = 0;
constexpr uint32_t VERSION_PATCH = 0;

//
// Logging
//
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

void Log(LogLevel level, const std::wstring& message);
void LogF(LogLevel level, const wchar_t* format, ...);

#define LOG_TRACE(msg)   Log(LogLevel::Trace, msg)
#define LOG_DEBUG(msg)   Log(LogLevel::Debug, msg)
#define LOG_INFO(msg)    Log(LogLevel::Info, msg)
#define LOG_WARNING(msg) Log(LogLevel::Warning, msg)
#define LOG_ERROR(msg)   Log(LogLevel::Error, msg)
#define LOG_FATAL(msg)   Log(LogLevel::Fatal, msg)

//
// Frame data for display capture
//
struct FrameData {
    std::vector<uint8_t> data;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint64_t timestamp = 0;
    bool keyFrame = false;
};

//
// Audio data
//
struct AudioData {
    std::vector<uint8_t> data;
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint64_t timestamp = 0;
};

//
// Input events
//
enum class InputEventType {
    MouseMove,
    MouseButton,
    MouseWheel,
    KeyDown,
    KeyUp,
    KeyPress
};

struct MouseEvent {
    int32_t x = 0;
    int32_t y = 0;
    int32_t deltaX = 0;
    int32_t deltaY = 0;
    uint32_t button = 0;  // 0=none, 1=left, 2=right, 3=middle
    bool pressed = false;
    int32_t wheelDelta = 0;
};

struct KeyEvent {
    uint32_t scanCode = 0;
    uint32_t virtualKey = 0;
    bool extended = false;
    bool pressed = false;
};

struct InputEvent {
    InputEventType type;
    uint64_t timestamp = 0;
    union {
        MouseEvent mouse;
        KeyEvent key;
    };

    InputEvent() : type(InputEventType::MouseMove), mouse{} {}
};

//
// Clipboard data
//
enum class ClipboardFormat {
    None,
    Text,
    UnicodeText,
    Rtf,
    Html,
    Image,
    FileList
};

struct ClipboardData {
    ClipboardFormat format = ClipboardFormat::None;
    std::vector<uint8_t> data;
    std::wstring mimeType;
};

//
// Monitor information
//
struct MonitorInfo {
    uint32_t id = 0;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 32;
    bool primary = false;
    std::wstring name;
};

//
// Interface for subsystems
//
class ISubsystem {
public:
    virtual ~ISubsystem() = default;
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsRunning() const = 0;
    virtual const wchar_t* GetName() const = 0;
};

//
// Callback types
//
using FrameCallback = std::function<void(const FrameData&)>;
using AudioCallback = std::function<void(const AudioData&)>;
using InputCallback = std::function<void(const InputEvent&)>;
using ClipboardCallback = std::function<void(const ClipboardData&)>;

//
// Thread-safe queue
//
template<typename T>
class ThreadSafeQueue {
public:
    void Push(T item) {
        std::lock_guard lock(mutex_);
        queue_.push(std::move(item));
        cv_.notify_one();
    }

    std::optional<T> Pop(std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) {
        std::unique_lock lock(mutex_);
        if (timeout.count() > 0) {
            if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
                return std::nullopt;
            }
        } else if (queue_.empty()) {
            return std::nullopt;
        }
        T item = std::move(queue_.front());
        queue_.pop();
        return item;
    }

    bool Empty() const {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

    size_t Size() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    void Clear() {
        std::lock_guard lock(mutex_);
        while (!queue_.empty()) queue_.pop();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
};

//
// RAII handle wrapper
//
template<typename HandleType, typename Deleter>
class UniqueHandle {
public:
    UniqueHandle() : handle_(nullptr) {}
    explicit UniqueHandle(HandleType h) : handle_(h) {}
    ~UniqueHandle() { Reset(); }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    void Reset(HandleType h = nullptr) {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE) {
            Deleter()(handle_);
        }
        handle_ = h;
    }

    HandleType Get() const { return handle_; }
    HandleType* GetAddressOf() { return &handle_; }
    explicit operator bool() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }
    HandleType Release() {
        HandleType h = handle_;
        handle_ = nullptr;
        return h;
    }

private:
    HandleType handle_;
};

struct HandleDeleter {
    void operator()(HANDLE h) const { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
};

using ScopedHandle = UniqueHandle<HANDLE, HandleDeleter>;

//
// Error handling
//
inline std::wstring GetLastErrorMessage() {
    DWORD error = GetLastError();
    if (error == 0) return L"";

    LPWSTR buffer = nullptr;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::wstring message = buffer ? buffer : L"Unknown error";
    LocalFree(buffer);
    return message;
}

} // namespace zixiao::vdi
