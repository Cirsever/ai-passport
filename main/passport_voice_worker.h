// Passport Voice Worker — Slice D minimal audio pipeline.
//
// Long-press OK on the device asks the host adapter to run one utterance.
// Until Slice D + P0-5 (real STT) lands the worker just:
//   1. reads PCM16 mono at 16 kHz from bsp_audio in 20 ms blocks;
//   2. runs a pure-C VAD to decide when the utterance ends;
//   3. emits a bounded burst of Passport frames over USB:
//        - one voice.capture.start (with request_id + sample_rate)
//        - up to `chunk_cap` voice.capture.audio chunks (base64 PCM)
//        - one voice.capture.stop (with duration_ms, reason, peak_level)
//
// The IDE-side adapter still runs a text-only path when it sees stop; the
// stop frame carries a diagnostic `text` placeholder ("[voice N chunks
// XXXms peak=YY]") so the operator sees something concrete in the Trae /
// Codex chat window without a real STT service yet. When STT arrives it
// replaces the placeholder line and the audio chunks become useful.
//
// The worker runs as its own FreeRTOS task so bsp_audio_read may block
// on I2S DMA without stalling the LVGL timer or the button worker. Only
// one utterance runs at a time. OK long-press starts capture and releasing
// OK requests a manual stop.
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// State snapshot for the UI. Read by demo_passport_service every UI tick
// (guarded by an internal mutex).
typedef struct {
    bool     active;        // worker is currently recording
    bool     completed;     // last utterance ended; cleared by the next begin
    uint16_t elapsed_ms;    // audio consumed so far (bounded by max_ms)
    uint16_t max_ms;        // hard cap; matches VAD max_utterance_ms
    uint8_t  level;         // 0..100 UI meter
    uint16_t chunks_sent;   // number of voice.capture.audio frames emitted
} passport_voice_status_t;

// Initialize the worker task (idempotent). Requires bsp_audio_init() to
// have run. Safe to call multiple times.
esp_err_t passport_voice_worker_start(void);

// Ask the worker to begin one utterance. No-ops if one is already active.
// Returns ESP_OK on successful notification, ESP_ERR_INVALID_STATE if the
// worker has not been started.
esp_err_t passport_voice_worker_begin(void);

// Pin all frames in the next utterance to one negotiated v2 route. Passing
// NULL clears the route and retains protocol-1 framing.
void passport_voice_worker_set_route(const char *bridge, const char *sid,
                                     uint32_t epoch);

// Force-stop the current utterance. Cheap when nothing is active.
esp_err_t passport_voice_worker_manual_stop(void);

// Snapshot the current worker state (thread-safe).
void passport_voice_worker_snapshot(passport_voice_status_t *out);

// Stop the task and release resources. Must be called before deleting the
// screen that references voice state, per the AGENTS.md demo teardown rule.
esp_err_t passport_voice_worker_stop(void);
