/*
 * Zixiao VDI Agent for Linux - WebRTC Agent
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZIXIAO_VDI_WEBRTC_AGENT_H
#define ZIXIAO_VDI_WEBRTC_AGENT_H

#include "../src/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WebRTC agent configuration */
typedef struct {
    const char *signaling_url;
} WebRTCAgentConfig;

/* WebRTC agent context (opaque) */
typedef struct WebRTCAgent WebRTCAgent;

/* Create/destroy */
WebRTCAgent *webrtc_agent_create(void);
void webrtc_agent_destroy(WebRTCAgent *wa);

/* Initialize/shutdown */
bool webrtc_agent_init(WebRTCAgent *wa, const WebRTCAgentConfig *config);
void webrtc_agent_shutdown(WebRTCAgent *wa);

/* Control */
bool webrtc_agent_start(WebRTCAgent *wa);
void webrtc_agent_stop(WebRTCAgent *wa);
bool webrtc_agent_is_running(WebRTCAgent *wa);
bool webrtc_agent_is_connected(WebRTCAgent *wa);

/* Callbacks */
void webrtc_agent_set_input_callback(WebRTCAgent *wa, InputCallback callback, void *user_data);

/* Send data to remote */
bool webrtc_agent_send_frame(WebRTCAgent *wa, const FrameData *frame);
bool webrtc_agent_send_audio(WebRTCAgent *wa, const AudioData *audio);

#ifdef __cplusplus
}
#endif

#endif /* ZIXIAO_VDI_WEBRTC_AGENT_H */
