/*
 * Zixiao VDI Agent for Linux - Input Handler
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZIXIAO_VDI_INPUT_HANDLER_H
#define ZIXIAO_VDI_INPUT_HANDLER_H

#include "../src/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Input handler context (opaque) */
typedef struct InputHandler InputHandler;

/* Create/destroy */
InputHandler *input_handler_create(void);
void input_handler_destroy(InputHandler *ih);

/* Initialize/shutdown */
bool input_handler_init(InputHandler *ih);
void input_handler_shutdown(InputHandler *ih);

/* Inject input */
bool input_handler_inject(InputHandler *ih, const InputEvent *event);
bool input_handler_inject_mouse_move(InputHandler *ih, int32_t x, int32_t y, bool absolute);
bool input_handler_inject_mouse_button(InputHandler *ih, uint32_t button, bool pressed);
bool input_handler_inject_mouse_wheel(InputHandler *ih, int32_t delta);
bool input_handler_inject_key(InputHandler *ih, uint32_t key_code, bool pressed);

/* Configuration */
void input_handler_set_screen_size(InputHandler *ih, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_VDI_INPUT_HANDLER_H */
