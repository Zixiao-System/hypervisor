/*
 * Zixiao VDI Agent - Clipboard Manager Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "clipboard_manager.h"

namespace zixiao::vdi {

static ClipboardManager* g_clipboardManager = nullptr;

ClipboardManager::ClipboardManager() {
    windowClass_ = L"ZixiaoVDIClipboardWnd";
}

ClipboardManager::~ClipboardManager() {
    Shutdown();
}

bool ClipboardManager::Initialize() {
    LOG_INFO(L"Initializing clipboard manager...");

    g_clipboardManager = this;
    lastSequenceNumber_ = GetClipboardSequenceNumber();

    LOG_INFO(L"Clipboard manager initialized");
    return true;
}

void ClipboardManager::Shutdown() {
    Stop();
    g_clipboardManager = nullptr;
    LOG_INFO(L"Clipboard manager shutdown");
}

bool ClipboardManager::Start() {
    if (running_.load()) {
        return true;
    }

    LOG_INFO(L"Starting clipboard manager...");

    stopping_.store(false);
    running_.store(true);

    messageThread_ = std::thread(&ClipboardManager::MessageLoop, this);

    LOG_INFO(L"Clipboard manager started");
    return true;
}

void ClipboardManager::Stop() {
    if (!running_.load()) {
        return;
    }

    LOG_INFO(L"Stopping clipboard manager...");

    stopping_.store(true);
    running_.store(false);

    // Post quit message to window thread
    if (hwnd_) {
        PostMessageW(hwnd_, WM_QUIT, 0, 0);
    }

    if (messageThread_.joinable()) {
        messageThread_.join();
    }

    LOG_INFO(L"Clipboard manager stopped");
}

void ClipboardManager::MessageLoop() {
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = ClipboardWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = windowClass_.c_str();

    if (!RegisterClassExW(&wc)) {
        LOG_ERROR(L"Failed to register clipboard window class");
        running_.store(false);
        return;
    }

    // Create hidden window
    hwnd_ = CreateWindowExW(
        0,
        windowClass_.c_str(),
        L"Zixiao VDI Clipboard",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (!hwnd_) {
        LOG_ERROR(L"Failed to create clipboard window");
        UnregisterClassW(windowClass_.c_str(), GetModuleHandleW(nullptr));
        running_.store(false);
        return;
    }

    // Add clipboard format listener
    if (!AddClipboardFormatListener(hwnd_)) {
        LOG_ERROR(L"Failed to add clipboard format listener");
        DestroyWindow(hwnd_);
        UnregisterClassW(windowClass_.c_str(), GetModuleHandleW(nullptr));
        running_.store(false);
        return;
    }

    LOG_DEBUG(L"Clipboard monitoring started");

    // Message loop
    MSG msg;
    while (!stopping_.load() && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    RemoveClipboardFormatListener(hwnd_);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    UnregisterClassW(windowClass_.c_str(), GetModuleHandleW(nullptr));
}

LRESULT CALLBACK ClipboardManager::ClipboardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLIPBOARDUPDATE) {
        if (g_clipboardManager) {
            g_clipboardManager->OnClipboardUpdate();
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ClipboardManager::OnClipboardUpdate() {
    // Check if we should ignore this update
    if (ignoreNextChange_.exchange(false)) {
        return;
    }

    // Check sequence number to avoid duplicate processing
    uint32_t seqNum = GetClipboardSequenceNumber();
    if (seqNum == lastSequenceNumber_) {
        return;
    }
    lastSequenceNumber_ = seqNum;

    LOG_DEBUG(L"Clipboard updated");

    // Get clipboard data and notify callback
    if (clipboardCallback_) {
        ClipboardData data = GetClipboard();
        if (data.format != ClipboardFormat::None) {
            clipboardCallback_(data);
        }
    }
}

ClipboardData ClipboardManager::GetClipboard() {
    ClipboardData result;

    if (!OpenClipboard(hwnd_)) {
        return result;
    }

    // Try Unicode text first
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
        if (hData) {
            wchar_t* text = static_cast<wchar_t*>(GlobalLock(hData));
            if (text) {
                size_t len = wcslen(text);
                result.format = ClipboardFormat::UnicodeText;
                result.mimeType = L"text/plain;charset=utf-16";
                result.data.resize((len + 1) * sizeof(wchar_t));
                memcpy(result.data.data(), text, result.data.size());
                GlobalUnlock(hData);
            }
        }
    }
    // Try ANSI text
    else if (IsClipboardFormatAvailable(CF_TEXT)) {
        HANDLE hData = GetClipboardData(CF_TEXT);
        if (hData) {
            char* text = static_cast<char*>(GlobalLock(hData));
            if (text) {
                size_t len = strlen(text);
                result.format = ClipboardFormat::Text;
                result.mimeType = L"text/plain";
                result.data.resize(len + 1);
                memcpy(result.data.data(), text, result.data.size());
                GlobalUnlock(hData);
            }
        }
    }
    // Try HTML
    else if (UINT cf = RegisterClipboardFormatW(L"HTML Format"); IsClipboardFormatAvailable(cf)) {
        HANDLE hData = GetClipboardData(cf);
        if (hData) {
            char* html = static_cast<char*>(GlobalLock(hData));
            if (html) {
                size_t len = GlobalSize(hData);
                result.format = ClipboardFormat::Html;
                result.mimeType = L"text/html";
                result.data.resize(len);
                memcpy(result.data.data(), html, len);
                GlobalUnlock(hData);
            }
        }
    }
    // Try bitmap
    else if (IsClipboardFormatAvailable(CF_DIB) || IsClipboardFormatAvailable(CF_BITMAP)) {
        HANDLE hData = GetClipboardData(CF_DIB);
        if (hData) {
            void* dib = GlobalLock(hData);
            if (dib) {
                size_t size = GlobalSize(hData);
                result.format = ClipboardFormat::Image;
                result.mimeType = L"image/bmp";
                result.data.resize(size);
                memcpy(result.data.data(), dib, size);
                GlobalUnlock(hData);
            }
        }
    }

    CloseClipboard();
    return result;
}

bool ClipboardManager::SetClipboard(const ClipboardData& data) {
    if (data.format == ClipboardFormat::None || data.data.empty()) {
        return false;
    }

    if (!OpenClipboard(hwnd_)) {
        LOG_ERROR(L"Failed to open clipboard");
        return false;
    }

    EmptyClipboard();

    bool success = false;
    UINT format = 0;

    switch (data.format) {
        case ClipboardFormat::UnicodeText:
            format = CF_UNICODETEXT;
            break;
        case ClipboardFormat::Text:
            format = CF_TEXT;
            break;
        case ClipboardFormat::Html:
            format = RegisterClipboardFormatW(L"HTML Format");
            break;
        case ClipboardFormat::Image:
            format = CF_DIB;
            break;
        default:
            CloseClipboard();
            return false;
    }

    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, data.data.size());
    if (hMem) {
        void* mem = GlobalLock(hMem);
        if (mem) {
            memcpy(mem, data.data.data(), data.data.size());
            GlobalUnlock(hMem);

            // Ignore the clipboard update we're about to cause
            ignoreNextChange_.store(true);

            if (SetClipboardData(format, hMem)) {
                success = true;
            } else {
                GlobalFree(hMem);
            }
        } else {
            GlobalFree(hMem);
        }
    }

    CloseClipboard();
    return success;
}

} // namespace zixiao::vdi
