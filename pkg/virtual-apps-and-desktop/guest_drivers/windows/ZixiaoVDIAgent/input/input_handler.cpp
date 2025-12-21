/*
 * Zixiao VDI Agent - Input Handler Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#include "input_handler.h"

namespace zixiao::vdi {

InputHandler::InputHandler() = default;

InputHandler::~InputHandler() {
    Shutdown();
}

bool InputHandler::Initialize() {
    LOG_INFO(L"Initializing input handler...");

    // Get screen dimensions
    screenWidth_ = GetSystemMetrics(SM_CXSCREEN);
    screenHeight_ = GetSystemMetrics(SM_CYSCREEN);

    LogF(LogLevel::Info, L"Screen size: %ux%u", screenWidth_, screenHeight_);

    initialized_ = true;
    LOG_INFO(L"Input handler initialized");
    return true;
}

void InputHandler::Shutdown() {
    initialized_ = false;
    LOG_INFO(L"Input handler shutdown");
}

bool InputHandler::InjectInput(const InputEvent& event) {
    switch (event.type) {
        case InputEventType::MouseMove:
            return InjectMouseMove(event.mouse.x, event.mouse.y, true);

        case InputEventType::MouseButton:
            return InjectMouseButton(event.mouse.button, event.mouse.pressed);

        case InputEventType::MouseWheel:
            return InjectMouseWheel(event.mouse.wheelDelta);

        case InputEventType::KeyDown:
        case InputEventType::KeyPress:
            return InjectKeyboard(event.key.scanCode, event.key.virtualKey, true, event.key.extended);

        case InputEventType::KeyUp:
            return InjectKeyboard(event.key.scanCode, event.key.virtualKey, false, event.key.extended);

        default:
            return false;
    }
}

bool InputHandler::InjectMouseMove(int32_t x, int32_t y, bool absolute) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;

    if (absolute) {
        input.mi.dwFlags |= MOUSEEVENTF_ABSOLUTE;

        // Convert to absolute coordinates (0-65535 range)
        if (screenWidth_ > 0 && screenHeight_ > 0) {
            input.mi.dx = MulDiv(x, 65535, screenWidth_ - 1);
            input.mi.dy = MulDiv(y, 65535, screenHeight_ - 1);
        } else {
            input.mi.dx = x;
            input.mi.dy = y;
        }
    } else {
        input.mi.dx = x;
        input.mi.dy = y;
    }

    UINT sent = SendInput(1, &input, sizeof(INPUT));
    return sent == 1;
}

bool InputHandler::InjectMouseButton(uint32_t button, bool pressed) {
    INPUT input = {};
    input.type = INPUT_MOUSE;

    switch (button) {
        case 1:  // Left button
            input.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
            break;

        case 2:  // Right button
            input.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
            break;

        case 3:  // Middle button
            input.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
            break;

        case 4:  // X1 button (back)
            input.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = XBUTTON1;
            break;

        case 5:  // X2 button (forward)
            input.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = XBUTTON2;
            break;

        default:
            return false;
    }

    UINT sent = SendInput(1, &input, sizeof(INPUT));
    return sent == 1;
}

bool InputHandler::InjectMouseWheel(int32_t delta) {
    INPUT input = {};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = static_cast<DWORD>(delta);

    UINT sent = SendInput(1, &input, sizeof(INPUT));
    return sent == 1;
}

bool InputHandler::InjectKeyboard(uint32_t scanCode, uint32_t virtualKey, bool pressed, bool extended) {
    INPUT input = {};
    input.type = INPUT_KEYBOARD;

    // Prefer scan code if available
    if (scanCode != 0) {
        input.ki.wScan = static_cast<WORD>(scanCode);
        input.ki.dwFlags = KEYEVENTF_SCANCODE;

        if (!pressed) {
            input.ki.dwFlags |= KEYEVENTF_KEYUP;
        }

        if (extended) {
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
    } else if (virtualKey != 0) {
        input.ki.wVk = static_cast<WORD>(virtualKey);

        if (!pressed) {
            input.ki.dwFlags = KEYEVENTF_KEYUP;
        }

        if (extended) {
            input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
    } else {
        return false;
    }

    UINT sent = SendInput(1, &input, sizeof(INPUT));
    return sent == 1;
}

void InputHandler::SetScreenSize(uint32_t width, uint32_t height) {
    screenWidth_ = width;
    screenHeight_ = height;
    LogF(LogLevel::Debug, L"Screen size updated: %ux%u", width, height);
}

void InputHandler::ConvertToAbsolute(int32_t& x, int32_t& y) const {
    if (screenWidth_ > 0 && screenHeight_ > 0) {
        x = MulDiv(x, 65535, screenWidth_ - 1);
        y = MulDiv(y, 65535, screenHeight_ - 1);
    }
}

} // namespace zixiao::vdi
