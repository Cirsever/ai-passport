#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "passport_v2_state.h"

static const char *DIGEST =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

static bool verify_test_hash(const uint8_t *data, size_t length,
                             const char *expected) {
    return data && length == PASSPORT_V2_ASSET_BYTES &&
           strcmp(expected, DIGEST) == 0;
}

static void negotiate_and_select(passport_v2_state_t *state) {
    passport_v2_action_t action;
    assert(passport_v2_apply_line(
        state,
        "{\"type\":\"host.hello\",\"protocol\":2,"
        "\"bridge\":\"0123456789abcdef\",\"sessions\":true,"
        "\"companion\":true}"));
    assert(passport_v2_open_sessions(state));
    assert(passport_v2_take_action(state, &action));
    assert(action.type == PASSPORT_V2_ACTION_SESSION_LIST);
    assert(action.tx == 1);
    assert(passport_v2_apply_line(
        state,
        "{\"type\":\"session.catalog\",\"tx\":1,\"page\":0,"
        "\"count\":2,\"total\":2}"));
    assert(passport_v2_apply_line(
        state,
        "{\"type\":\"session.entry\",\"tx\":1,\"index\":0,"
        "\"sid\":\"s-one\",\"title\":\"Same title\",\"ide\":\"codex\","
        "\"state\":\"idle\",\"writable\":true}"));
    assert(state->session_count == 0);
    assert(passport_v2_apply_line(
        state,
        "{\"type\":\"session.entry\",\"tx\":1,\"index\":1,"
        "\"sid\":\"s-two\",\"title\":\"Same title\",\"ide\":\"codex\","
        "\"state\":\"running\",\"writable\":true}"));
    assert(state->session_count == 2);
    assert(strcmp(state->sessions[0].sid, state->sessions[1].sid) != 0);

    assert(passport_v2_button(state, 1));
    assert(state->session_selected == 1);
    assert(passport_v2_button(state, 2));
    assert(passport_v2_take_action(state, &action));
    assert(action.type == PASSPORT_V2_ACTION_SESSION_SELECT);
    assert(action.tx == 2);
    assert(strcmp(action.sid, "s-two") == 0);

    assert(!passport_v2_apply_line(
        state,
        "{\"type\":\"session.selected\",\"tx\":1,"
        "\"bridge\":\"0123456789abcdef\",\"epoch\":1,\"sid\":\"s-one\","
        "\"ide\":\"codex\",\"title\":\"stale\",\"state\":\"idle\","
        "\"writable\":true}"));
    assert(passport_v2_apply_line(
        state,
        "{\"type\":\"session.selected\",\"tx\":2,"
        "\"bridge\":\"0123456789abcdef\",\"epoch\":1,\"sid\":\"s-two\","
        "\"ide\":\"codex\",\"title\":\"Chosen\",\"state\":\"idle\","
        "\"writable\":true}"));
    assert(strcmp(state->sid, "s-two") == 0);
    assert(state->epoch == 1);
}

static void test_session_catalog_and_stale_transaction(void) {
    passport_v2_state_t state;
    passport_v2_init(&state, verify_test_hash);
    negotiate_and_select(&state);
}

static void test_approval_waits_for_matching_receipt(void) {
    passport_v2_state_t state;
    passport_v2_action_t action;
    passport_v2_init(&state, verify_test_hash);
    negotiate_and_select(&state);

    assert(passport_v2_apply_line(
        &state,
        "{\"type\":\"approval.request\",\"bridge\":\"0123456789abcdef\","
        "\"sid\":\"s-two\",\"epoch\":1,\"request_id\":\"r-7\","
        "\"operation\":\"edit\",\"summary\":\"Edit 3 files\","
        "\"pages\":2,\"allow\":true,\"remaining_ms\":60000}"));
    assert(state.approval_status == PASSPORT_V2_APPROVAL_PENDING);
    assert(passport_v2_button(&state, 1));
    assert(passport_v2_take_action(&state, &action));
    assert(action.type == PASSPORT_V2_ACTION_APPROVAL_DETAIL);
    assert(!passport_v2_apply_line(
        &state,
        "{\"type\":\"approval.page\",\"bridge\":\"0123456789abcdef\","
        "\"sid\":\"s-two\",\"epoch\":1,\"request_id\":\"other\","
        "\"page\":0,\"pages\":2,\"text\":\"wrong\"}"));
    assert(passport_v2_apply_line(
        &state,
        "{\"type\":\"approval.page\",\"bridge\":\"0123456789abcdef\","
        "\"sid\":\"s-two\",\"epoch\":1,\"request_id\":\"r-7\","
        "\"page\":0,\"pages\":2,\"text\":\"main/a.c\"}"));
    assert(passport_v2_button(&state, 2));
    assert(state.approval_status == PASSPORT_V2_APPROVAL_SENDING);
    assert(passport_v2_take_action(&state, &action));
    assert(strcmp(action.decision, "approve") == 0);
    assert(!passport_v2_apply_line(
        &state,
        "{\"type\":\"approval.receipt\",\"bridge\":\"0123456789abcdef\","
        "\"sid\":\"s-two\",\"epoch\":1,\"request_id\":\"other\","
        "\"status\":\"allowed\"}"));
    assert(state.approval_status == PASSPORT_V2_APPROVAL_SENDING);
    assert(passport_v2_apply_line(
        &state,
        "{\"type\":\"approval.receipt\",\"bridge\":\"0123456789abcdef\","
        "\"sid\":\"s-two\",\"epoch\":1,\"request_id\":\"r-7\","
        "\"status\":\"allowed\"}"));
    assert(state.approval_status == PASSPORT_V2_APPROVAL_ALLOWED);
}

