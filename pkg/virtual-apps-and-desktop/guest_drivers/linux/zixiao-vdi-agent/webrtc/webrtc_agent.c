/*
 * Zixiao VDI Agent for Linux - WebRTC Agent Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Note: This is a simplified implementation.
 * For production, integrate with libwebrtc or GStreamer WebRTC.
 */

#include "webrtc_agent.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <poll.h>

struct WebRTCAgent {
    /* Configuration */
    WebRTCAgentConfig config;
    InputCallback     input_callback;
    void             *input_callback_data;

    /* State */
    bool              running;
    bool              stopping;
    bool              connected;
    pthread_t         signaling_thread;

    /* WebSocket */
    int               ws_fd;
    char              signaling_url[512];

    /* Peer connection state */
    bool              peer_connected;
};

static void *signaling_thread_func(void *arg);
static bool connect_to_signaling(WebRTCAgent *wa);
static bool send_ws_message(WebRTCAgent *wa, const char *message);
static void process_signaling_message(WebRTCAgent *wa, const char *message);

WebRTCAgent *webrtc_agent_create(void) {
    WebRTCAgent *wa = zixiao_calloc(1, sizeof(WebRTCAgent));
    if (!wa) return NULL;

    wa->ws_fd = -1;
    wa->running = false;
    wa->stopping = false;
    wa->connected = false;
    wa->peer_connected = false;

    return wa;
}

void webrtc_agent_destroy(WebRTCAgent *wa) {
    if (!wa) return;

    webrtc_agent_shutdown(wa);
    zixiao_free(wa);
}

bool webrtc_agent_init(WebRTCAgent *wa, const WebRTCAgentConfig *config) {
    if (!wa) return false;

    LOG_INFO("Initializing WebRTC agent...");

    if (config) {
        wa->config = *config;
        if (config->signaling_url) {
            strncpy(wa->signaling_url, config->signaling_url, sizeof(wa->signaling_url) - 1);
        }
    }

    if (wa->signaling_url[0] == '\0') {
        strncpy(wa->signaling_url, "ws://localhost:8080/signaling", sizeof(wa->signaling_url) - 1);
    }

    LOG_INFO("WebRTC agent initialized (signaling: %s)", wa->signaling_url);
    return true;
}

void webrtc_agent_shutdown(WebRTCAgent *wa) {
    if (!wa) return;

    webrtc_agent_stop(wa);

    LOG_INFO("WebRTC agent shutdown");
}

bool webrtc_agent_start(WebRTCAgent *wa) {
    if (!wa || wa->running) return false;

    LOG_INFO("Starting WebRTC agent...");

    wa->stopping = false;
    wa->running = true;

    /* Start signaling thread */
    if (pthread_create(&wa->signaling_thread, NULL, signaling_thread_func, wa) != 0) {
        LOG_ERROR("Failed to create WebRTC signaling thread");
        wa->running = false;
        return false;
    }

    LOG_INFO("WebRTC agent started");
    return true;
}

void webrtc_agent_stop(WebRTCAgent *wa) {
    if (!wa || !wa->running) return;

    LOG_INFO("Stopping WebRTC agent...");

    wa->stopping = true;
    wa->running = false;

    if (wa->ws_fd >= 0) {
        close(wa->ws_fd);
        wa->ws_fd = -1;
    }

    pthread_join(wa->signaling_thread, NULL);

    LOG_INFO("WebRTC agent stopped");
}

bool webrtc_agent_is_running(WebRTCAgent *wa) {
    return wa && wa->running;
}

bool webrtc_agent_is_connected(WebRTCAgent *wa) {
    return wa && wa->peer_connected;
}

void webrtc_agent_set_input_callback(WebRTCAgent *wa, InputCallback callback, void *user_data) {
    if (!wa) return;
    wa->input_callback = callback;
    wa->input_callback_data = user_data;
}

