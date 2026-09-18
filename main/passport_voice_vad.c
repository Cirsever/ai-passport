#include "passport_voice_vad.h"

#include <stdlib.h>

void passport_voice_vad_init(passport_voice_vad_t *vad) {
    if (!vad) return;
    vad->sample_rate_hz = 16000;
    vad->block_ms = 20;
    vad->voice_start_ms = 100;
    vad->silence_hangover_ms = 800;
    vad->max_utterance_ms = 15000;
    vad->voice_amp_threshold = 1200;
    vad->utterance_ms = 0;
    vad->voiced_run_ms = 0;
    vad->last_voice_ms = 0;
    vad->level = 0;
    vad->voice_seen = false;
    vad->stopped = false;
    vad->stop_reason = PASSPORT_VOICE_STOP_NONE;
}

static uint16_t peak_amplitude(const int16_t *samples, size_t n) {
    uint16_t peak = 0;
    for (size_t i = 0; i < n; ++i) {
        int32_t v = samples[i];
        if (v < 0) v = -v;
        if (v > peak) peak = (uint16_t)v;
    }
    return peak;
}

void passport_voice_vad_feed(passport_voice_vad_t *vad,
                             const int16_t *samples, size_t sample_count) {
    if (!vad || vad->stopped) return;
    uint16_t peak = (samples && sample_count) ?
                    peak_amplitude(samples, sample_count) : 0;
    // Map peak → 0..100 with an ~8:1 slope so quiet speech shows a visible
    // bar; clamp at 100 (loud) so the meter never overflows.
    uint32_t level = (uint32_t)peak / 300u;
    if (level > 100u) level = 100u;
    vad->level = (uint8_t)level;

    const bool voiced = peak >= (uint16_t)vad->voice_amp_threshold;
    vad->utterance_ms = (uint32_t)(vad->utterance_ms + vad->block_ms);
    if (voiced) {
        vad->voiced_run_ms =
            (uint32_t)(vad->voiced_run_ms + vad->block_ms);
        if (vad->voiced_run_ms >= (uint32_t)vad->voice_start_ms) {
            vad->voice_seen = true;
        }
        vad->last_voice_ms = 0;
    } else {
        vad->voiced_run_ms = 0;
        vad->last_voice_ms = (uint32_t)(vad->last_voice_ms + vad->block_ms);
    }

    if (vad->voice_seen &&
        vad->last_voice_ms >= (uint32_t)vad->silence_hangover_ms) {
        vad->stopped = true;
        vad->stop_reason = PASSPORT_VOICE_STOP_SILENCE;
        return;
    }
    if (vad->utterance_ms >= (uint32_t)vad->max_utterance_ms) {
        vad->stopped = true;
        vad->stop_reason = PASSPORT_VOICE_STOP_MAX_DURATION;
    }
}

void passport_voice_vad_stop(passport_voice_vad_t *vad) {
    if (!vad || vad->stopped) return;
    vad->stopped = true;
    vad->stop_reason = PASSPORT_VOICE_STOP_MANUAL;
}