static size_t encode_base64(const uint8_t *source, size_t length, char *output) {
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t input = 0;
    size_t used = 0;
    while (input < length) {
        size_t remaining = length - input;
        uint32_t value = (uint32_t)source[input++] << 16;
        if (remaining > 1) value |= (uint32_t)source[input++] << 8;
        if (remaining > 2) value |= source[input++];
        output[used++] = alphabet[(value >> 18) & 63U];
        output[used++] = alphabet[(value >> 12) & 63U];
        output[used++] = remaining > 1 ? alphabet[(value >> 6) & 63U] : '=';
        output[used++] = remaining > 2 ? alphabet[value & 63U] : '=';
    }
    output[used] = '\0';
    return used;
}

static void test_companion_is_atomic_and_offset_checked(void) {
    passport_v2_state_t state;
    passport_v2_action_t action;
    uint8_t payload[PASSPORT_V2_ASSET_BYTES];
    memset(payload, 0x11, sizeof(payload));
    passport_v2_init(&state, verify_test_hash);
    negotiate_and_select(&state);

    char line[512];
    snprintf(line, sizeof(line),
             "{\"type\":\"companion.begin\",\"bridge\":\"0123456789abcdef\","
             "\"sid\":\"s-two\",\"epoch\":1,\"asset\":\"%s\","
             "\"bytes\":544,\"frames\":1,\"name\":\"pet\"}", DIGEST);
    assert(passport_v2_apply_line(&state, line));
    assert(passport_v2_companion(&state) == NULL);
    assert(passport_v2_take_action(&state, &action));
    assert(strcmp(action.status, "receiving") == 0);

    snprintf(line, sizeof(line),
             "{\"type\":\"companion.chunk\",\"asset\":\"%s\","
             "\"offset\":8,\"data\":\"EREREREREREREREREREREQ==\"}", DIGEST);
    assert(passport_v2_apply_line(&state, line));
    assert(passport_v2_take_action(&state, &action));
    assert(strcmp(action.status, "retry") == 0);
    assert(action.offset == 0);

    for (size_t offset = 0; offset < sizeof(payload); offset += 128) {
        size_t length = sizeof(payload) - offset;
        if (length > 128) length = 128;
        char encoded[176];
        encode_base64(payload + offset, length, encoded);
        snprintf(line, sizeof(line),
                 "{\"type\":\"companion.chunk\",\"asset\":\"%s\","
                 "\"offset\":%zu,\"data\":\"%s\"}",
                 DIGEST, offset, encoded);
        assert(passport_v2_apply_line(&state, line));
        assert(passport_v2_take_action(&state, &action));
        assert(action.offset == offset + length);
    }
    assert(passport_v2_companion(&state) == NULL);
    snprintf(line, sizeof(line),
             "{\"type\":\"companion.commit\",\"bridge\":\"0123456789abcdef\","
             "\"sid\":\"s-two\",\"epoch\":1,\"asset\":\"%s\"}", DIGEST);
    assert(passport_v2_apply_line(&state, line));
    assert(passport_v2_take_action(&state, &action));
    assert(strcmp(action.status, "ready") == 0);
    const passport_v2_companion_t *asset = passport_v2_companion(&state);
    assert(asset != NULL);
    assert(memcmp(asset->bytes, payload, sizeof(payload)) == 0);
}

static void test_battery_segments(void) {
    assert(passport_v2_battery_segments(-1) == -1);
    assert(passport_v2_battery_segments(0) == 0);
    assert(passport_v2_battery_segments(1) == 1);
    assert(passport_v2_battery_segments(25) == 1);
    assert(passport_v2_battery_segments(26) == 2);
    assert(passport_v2_battery_segments(100) == 4);
    assert(passport_v2_battery_segments(101) == -1);
}

int main(void) {
    test_session_catalog_and_stale_transaction();
    test_approval_waits_for_matching_receipt();
    test_companion_is_atomic_and_offset_checked();
    test_battery_segments();
    puts("passport_v2_state tests passed");
    return 0;
}
