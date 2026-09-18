#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_V2_BRIDGE_MAX 17U
#define PASSPORT_V2_SID_MAX 24U
#define PASSPORT_V2_TITLE_MAX 64U
#define PASSPORT_V2_SUMMARY_MAX 96U
#define PASSPORT_V2_REQUEST_MAX 48U
#define PASSPORT_V2_ASSET_HEX 65U
#define PASSPORT_V2_SESSION_PAGE 3U
#define PASSPORT_V2_PALETTE_BYTES 32U
#define PASSPORT_V2_FRAME_BYTES 512U
#define PASSPORT_V2_ASSET_BYTES (PASSPORT_V2_PALETTE_BYTES + PASSPORT_V2_FRAME_BYTES)

typedef enum {
    PASSPORT_V2_VIEW_HOME = 0,
    PASSPORT_V2_VIEW_SESSIONS,
    PASSPORT_V2_VIEW_SWITCHING,
    PASSPORT_V2_VIEW_APPROVAL,
    PASSPORT_V2_VIEW_APPROVAL_DETAIL,
} passport_v2_view_t;

typedef enum {
    PASSPORT_V2_APPROVAL_NONE = 0,
    PASSPORT_V2_APPROVAL_PENDING,
    PASSPORT_V2_APPROVAL_SENDING,
    PASSPORT_V2_APPROVAL_ALLOWED,
    PASSPORT_V2_APPROVAL_DENIED,
    PASSPORT_V2_APPROVAL_EXPIRED,
    PASSPORT_V2_APPROVAL_UNKNOWN,
} passport_v2_approval_status_t;

typedef enum {
    PASSPORT_V2_ACTION_NONE = 0,
    PASSPORT_V2_ACTION_SESSION_LIST,
    PASSPORT_V2_ACTION_SESSION_SELECT,
    PASSPORT_V2_ACTION_SESSION_QUERY,
    PASSPORT_V2_ACTION_APPROVAL_DETAIL,
    PASSPORT_V2_ACTION_APPROVAL_DECISION,
    PASSPORT_V2_ACTION_COMPANION_ACK,
} passport_v2_action_type_t;

typedef struct {
    char sid[PASSPORT_V2_SID_MAX];
    char title[PASSPORT_V2_TITLE_MAX];
    char ide[12];
    char state[12];
    bool writable;
} passport_v2_session_t;

typedef struct {
    uint8_t bytes[PASSPORT_V2_ASSET_BYTES];
    char digest[PASSPORT_V2_ASSET_HEX];
    uint32_t generation;
    bool valid;
} passport_v2_companion_t;

typedef struct {
    passport_v2_action_type_t type;
    uint32_t tx;
    uint32_t page;
    uint32_t offset;
    char sid[PASSPORT_V2_SID_MAX];
    char request_id[PASSPORT_V2_REQUEST_MAX];
    char decision[8];
    char asset[PASSPORT_V2_ASSET_HEX];
    char status[12];
} passport_v2_action_t;

typedef bool (*passport_v2_hash_verify_fn)(const uint8_t *data, size_t length,
                                           const char *expected_hex);

typedef struct {
    bool negotiated;
    bool sessions_capable;
    bool companion_capable;
    char bridge[PASSPORT_V2_BRIDGE_MAX];
    char sid[PASSPORT_V2_SID_MAX];
    char ide[12];
    char title[PASSPORT_V2_TITLE_MAX];
    char session_state[12];
    uint32_t epoch;
    bool writable;

    uint32_t next_tx;
    uint32_t pending_tx;
    uint32_t catalog_page;
    uint32_t catalog_total;
    passport_v2_session_t sessions[PASSPORT_V2_SESSION_PAGE];
    passport_v2_session_t staged_sessions[PASSPORT_V2_SESSION_PAGE];
    size_t session_count;
    size_t staged_count;
    size_t staged_received;
    uint8_t staged_mask;
    size_t session_selected;
    passport_v2_view_t view;

    char approval_request_id[PASSPORT_V2_REQUEST_MAX];
    char approval_operation[12];
    char approval_summary[PASSPORT_V2_SUMMARY_MAX];
    char approval_detail[PASSPORT_V2_SUMMARY_MAX];
    uint32_t approval_pages;
    uint32_t approval_page;
    uint32_t approval_remaining_ms;
    bool approval_allow;
    passport_v2_approval_status_t approval_status;

    passport_v2_companion_t companion[2];
    uint8_t active_companion;
    uint8_t staging_companion;
    size_t staging_expected;
    size_t staging_received;
    char staging_digest[PASSPORT_V2_ASSET_HEX];
    bool staging_active;
    passport_v2_hash_verify_fn verify_hash;

    passport_v2_action_t action;
} passport_v2_state_t;

void passport_v2_init(passport_v2_state_t *state,
                      passport_v2_hash_verify_fn verify_hash);
bool passport_v2_apply_line(passport_v2_state_t *state, const char *line);
bool passport_v2_route_matches_line(const passport_v2_state_t *state,
                                    const char *line);
bool passport_v2_open_sessions(passport_v2_state_t *state);
bool passport_v2_button(passport_v2_state_t *state, int button);
bool passport_v2_take_action(passport_v2_state_t *state,
                             passport_v2_action_t *action);
const passport_v2_companion_t *passport_v2_companion(
    const passport_v2_state_t *state);
int passport_v2_battery_segments(int soc);
