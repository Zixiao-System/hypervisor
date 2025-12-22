/*
 * Zixiao VDI Agent for Linux - Audio Capture Implementation
 *
 * Copyright (c) 2025 Zixiao System
 * SPDX-License-Identifier: Apache-2.0
 *
 * Uses PulseAudio for desktop audio capture (monitor source).
 */

#include "audio_capture.h"
#include <pulse/pulseaudio.h>
#include <pulse/simple.h>

struct AudioCapture {
    /* Configuration */
    AudioCallback   callback;
    void           *callback_data;

    /* State */
    bool            running;
    bool            stopping;
    pthread_t       capture_thread;

    /* PulseAudio */
    pa_simple      *pa_stream;
    uint32_t        sample_rate;
    uint16_t        channels;
    uint16_t        bits_per_sample;

    /* Buffer */
    uint8_t        *buffer;
    size_t          buffer_size;
};

static void *capture_thread_func(void *arg);

AudioCapture *audio_capture_create(void) {
    AudioCapture *ac = zixiao_calloc(1, sizeof(AudioCapture));
    if (!ac) return NULL;

    ac->sample_rate = 48000;
    ac->channels = 2;
    ac->bits_per_sample = 16;
    ac->running = false;
    ac->stopping = false;

    return ac;
}

void audio_capture_destroy(AudioCapture *ac) {
    if (!ac) return;

    audio_capture_shutdown(ac);
    zixiao_free(ac);
}

bool audio_capture_init(AudioCapture *ac) {
    if (!ac) return false;

    LOG_INFO("Initializing audio capture...");

    /* Set up PulseAudio sample spec */
    pa_sample_spec spec = {
        .format = PA_SAMPLE_S16LE,
        .rate = ac->sample_rate,
        .channels = ac->channels
    };

    pa_buffer_attr attr = {
        .maxlength = (uint32_t)-1,
        .tlength = (uint32_t)-1,
        .prebuf = (uint32_t)-1,
        .minreq = (uint32_t)-1,
        .fragsize = 1920  /* 20ms at 48kHz stereo 16-bit */
    };

    int error;

    /*
     * Connect to monitor source (desktop audio loopback)
     * The source name can be:
     * - "@DEFAULT_MONITOR@" for default output monitor
     * - Specific monitor like "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor"
     */
    ac->pa_stream = pa_simple_new(
        NULL,                           /* Server name (NULL for default) */
        ZIXIAO_VDI_AGENT_NAME,          /* Application name */
        PA_STREAM_RECORD,               /* Stream direction */
        "@DEFAULT_MONITOR@",            /* Source name (monitor for loopback) */
        "Desktop Audio Capture",        /* Stream description */
        &spec,                          /* Sample spec */
        NULL,                           /* Channel map (NULL for default) */
        &attr,                          /* Buffer attributes */
        &error                          /* Error code */
    );

    if (!ac->pa_stream) {
        LOG_ERROR("Failed to connect to PulseAudio: %s", pa_strerror(error));
        return false;
    }

    /* Allocate buffer (20ms of audio) */
    ac->buffer_size = (ac->sample_rate * ac->channels * (ac->bits_per_sample / 8)) / 50;
    ac->buffer = zixiao_malloc(ac->buffer_size);
    if (!ac->buffer) {
        pa_simple_free(ac->pa_stream);
        ac->pa_stream = NULL;
        return false;
    }

    LOG_INFO("Audio capture initialized: %uHz, %u channels, %u bits",
             ac->sample_rate, ac->channels, ac->bits_per_sample);

    return true;
}

void audio_capture_shutdown(AudioCapture *ac) {
    if (!ac) return;

    audio_capture_stop(ac);

    if (ac->pa_stream) {
        pa_simple_free(ac->pa_stream);
        ac->pa_stream = NULL;
    }

    if (ac->buffer) {
        zixiao_free(ac->buffer);
        ac->buffer = NULL;
    }

    LOG_INFO("Audio capture shutdown");
}

bool audio_capture_start(AudioCapture *ac) {
    if (!ac || !ac->pa_stream || ac->running) return false;

    LOG_INFO("Starting audio capture...");

    ac->stopping = false;
    ac->running = true;

    if (pthread_create(&ac->capture_thread, NULL, capture_thread_func, ac) != 0) {
        LOG_ERROR("Failed to create audio capture thread");
        ac->running = false;
        return false;
    }

    LOG_INFO("Audio capture started");
    return true;
}

void audio_capture_stop(AudioCapture *ac) {
    if (!ac || !ac->running) return;

    LOG_INFO("Stopping audio capture...");

    ac->stopping = true;
    ac->running = false;

    pthread_join(ac->capture_thread, NULL);

    LOG_INFO("Audio capture stopped");
}

bool audio_capture_is_running(AudioCapture *ac) {
    return ac && ac->running;
}

void audio_capture_set_callback(AudioCapture *ac, AudioCallback callback, void *user_data) {
    if (!ac) return;
    ac->callback = callback;
    ac->callback_data = user_data;
}

uint32_t audio_capture_get_sample_rate(AudioCapture *ac) {
    return ac ? ac->sample_rate : 0;
}

uint16_t audio_capture_get_channels(AudioCapture *ac) {
    return ac ? ac->channels : 0;
}

uint16_t audio_capture_get_bits_per_sample(AudioCapture *ac) {
    return ac ? ac->bits_per_sample : 0;
}

static void *capture_thread_func(void *arg) {
    AudioCapture *ac = (AudioCapture *)arg;
    int error;

    LOG_DEBUG("Audio capture thread started");

    while (!ac->stopping) {
        /* Read audio data */
        if (pa_simple_read(ac->pa_stream, ac->buffer, ac->buffer_size, &error) < 0) {
            if (!ac->stopping) {
                LOG_ERROR("Failed to read audio: %s", pa_strerror(error));
            }
            break;
        }

        /* Call callback */
        if (ac->callback) {
            AudioData audio = {
                .data = ac->buffer,
                .data_size = ac->buffer_size,
                .sample_rate = ac->sample_rate,
                .channels = ac->channels,
                .bits_per_sample = ac->bits_per_sample,
                .timestamp = get_timestamp_ms()
            };

            ac->callback(&audio, ac->callback_data);
        }
    }

    LOG_DEBUG("Audio capture thread exiting");
    return NULL;
}
