/*
 * Zixiao VDI Agent for Linux - Input Handler Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Uses uinput for kernel-level input injection.
 */

#include "input_handler.h"
#include <linux/uinput.h>
#include <fcntl.h>
#include <sys/ioctl.h>

struct InputHandler {
    int         uinput_fd;
    uint32_t    screen_width;
    uint32_t    screen_height;
    bool        initialized;
};

static bool setup_uinput_device(InputHandler *ih);
static void emit_event(InputHandler *ih, int type, int code, int value);
static void sync_event(InputHandler *ih);

InputHandler *input_handler_create(void) {
    InputHandler *ih = zixiao_calloc(1, sizeof(InputHandler));
    if (!ih) return NULL;

    ih->uinput_fd = -1;
    ih->screen_width = 1920;
    ih->screen_height = 1080;
    ih->initialized = false;

    return ih;
}

void input_handler_destroy(InputHandler *ih) {
    if (!ih) return;

    input_handler_shutdown(ih);
    zixiao_free(ih);
}

bool input_handler_init(InputHandler *ih) {
    if (!ih) return false;

    LOG_INFO("Initializing input handler...");

    /* Open uinput device */
    ih->uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (ih->uinput_fd < 0) {
        LOG_ERROR("Failed to open /dev/uinput: %s", strerror(errno));
        LOG_INFO("Hint: Make sure uinput module is loaded and you have permissions");
        return false;
    }

    if (!setup_uinput_device(ih)) {
        LOG_ERROR("Failed to setup uinput device");
        close(ih->uinput_fd);
        ih->uinput_fd = -1;
        return false;
    }

    ih->initialized = true;
    LOG_INFO("Input handler initialized");
    return true;
}

void input_handler_shutdown(InputHandler *ih) {
    if (!ih) return;

    if (ih->uinput_fd >= 0) {
        ioctl(ih->uinput_fd, UI_DEV_DESTROY);
        close(ih->uinput_fd);
        ih->uinput_fd = -1;
    }

    ih->initialized = false;
    LOG_INFO("Input handler shutdown");
}

static bool setup_uinput_device(InputHandler *ih) {
    /* Enable event types */
    if (ioctl(ih->uinput_fd, UI_SET_EVBIT, EV_KEY) < 0) return false;
    if (ioctl(ih->uinput_fd, UI_SET_EVBIT, EV_REL) < 0) return false;
    if (ioctl(ih->uinput_fd, UI_SET_EVBIT, EV_ABS) < 0) return false;
    if (ioctl(ih->uinput_fd, UI_SET_EVBIT, EV_SYN) < 0) return false;

    /* Enable mouse buttons */
    if (ioctl(ih->uinput_fd, UI_SET_KEYBIT, BTN_LEFT) < 0) return false;
    if (ioctl(ih->uinput_fd, UI_SET_KEYBIT, BTN_RIGHT) < 0) return false;
    if (ioctl(ih->uinput_fd, UI_SET_KEYBIT, BTN_MIDDLE) < 0) return false;

    /* Enable relative axes (for mouse movement) */
    if (ioctl(ih->uinput_fd, UI_SET_RELBIT, REL_X) < 0) return false;
    if (ioctl(ih->uinput_fd, UI_SET_RELBIT, REL_Y) < 0) return false;
    if (ioctl(ih->uinput_fd, UI_SET_RELBIT, REL_WHEEL) < 0) return false;

    /* Enable absolute axes (for absolute positioning) */
    if (ioctl(ih->uinput_fd, UI_SET_ABSBIT, ABS_X) < 0) return false;
    if (ioctl(ih->uinput_fd, UI_SET_ABSBIT, ABS_Y) < 0) return false;

    /* Enable all keyboard keys */
    for (int i = 0; i < 256; i++) {
        ioctl(ih->uinput_fd, UI_SET_KEYBIT, i);
    }

    /* Setup device */
    struct uinput_setup setup = {0};
    snprintf(setup.name, UINPUT_MAX_NAME_SIZE, "Zixiao VDI Input");
    setup.id.bustype = BUS_VIRTUAL;
    setup.id.vendor = 0x1AF4;  /* VirtIO vendor */
    setup.id.product = 0x8000;
    setup.id.version = 1;

    if (ioctl(ih->uinput_fd, UI_DEV_SETUP, &setup) < 0) {
        /* Fallback for older kernels */
        struct uinput_user_dev udev = {0};
        snprintf(udev.name, UINPUT_MAX_NAME_SIZE, "Zixiao VDI Input");
        udev.id.bustype = BUS_VIRTUAL;
        udev.id.vendor = 0x1AF4;
        udev.id.product = 0x8000;
        udev.id.version = 1;

        /* Set absolute axis range */
        udev.absmin[ABS_X] = 0;
        udev.absmax[ABS_X] = ih->screen_width;
        udev.absmin[ABS_Y] = 0;
        udev.absmax[ABS_Y] = ih->screen_height;

        if (write(ih->uinput_fd, &udev, sizeof(udev)) != sizeof(udev)) {
            return false;
        }
    } else {
        /* Set absolute axis info */
        struct uinput_abs_setup abs_setup = {0};

        abs_setup.code = ABS_X;
        abs_setup.absinfo.minimum = 0;
        abs_setup.absinfo.maximum = ih->screen_width;
        abs_setup.absinfo.resolution = ih->screen_width;
        ioctl(ih->uinput_fd, UI_ABS_SETUP, &abs_setup);

        abs_setup.code = ABS_Y;
        abs_setup.absinfo.minimum = 0;
        abs_setup.absinfo.maximum = ih->screen_height;
        abs_setup.absinfo.resolution = ih->screen_height;
        ioctl(ih->uinput_fd, UI_ABS_SETUP, &abs_setup);
    }

    /* Create device */
    if (ioctl(ih->uinput_fd, UI_DEV_CREATE) < 0) {
        return false;
    }

    /* Wait for device to be ready */
    usleep(100000);

    return true;
}

