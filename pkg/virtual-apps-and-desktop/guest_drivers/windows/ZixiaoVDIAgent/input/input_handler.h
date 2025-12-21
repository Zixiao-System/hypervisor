/*
 * Zixiao VDI Agent - Input Handler
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "../src/common.h"

namespace zixiao::vdi {

//
// Input injection using SendInput API
//
class InputHandler : public ISubsystem {
public:
    InputHandler();
    ~InputHandler() override;

    // ISubsystem
    bool Initialize() override;
    void Shutdown() override;
    bool IsRunning() const override { return initialized_; }
    const wchar_t* GetName() const override { return L"InputHandler"; }

    // Inject input events
    bool InjectInput(const InputEvent& event);
    bool InjectMouseMove(int32_t x, int32_t y, bool absolute = true);
    bool InjectMouseButton(uint32_t button, bool pressed);
    bool InjectMouseWheel(int32_t delta);
    bool InjectKeyboard(uint32_t scanCode, uint32_t virtualKey, bool pressed, bool extended = false);

    // Screen coordinate conversion
    void SetScreenSize(uint32_t width, uint32_t height);
    void ConvertToAbsolute(int32_t& x, int32_t& y) const;

private:
    bool initialized_ = false;

    // Screen dimensions for absolute positioning
    uint32_t screenWidth_ = 0;
    uint32_t screenHeight_ = 0;
    uint32_t virtualWidth_ = 65535;
    uint32_t virtualHeight_ = 65535;
};

} // namespace zixiao::vdi
