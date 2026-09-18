// Passport Voice VAD — pure C, no IDF/LVGL. Trivially host-testable.
//
// The MVP goal is to answer three questions each block of PCM16 mono audio:
//   1. Is this block "loud enough" to be voice?
//   2. Has silence lasted long enough to end the utterance?
//   3. What normalized level (0..100) should the UI show as a meter?
//
// This module owns the decision. The IDF worker on device — and the host
// tests — feed it fixed-size PCM16 blocks and drive I/O off its answers.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    // Static configuration. All durations are in milliseconds of audio.
    uint16_t sample_rate_hz;
    uint16_t block_ms;                 // size of one feed() call
    uint16_t voice_start_ms;           // continuous voice required for onset
    uint16_t silence_hangover_ms;      // silence tail before stop is signaled
    uint16_t max_utterance_ms;         // hard cap; guarantees termination
    int16_t  voice_amp_threshold;      // PCM16 peak amplitude to count as voice

    // Runtime state.
    uint32_t utterance_ms;             // total audio consumed since start
    uint32_t voiced_run_ms;             // current continuous voiced duration
    uint32_t last_voice_ms;            // ms elapsed since a voiced block
    uint8_t  level;                    // last computed 0..100 UI meter
    bool     voice_seen;               // any voiced block since start
    bool     stopped;                  // once true, feed() is a no-op
    uint16_t stop_reason;              // PASSPORT_VOICE_STOP_*
} passport_voice_vad_t;

enum {
    PASSPORT_VOICE_STOP_NONE = 0,
    PASSPORT_VOICE_STOP_SILENCE,     // hangover exceeded after voice_seen
    PASSPORT_VOICE_STOP_MAX_DURATION,// max_utterance_ms hit
    PASSPORT_VOICE_STOP_MANUAL,      // caller invoked passport_voice_vad_stop()
};

// Initialize with sane defaults: 16 kHz mono, 20 ms blocks, 100 ms continuous
// voice onset, 800 ms silence hangover, 15 s hard cap, and PCM16 amplitude
// threshold ~1200 (roughly quiet office speech through the ES8311 default gain).
void passport_voice_vad_init(passport_voice_vad_t *vad);

// Feed one block of PCM16 mono. `samples` may be NULL if block_ms is zero.
// After this call:
//   - vad->level is updated (0..100 for UI meter).
//   - vad->stopped becomes true when the utterance is done.
//   - vad->stop_reason reflects why.
// Blocks arriving after stop are ignored (defensive, cheap).
void passport_voice_vad_feed(passport_voice_vad_t *vad,
                             const int16_t *samples, size_t sample_count);

// Force-stop the utterance (e.g. a second long-press). Idempotent.
void passport_voice_vad_stop(passport_voice_vad_t *vad);
