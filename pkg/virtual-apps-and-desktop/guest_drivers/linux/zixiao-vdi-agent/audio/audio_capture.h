/*
 * Zixiao VDI Agent for Linux - Audio Capture
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZIXIAO_VDI_AUDIO_CAPTURE_H
#define ZIXIAO_VDI_AUDIO_CAPTURE_H

#include "../src/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Audio capture context (opaque) */
typedef struct AudioCapture AudioCapture;

/* Create/destroy */
AudioCapture *audio_capture_create(void);
void audio_capture_destroy(AudioCapture *ac);

/* Initialize/shutdown */
bool audio_capture_init(AudioCapture *ac);
void audio_capture_shutdown(AudioCapture *ac);

/* Control */
bool audio_capture_start(AudioCapture *ac);
void audio_capture_stop(AudioCapture *ac);
bool audio_capture_is_running(AudioCapture *ac);

/* Configuration */
void audio_capture_set_callback(AudioCapture *ac, AudioCallback callback, void *user_data);

/* Query */
uint32_t audio_capture_get_sample_rate(AudioCapture *ac);
uint16_t audio_capture_get_channels(AudioCapture *ac);
uint16_t audio_capture_get_bits_per_sample(AudioCapture *ac);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_VDI_AUDIO_CAPTURE_H */