static bool parse_ws_url(const char *url, char *host, int host_len, int *port, char *path, int path_len) {
    /* Parse ws://host:port/path */
    const char *p = url;

    if (strncmp(p, "ws://", 5) == 0) {
        p += 5;
    } else if (strncmp(p, "wss://", 6) == 0) {
        p += 6;
    } else {
        return false;
    }

    /* Find port or path */
    const char *port_start = strchr(p, ':');
    const char *path_start = strchr(p, '/');

    if (port_start && (!path_start || port_start < path_start)) {
        int host_size = port_start - p;
        if (host_size >= host_len) host_size = host_len - 1;
        strncpy(host, p, host_size);
        host[host_size] = '\0';

        *port = atoi(port_start + 1);
    } else if (path_start) {
        int host_size = path_start - p;
        if (host_size >= host_len) host_size = host_len - 1;
        strncpy(host, p, host_size);
        host[host_size] = '\0';

        *port = 80;
    } else {
        strncpy(host, p, host_len - 1);
        *port = 80;
    }

    if (path_start) {
        strncpy(path, path_start, path_len - 1);
    } else {
        strcpy(path, "/");
    }

    return true;
}

static bool connect_to_signaling(WebRTCAgent *wa) {
    char host[256] = {0};
    char path[256] = {0};
    int port = 80;

    if (!parse_ws_url(wa->signaling_url, host, sizeof(host), &port, path, sizeof(path))) {
        LOG_ERROR("Failed to parse signaling URL");
        return false;
    }

    LOG_INFO("Connecting to signaling server: %s:%d%s", host, port, path);

    /* Resolve hostname */
    struct hostent *he = gethostbyname(host);
    if (!he) {
        LOG_ERROR("Failed to resolve host: %s", host);
        return false;
    }

    /* Create socket */
    wa->ws_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (wa->ws_fd < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return false;
    }

    /* Connect */
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    memcpy(&addr.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(wa->ws_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to connect: %s", strerror(errno));
        close(wa->ws_fd);
        wa->ws_fd = -1;
        return false;
    }

    /* Send WebSocket upgrade request */
    char request[1024];
    snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, port);

    if (send(wa->ws_fd, request, strlen(request), 0) < 0) {
        LOG_ERROR("Failed to send upgrade request");
        close(wa->ws_fd);
        wa->ws_fd = -1;
        return false;
    }

    /* Read response */
    char response[1024];
    ssize_t n = recv(wa->ws_fd, response, sizeof(response) - 1, 0);
    if (n <= 0) {
        LOG_ERROR("Failed to receive upgrade response");
        close(wa->ws_fd);
        wa->ws_fd = -1;
        return false;
    }
    response[n] = '\0';

    /* Check for 101 Switching Protocols */
    if (strstr(response, "101") == NULL) {
        LOG_ERROR("WebSocket upgrade failed: %s", response);
        close(wa->ws_fd);
        wa->ws_fd = -1;
        return false;
    }

    wa->connected = true;
    LOG_INFO("Connected to signaling server");

    return true;
}

static bool send_ws_message(WebRTCAgent *wa, const char *message) {
    if (!wa || wa->ws_fd < 0 || !message) return false;

    size_t len = strlen(message);

    /* Simple WebSocket frame (text, unmasked - server to client typically doesn't mask) */
    uint8_t frame[10];
    int frame_len = 2;

    frame[0] = 0x81;  /* FIN + text opcode */

    if (len < 126) {
        frame[1] = 0x80 | len;  /* Mask bit + length */
    } else if (len < 65536) {
        frame[1] = 0x80 | 126;
        frame[2] = (len >> 8) & 0xFF;
        frame[3] = len & 0xFF;
        frame_len = 4;
    } else {
        return false;  /* Too large */
    }

    /* Add mask key */
    uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    memcpy(frame + frame_len, mask, 4);
    frame_len += 4;

    /* Send frame header */
    if (send(wa->ws_fd, frame, frame_len, 0) != frame_len) {
        return false;
    }

    /* Send masked payload */
    uint8_t *masked = zixiao_malloc(len);
    if (!masked) return false;

    for (size_t i = 0; i < len; i++) {
        masked[i] = message[i] ^ mask[i % 4];
    }

    ssize_t sent = send(wa->ws_fd, masked, len, 0);
    zixiao_free(masked);

    return sent == (ssize_t)len;
}

