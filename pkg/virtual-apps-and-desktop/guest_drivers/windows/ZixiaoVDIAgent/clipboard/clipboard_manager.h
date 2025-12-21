/*
 * Zixiao VDI Agent - Clipboard Manager
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "../src/common.h"

namespace zixiao::vdi {

//
// Clipboard synchronization manager
//
class ClipboardManager : public ISubsystem {
public:
    ClipboardManager();
    ~ClipboardManager() override;

    // ISubsystem
    bool Initialize() override;
    void Shutdown() override;
    bool IsRunning() const override { return running_.load(); }
    const wchar_t* GetName() const override { return L"ClipboardManager"; }

    // Clipboard callback for outgoing changes
    void SetClipboardCallback(ClipboardCallback callback) { clipboardCallback_ = std::move(callback); }

    // Set clipboard from remote
    bool SetClipboard(const ClipboardData& data);

    // Get current clipboard contents
    ClipboardData GetClipboard();

    // Control
    bool Start();
    void Stop();

private:
    static LRESULT CALLBACK ClipboardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void OnClipboardUpdate();
    void MessageLoop();

    // Hidden window for clipboard monitoring
    HWND hwnd_ = nullptr;
    std::wstring windowClass_;
    std::thread messageThread_;

    // State
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    ClipboardCallback clipboardCallback_;

    // Prevent echo when we set clipboard ourselves
    std::atomic<bool> ignoreNextChange_{false};
    uint32_t lastSequenceNumber_ = 0;
};

} // namespace zixiao::vdi