static void emit_event(InputHandler *ih, int type, int code, int value) {
    struct input_event ev = {0};
    gettimeofday(&ev.time, NULL);
    ev.type = type;
    ev.code = code;
    ev.value = value;
    write(ih->uinput_fd, &ev, sizeof(ev));
}

static void sync_event(InputHandler *ih) {
    emit_event(ih, EV_SYN, SYN_REPORT, 0);
}

bool input_handler_inject(InputHandler *ih, const InputEvent *event) {
    if (!ih || !ih->initialized || !event) return false;

    switch (event->type) {
        case INPUT_EVENT_MOUSE_MOVE:
            return input_handler_inject_mouse_move(ih, event->mouse.x, event->mouse.y, true);

        case INPUT_EVENT_MOUSE_BUTTON:
            return input_handler_inject_mouse_button(ih, event->mouse.button, event->mouse.pressed);

        case INPUT_EVENT_MOUSE_WHEEL:
            return input_handler_inject_mouse_wheel(ih, event->mouse.wheel_delta);

        case INPUT_EVENT_KEY_DOWN:
        case INPUT_EVENT_KEY_PRESS:
            return input_handler_inject_key(ih, event->key.scan_code, true);

        case INPUT_EVENT_KEY_UP:
            return input_handler_inject_key(ih, event->key.scan_code, false);

        default:
            return false;
    }
}

bool input_handler_inject_mouse_move(InputHandler *ih, int32_t x, int32_t y, bool absolute) {
    if (!ih || !ih->initialized) return false;

    if (absolute) {
        emit_event(ih, EV_ABS, ABS_X, x);
        emit_event(ih, EV_ABS, ABS_Y, y);
    } else {
        emit_event(ih, EV_REL, REL_X, x);
        emit_event(ih, EV_REL, REL_Y, y);
    }

    sync_event(ih);
    return true;
}

bool input_handler_inject_mouse_button(InputHandler *ih, uint32_t button, bool pressed) {
    if (!ih || !ih->initialized) return false;

    int btn_code;
    switch (button) {
        case 1: btn_code = BTN_LEFT; break;
        case 2: btn_code = BTN_RIGHT; break;
        case 3: btn_code = BTN_MIDDLE; break;
        default: return false;
    }

    emit_event(ih, EV_KEY, btn_code, pressed ? 1 : 0);
    sync_event(ih);
    return true;
}

bool input_handler_inject_mouse_wheel(InputHandler *ih, int32_t delta) {
    if (!ih || !ih->initialized) return false;

    /* Normalize delta to +/-1 */
    int value = (delta > 0) ? 1 : ((delta < 0) ? -1 : 0);

    emit_event(ih, EV_REL, REL_WHEEL, value);
    sync_event(ih);
    return true;
}

bool input_handler_inject_key(InputHandler *ih, uint32_t key_code, bool pressed) {
    if (!ih || !ih->initialized) return false;

    /* key_code is expected to be Linux key code */
    emit_event(ih, EV_KEY, key_code, pressed ? 1 : 0);
    sync_event(ih);
    return true;
}

void input_handler_set_screen_size(InputHandler *ih, uint32_t width, uint32_t height) {
    if (!ih) return;

    ih->screen_width = width;
    ih->screen_height = height;

    /* Note: Would need to recreate device to update absolute axis range */
    LOG_DEBUG("Screen size updated: %ux%u", width, height);
}