static void process_signaling_message(WebRTCAgent *wa, const char *message) {
    LOG_DEBUG("Signaling message: %s", message);

    /* Parse simple JSON */
    const char *type_start = strstr(message, "\"type\"");
    if (!type_start) return;

    if (strstr(message, "\"offer\"")) {
        LOG_INFO("Received SDP offer");
        wa->peer_connected = true;

        /* Send simple answer */
        const char *answer = "{\"type\":\"answer\",\"payload\":\"\"}";
        send_ws_message(wa, answer);
    } else if (strstr(message, "\"answer\"")) {
        LOG_INFO("Received SDP answer");
        wa->peer_connected = true;
    } else if (strstr(message, "\"ice\"")) {
        LOG_DEBUG("Received ICE candidate");
    } else if (strstr(message, "\"input\"")) {
        /* Parse input event */
        if (wa->input_callback) {
            /* Simple input parsing - would need proper JSON parsing in production */
            const char *data = strstr(message, "\"data\"");
            if (data) {
                InputEvent event = {0};
                event.type = INPUT_EVENT_MOUSE_MOVE;
                event.timestamp = get_timestamp_ms();
                /* Parse x, y from data... */
                wa->input_callback(&event, wa->input_callback_data);
            }
        }
    }
}

static void *signaling_thread_func(void *arg) {
    WebRTCAgent *wa = (WebRTCAgent *)arg;

    LOG_DEBUG("WebRTC signaling thread started");

    while (!wa->stopping) {
        /* Connect if not connected */
        if (!wa->connected) {
            if (!connect_to_signaling(wa)) {
                /* Wait before retry */
                sleep(5);
                continue;
            }
        }

        /* Poll for messages */
        struct pollfd pfd = {
            .fd = wa->ws_fd,
            .events = POLLIN
        };

        int ret = poll(&pfd, 1, 100);

        if (ret < 0) {
            if (errno != EINTR) {
                LOG_ERROR("poll failed: %s", strerror(errno));
                wa->connected = false;
            }
            continue;
        }

        if (ret == 0) continue;

        if (pfd.revents & (POLLHUP | POLLERR)) {
            LOG_INFO("Signaling connection closed");
            close(wa->ws_fd);
            wa->ws_fd = -1;
            wa->connected = false;
            wa->peer_connected = false;
            continue;
        }

        if (pfd.revents & POLLIN) {
            uint8_t buffer[4096];
            ssize_t n = recv(wa->ws_fd, buffer, sizeof(buffer), 0);

            if (n <= 0) {
                LOG_INFO("Signaling connection closed");
                close(wa->ws_fd);
                wa->ws_fd = -1;
                wa->connected = false;
                wa->peer_connected = false;
                continue;
            }

            /* Parse WebSocket frame */
            if (n >= 2) {
                uint8_t opcode = buffer[0] & 0x0F;
                uint8_t len = buffer[1] & 0x7F;
                int offset = 2;

                if (len == 126 && n >= 4) {
                    len = (buffer[2] << 8) | buffer[3];
                    offset = 4;
                }

                if (opcode == 0x01 && offset + len <= n) {  /* Text frame */
                    buffer[offset + len] = '\0';
                    process_signaling_message(wa, (char *)(buffer + offset));
                } else if (opcode == 0x08) {  /* Close frame */
                    LOG_INFO("WebSocket close frame received");
                    close(wa->ws_fd);
                    wa->ws_fd = -1;
                    wa->connected = false;
                    wa->peer_connected = false;
                }
            }
        }
    }

    if (wa->ws_fd >= 0) {
        close(wa->ws_fd);
        wa->ws_fd = -1;
    }

    LOG_DEBUG("WebRTC signaling thread exiting");
    return NULL;
}

bool webrtc_agent_send_frame(WebRTCAgent *wa, const FrameData *frame) {
    if (!wa || !wa->peer_connected || !frame) return false;

    /* In a full implementation:
     * 1. Encode frame to H.264 (using libx264 or hardware encoder)
     * 2. Package in RTP
     * 3. Send via DTLS-SRTP over UDP
     */

    (void)frame;
    return true;
}

bool webrtc_agent_send_audio(WebRTCAgent *wa, const AudioData *audio) {
    if (!wa || !wa->peer_connected || !audio) return false;

    /* In a full implementation:
     * 1. Encode audio to Opus
     * 2. Package in RTP
     * 3. Send via DTLS-SRTP over UDP
     */

    (void)audio;
    return true;
}
