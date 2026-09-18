// Host tests for passport_voice_vad. Pure C, no IDF dependency — mirrors
// the state-machine-first testing rule in AGENTS.md.
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "passport_voice_vad.h"

// Fill a 20 ms block (320 s16 samples) with a constant peak amplitude.
static void fill_block(int16_t *buf, size_t n, int16_t peak) {
    for (size_t i = 0; i < n; ++i) buf[i] = (i & 1) ? peak : (int16_t)-peak;
}

static void test_defaults_are_sane(void) {
    passport_voice_vad_t v;
    passport_voice_vad_init(&v);
    assert(v.sample_rate_hz == 16000);
    assert(v.block_ms == 20);
    assert(v.voice_start_ms == 100);
    assert(v.silence_hangover_ms == 800);
    assert(v.max_utterance_ms == 15000);
    assert(v.stopped == false);
    assert(v.stop_reason == PASSPORT_VOICE_STOP_NONE);
    assert(v.voice_seen == false);
    assert(v.level == 0);
}

static void test_pure_silence_never_stops_until_max_duration(void) {
    passport_voice_vad_t v;
    passport_voice_vad_init(&v);
    // Small max so the test stays fast.
    v.max_utterance_ms = 200;
    int16_t block[320] = {0};
    // 10 blocks × 20 ms = 200 ms.
    for (int i = 0; i < 10 && !v.stopped; ++i) {
        passport_voice_vad_feed(&v, block, 320);
    }
    assert(v.stopped == true);
    assert(v.stop_reason == PASSPORT_VOICE_STOP_MAX_DURATION);
    assert(v.voice_seen == false);
}

static void test_voice_then_silence_hangover_triggers_silence_stop(void) {
    passport_voice_vad_t v;
    passport_voice_vad_init(&v);
    v.silence_hangover_ms = 60;  // 3 blocks of silence
    int16_t voice[320];
    int16_t quiet[320] = {0};
    fill_block(voice, 320, 5000);  // above threshold 1200

    // 5 voice blocks (100 ms) satisfy the onset guard.
    for (int i = 0; i < 5; ++i) passport_voice_vad_feed(&v, voice, 320);
    assert(v.voice_seen == true);
    assert(v.stopped == false);
    assert(v.level > 0);
    assert(v.last_voice_ms == 0);

    // 3 silent blocks — exactly hits the hangover.
    for (int i = 0; i < 3 && !v.stopped; ++i)
        passport_voice_vad_feed(&v, quiet, 320);
    assert(v.stopped == true);
    assert(v.stop_reason == PASSPORT_VOICE_STOP_SILENCE);
}

static void test_manual_stop_takes_precedence(void) {
    passport_voice_vad_t v;
    passport_voice_vad_init(&v);
    int16_t voice[320];
    fill_block(voice, 320, 5000);
    passport_voice_vad_feed(&v, voice, 320);
    passport_voice_vad_stop(&v);
    assert(v.stopped == true);
    assert(v.stop_reason == PASSPORT_VOICE_STOP_MANUAL);
    // Feeding after stop must not un-stop.
    passport_voice_vad_feed(&v, voice, 320);
    assert(v.stopped == true);
    assert(v.stop_reason == PASSPORT_VOICE_STOP_MANUAL);
}

static void test_level_scales_with_amplitude(void) {
    passport_voice_vad_t v;
    passport_voice_vad_init(&v);
    int16_t quiet[320];
    fill_block(quiet, 320, 1500);
    passport_voice_vad_feed(&v, quiet, 320);
    uint8_t low = v.level;

    passport_voice_vad_init(&v);
    int16_t loud[320];
    fill_block(loud, 320, 15000);
    passport_voice_vad_feed(&v, loud, 320);
    uint8_t high = v.level;

    assert(low > 0);
    assert(high > low);
    assert(high <= 100);
}

static void test_below_threshold_is_not_voice(void) {
    passport_voice_vad_t v;
    passport_voice_vad_init(&v);
    v.silence_hangover_ms = 40;
    int16_t buf[320];
    // 500 is below the 1200 threshold — no voice, no stop.
    fill_block(buf, 320, 500);
    for (int i = 0; i < 5; ++i) passport_voice_vad_feed(&v, buf, 320);
    assert(v.voice_seen == false);
    assert(v.stopped == false);  // no voice → hangover never fires
}

static void test_short_transient_does_not_arm_silence_stop(void) {
    passport_voice_vad_t v;
    passport_voice_vad_init(&v);
    int16_t click[320];
    int16_t quiet[320] = {0};
    fill_block(click, 320, 5000);

    // A button click or handling noise shorter than 100 ms must not count as
    // speech and cause an early silence stop.
    for (int i = 0; i < 4; ++i) passport_voice_vad_feed(&v, click, 320);
    for (int i = 0; i < 50; ++i) passport_voice_vad_feed(&v, quiet, 320);

    assert(v.voice_seen == false);
    assert(v.stopped == false);
}

int main(void) {
    test_defaults_are_sane();
    test_pure_silence_never_stops_until_max_duration();
    test_voice_then_silence_hangover_triggers_silence_stop();
    test_manual_stop_takes_precedence();
    test_level_scales_with_amplitude();
    test_below_threshold_is_not_voice();
    test_short_transient_does_not_arm_silence_stop();
    printf("passport_voice_vad: PASS (7 tests)\n");
    return 0;
}
