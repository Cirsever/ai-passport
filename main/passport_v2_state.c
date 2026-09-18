#include "passport_v2_state.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_text(char *destination, size_t capacity, const char *source) {
    if (!capacity) return;
    strncpy(destination, source, capacity - 1U);
    destination[capacity - 1U] = '\0';
}

static bool has_type(const char *line, const char *type) {
    char needle[64];
    int length = snprintf(needle, sizeof(needle), "\"type\":\"%s\"", type);
    return length > 0 && (size_t)length < sizeof(needle) &&
           strstr(line, needle) != NULL;
}

static const char *value_start(const char *line, const char *key) {
    char needle[48];
    int length = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (length <= 0 || (size_t)length >= sizeof(needle)) return NULL;
    const char *value = strstr(line, needle);
    return value ? value + length : NULL;
}

static bool get_string(const char *line, const char *key,
                       char *value, size_t capacity) {
    const char *cursor = value_start(line, key);
    if (!cursor || *cursor++ != '"' || !capacity) return false;
    size_t used = 0;
    while (*cursor && *cursor != '"') {
        unsigned char ch = (unsigned char)*cursor++;
        if (ch == '\\') {
            ch = (unsigned char)*cursor++;
            if (ch == '"' || ch == '\\' || ch == '/') {
                /* literal */
            } else if (ch == 'n') {
                ch = '\n';
            } else if (ch == 'r') {
                ch = '\r';
            } else if (ch == 't') {
                ch = '\t';
            } else {
                return false;
            }
        }
        if (used + 1U >= capacity) return false;
        value[used++] = (char)ch;
    }
    if (*cursor != '"') return false;
    value[used] = '\0';
    return true;
}

static bool get_uint(const char *line, const char *key, uint32_t *value) {
    const char *start = value_start(line, key);
    if (!start || !isdigit((unsigned char)*start)) return false;
    char *end = NULL;
    unsigned long parsed = strtoul(start, &end, 10);
    if (end == start || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool get_bool(const char *line, const char *key, bool *value) {
    const char *start = value_start(line, key);
    if (!start) return false;
    if (strncmp(start, "true", 4) == 0) {
        *value = true;
        return true;
    }
    if (strncmp(start, "false", 5) == 0) {
        *value = false;
        return true;
    }
    return false;
}

static void clear_staging(passport_v2_state_t *state) {
    state->staging_active = false;
    state->staging_expected = 0;
    state->staging_received = 0;
    state->staging_digest[0] = '\0';
}

static void clear_route(passport_v2_state_t *state) {
    state->sid[0] = '\0';
    state->title[0] = '\0';
    state->session_state[0] = '\0';
    state->epoch = 0;
    state->writable = false;
    state->approval_status = PASSPORT_V2_APPROVAL_NONE;
    state->view = PASSPORT_V2_VIEW_HOME;
    clear_staging(state);
}

static bool route_matches(const passport_v2_state_t *state, const char *line) {
    char bridge[PASSPORT_V2_BRIDGE_MAX];
    char sid[PASSPORT_V2_SID_MAX];
    uint32_t epoch = 0;
    return get_string(line, "bridge", bridge, sizeof(bridge)) &&
           get_string(line, "sid", sid, sizeof(sid)) &&
           get_uint(line, "epoch", &epoch) &&
           strcmp(bridge, state->bridge) == 0 &&
           strcmp(sid, state->sid) == 0 && epoch == state->epoch;
}

bool passport_v2_route_matches_line(const passport_v2_state_t *state,
                                    const char *line) {
    return state && line && state->negotiated && route_matches(state, line);
}

static void set_ack(passport_v2_state_t *state, const char *asset,
                    uint32_t offset, const char *status) {
    memset(&state->action, 0, sizeof(state->action));
    state->action.type = PASSPORT_V2_ACTION_COMPANION_ACK;
    state->action.offset = offset;
    copy_text(state->action.asset, sizeof(state->action.asset), asset);
    copy_text(state->action.status, sizeof(state->action.status), status);
}

static int base64_value(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

static bool decode_base64(const char *input, uint8_t *output,
                          size_t capacity, size_t *written) {
    size_t length = strlen(input);
    if (length == 0 || length % 4U != 0) return false;
    size_t used = 0;
    for (size_t i = 0; i < length; i += 4U) {
        int a = base64_value((unsigned char)input[i]);
        int b = base64_value((unsigned char)input[i + 1U]);
        int c = input[i + 2U] == '=' ? 0 :
                base64_value((unsigned char)input[i + 2U]);
        int d = input[i + 3U] == '=' ? 0 :
                base64_value((unsigned char)input[i + 3U]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;
        if (used >= capacity) return false;
        output[used++] = (uint8_t)((a << 2) | (b >> 4));
        if (input[i + 2U] != '=') {
            if (used >= capacity) return false;
            output[used++] = (uint8_t)((b << 4) | (c >> 2));
        }
        if (input[i + 3U] != '=') {
            if (used >= capacity) return false;
            output[used++] = (uint8_t)((c << 6) | d);
        }
    }
    *written = used;
    return true;
}

void passport_v2_init(passport_v2_state_t *state,
                      passport_v2_hash_verify_fn verify_hash) {
    if (!state) return;
    memset(state, 0, sizeof(*state));
    state->verify_hash = verify_hash;
}

static bool apply_host_hello(passport_v2_state_t *state, const char *line) {
    char bridge[PASSPORT_V2_BRIDGE_MAX];
    uint32_t protocol = 0;
    bool sessions = false;
    bool companion = false;
    if (!get_uint(line, "protocol", &protocol) || protocol != 2 ||
        !get_string(line, "bridge", bridge, sizeof(bridge)) ||
        strlen(bridge) != 16U ||
        !get_bool(line, "sessions", &sessions) ||
        !get_bool(line, "companion", &companion)) {
        return false;
    }
    if (strcmp(bridge, state->bridge) != 0) {
        clear_route(state);
        state->session_count = 0;
        copy_text(state->bridge, sizeof(state->bridge), bridge);
    }
    state->negotiated = true;
    state->sessions_capable = sessions;
    state->companion_capable = companion;
    return true;
}

static bool apply_catalog(passport_v2_state_t *state, const char *line) {
    uint32_t tx = 0;
    uint32_t page = 0;
    uint32_t count = 0;
    uint32_t total = 0;
    if (!get_uint(line, "tx", &tx) || tx != state->pending_tx ||
        !get_uint(line, "page", &page) ||
        !get_uint(line, "count", &count) ||
        !get_uint(line, "total", &total) ||
        count > PASSPORT_V2_SESSION_PAGE) {
        return false;
    }
    memset(state->staged_sessions, 0, sizeof(state->staged_sessions));
    state->staged_count = count;
    state->staged_received = 0;
    state->staged_mask = 0;
    state->catalog_page = page;
    state->catalog_total = total;
    if (count == 0) {
        state->session_count = 0;
        state->session_selected = 0;
    }
    return true;
}

static bool apply_entry(passport_v2_state_t *state, const char *line) {
    passport_v2_session_t entry = {0};
    uint32_t tx = 0;
    uint32_t index = 0;
    if (!get_uint(line, "tx", &tx) || tx != state->pending_tx ||
        !get_uint(line, "index", &index) || index >= state->staged_count ||
        !get_string(line, "sid", entry.sid, sizeof(entry.sid)) ||
        !get_string(line, "title", entry.title, sizeof(entry.title)) ||
        !get_string(line, "ide", entry.ide, sizeof(entry.ide)) ||
        !get_string(line, "state", entry.state, sizeof(entry.state)) ||
        !get_bool(line, "writable", &entry.writable)) {
        return false;
    }
    state->staged_sessions[index] = entry;
    uint8_t bit = (uint8_t)(1U << index);
    if (!(state->staged_mask & bit)) {
        state->staged_mask |= bit;
        state->staged_received++;
    }
    if (state->staged_received == state->staged_count) {
        memcpy(state->sessions, state->staged_sessions, sizeof(state->sessions));
        state->session_count = state->staged_count;
        if (state->session_selected >= state->session_count) {
            state->session_selected = 0;
        }
    }
    return true;
}

static bool apply_selected(passport_v2_state_t *state, const char *line) {
    char bridge[PASSPORT_V2_BRIDGE_MAX];
    char sid[PASSPORT_V2_SID_MAX];
    char title[PASSPORT_V2_TITLE_MAX];
    char ide[12];
    char status[12];
    uint32_t tx = 0;
    uint32_t epoch = 0;
    bool writable = false;
    if (!get_uint(line, "tx", &tx) || tx < state->pending_tx ||
        !get_string(line, "bridge", bridge, sizeof(bridge)) ||
        strcmp(bridge, state->bridge) != 0 ||
        !get_uint(line, "epoch", &epoch) ||
        !get_string(line, "sid", sid, sizeof(sid)) ||
        !get_string(line, "ide", ide, sizeof(ide)) ||
        !get_string(line, "title", title, sizeof(title)) ||
        !get_string(line, "state", status, sizeof(status)) ||
        !get_bool(line, "writable", &writable)) {
        return false;
    }
    if (strcmp(sid, state->sid) != 0 || epoch != state->epoch) {
        clear_staging(state);
        state->approval_status = PASSPORT_V2_APPROVAL_NONE;
    }
    state->pending_tx = tx;
    state->epoch = epoch;
    state->writable = writable;
    copy_text(state->sid, sizeof(state->sid), sid);
    copy_text(state->ide, sizeof(state->ide), ide);
    copy_text(state->title, sizeof(state->title), title);
    copy_text(state->session_state, sizeof(state->session_state), status);
    state->view = PASSPORT_V2_VIEW_HOME;
    return true;
}

static bool apply_approval(passport_v2_state_t *state, const char *line) {
    if (!route_matches(state, line)) return false;
    char request[PASSPORT_V2_REQUEST_MAX];
    char operation[12];
    char summary[PASSPORT_V2_SUMMARY_MAX];
    uint32_t pages = 0;
    uint32_t remaining = 0;
    bool allow = false;
    if (!get_string(line, "request_id", request, sizeof(request)) ||
        !get_string(line, "operation", operation, sizeof(operation)) ||
        !get_string(line, "summary", summary, sizeof(summary)) ||
        !get_uint(line, "pages", &pages) || pages == 0 ||
        !get_uint(line, "remaining_ms", &remaining) ||
        !get_bool(line, "allow", &allow)) {
        return false;
    }
    copy_text(state->approval_request_id,
              sizeof(state->approval_request_id), request);
    copy_text(state->approval_operation,
              sizeof(state->approval_operation), operation);
    copy_text(state->approval_summary,
              sizeof(state->approval_summary), summary);
    state->approval_pages = pages;
    state->approval_page = 0;
    state->approval_remaining_ms = remaining;
    state->approval_allow = allow;
    state->approval_status = PASSPORT_V2_APPROVAL_PENDING;
    state->view = PASSPORT_V2_VIEW_APPROVAL;
    return true;
}

static bool apply_receipt(passport_v2_state_t *state, const char *line) {
    char request[PASSPORT_V2_REQUEST_MAX];
    char status[12];
    if (!route_matches(state, line) ||
        !get_string(line, "request_id", request, sizeof(request)) ||
        strcmp(request, state->approval_request_id) != 0 ||
        !get_string(line, "status", status, sizeof(status))) {
        return false;
    }
    if (strcmp(status, "allowed") == 0) {
        state->approval_status = PASSPORT_V2_APPROVAL_ALLOWED;
    } else if (strcmp(status, "denied") == 0) {
        state->approval_status = PASSPORT_V2_APPROVAL_DENIED;
    } else if (strcmp(status, "expired") == 0) {
        state->approval_status = PASSPORT_V2_APPROVAL_EXPIRED;
    } else if (strcmp(status, "unknown") == 0) {
        state->approval_status = PASSPORT_V2_APPROVAL_UNKNOWN;
    } else {
        return false;
    }
    state->view = PASSPORT_V2_VIEW_APPROVAL;
    return true;
}

static bool apply_companion_begin(passport_v2_state_t *state, const char *line) {
    char asset[PASSPORT_V2_ASSET_HEX];
    uint32_t bytes = 0;
    uint32_t frames = 0;
    if (!state->companion_capable || !route_matches(state, line) ||
        !get_string(line, "asset", asset, sizeof(asset)) ||
        strlen(asset) != PASSPORT_V2_ASSET_HEX - 1U ||
        !get_uint(line, "bytes", &bytes) ||
        bytes != PASSPORT_V2_ASSET_BYTES ||
        !get_uint(line, "frames", &frames) || frames != 1U) {
        return false;
    }
    state->staging_companion = (uint8_t)(state->active_companion ^ 1U);
    memset(&state->companion[state->staging_companion], 0,
           sizeof(state->companion[0]));
    state->staging_expected = bytes;
    state->staging_received = 0;
    state->staging_active = true;
    copy_text(state->staging_digest, sizeof(state->staging_digest), asset);
    set_ack(state, asset, 0, "receiving");
    return true;
}

static bool apply_companion_chunk(passport_v2_state_t *state, const char *line) {
    char asset[PASSPORT_V2_ASSET_HEX];
    char data[180];
    uint32_t offset = 0;
    if (!state->staging_active ||
        !get_string(line, "asset", asset, sizeof(asset)) ||
        strcmp(asset, state->staging_digest) != 0 ||
        !get_uint(line, "offset", &offset) ||
        !get_string(line, "data", data, sizeof(data))) {
        return false;
    }
    if (offset != state->staging_received) {
        set_ack(state, asset, (uint32_t)state->staging_received, "retry");
        return true;
    }
    size_t decoded = 0;
    passport_v2_companion_t *target =
        &state->companion[state->staging_companion];
    if (!decode_base64(data, target->bytes + offset,
                       state->staging_expected - offset, &decoded) ||
        decoded > 128U) {
        set_ack(state, asset, (uint32_t)state->staging_received, "retry");
        return true;
    }
    state->staging_received += decoded;
    set_ack(state, asset, (uint32_t)state->staging_received, "receiving");
    return true;
}

static bool apply_companion_commit(passport_v2_state_t *state, const char *line) {
    char asset[PASSPORT_V2_ASSET_HEX];
    if (!state->staging_active ||
        !get_string(line, "asset", asset, sizeof(asset)) ||
        strcmp(asset, state->staging_digest) != 0) {
        return false;
    }
    if (state->staging_received != state->staging_expected ||
        !state->verify_hash ||
        !state->verify_hash(state->companion[state->staging_companion].bytes,
                            state->staging_expected, asset)) {
        set_ack(state, asset, (uint32_t)state->staging_received, "rejected");
        clear_staging(state);
        return true;
    }
    passport_v2_companion_t *target =
        &state->companion[state->staging_companion];
    target->valid = true;
    target->generation =
        state->companion[state->active_companion].generation + 1U;
    copy_text(target->digest, sizeof(target->digest), asset);
    state->active_companion = state->staging_companion;
    set_ack(state, asset, (uint32_t)state->staging_received, "ready");
    clear_staging(state);
    return true;
}

bool passport_v2_apply_line(passport_v2_state_t *state, const char *line) {
    if (!state || !line || strlen(line) >= 512U) return false;
    if (has_type(line, "host.hello")) return apply_host_hello(state, line);
    if (!state->negotiated) return false;
    if (has_type(line, "session.catalog")) return apply_catalog(state, line);
    if (has_type(line, "session.entry")) return apply_entry(state, line);
    if (has_type(line, "session.selected")) return apply_selected(state, line);
    if (has_type(line, "session.error")) {
        state->view = PASSPORT_V2_VIEW_SESSIONS;
        return true;
    }
    if (has_type(line, "approval.request")) return apply_approval(state, line);
    if (has_type(line, "approval.receipt")) return apply_receipt(state, line);
    if (has_type(line, "approval.page")) {
        char request[PASSPORT_V2_REQUEST_MAX];
        uint32_t pages = 0;
        if (!route_matches(state, line) ||
            !get_string(line, "request_id", request, sizeof(request)) ||
            strcmp(request, state->approval_request_id) != 0 ||
            !get_string(line, "text", state->approval_detail,
                        sizeof(state->approval_detail)) ||
            !get_uint(line, "page", &state->approval_page) ||
            !get_uint(line, "pages", &pages) ||
            pages != state->approval_pages ||
            state->approval_page >= pages) {
            return false;
        }
        state->view = PASSPORT_V2_VIEW_APPROVAL_DETAIL;
        return true;
    }
    if (has_type(line, "companion.begin")) {
        return apply_companion_begin(state, line);
    }
    if (has_type(line, "companion.chunk")) {
        return apply_companion_chunk(state, line);
    }
    if (has_type(line, "companion.commit")) {
        return apply_companion_commit(state, line);
    }
    return false;
}

bool passport_v2_open_sessions(passport_v2_state_t *state) {
    if (!state || !state->negotiated || !state->sessions_capable ||
        state->approval_status == PASSPORT_V2_APPROVAL_PENDING ||
        state->approval_status == PASSPORT_V2_APPROVAL_SENDING) {
        return false;
    }
    memset(&state->action, 0, sizeof(state->action));
    if (state->view == PASSPORT_V2_VIEW_SESSIONS ||
        state->view == PASSPORT_V2_VIEW_SWITCHING) {
        state->view = PASSPORT_V2_VIEW_HOME;
        state->action.type = PASSPORT_V2_ACTION_SESSION_QUERY;
    } else {
        state->view = PASSPORT_V2_VIEW_SESSIONS;
        state->pending_tx = ++state->next_tx;
        state->action.type = PASSPORT_V2_ACTION_SESSION_LIST;
        state->action.tx = state->pending_tx;
        state->action.page = 0;
    }
    return true;
}

bool passport_v2_button(passport_v2_state_t *state, int button) {
    if (!state) return false;
    memset(&state->action, 0, sizeof(state->action));
    if (state->view == PASSPORT_V2_VIEW_APPROVAL ||
        state->view == PASSPORT_V2_VIEW_APPROVAL_DETAIL) {
        if (state->approval_status != PASSPORT_V2_APPROVAL_PENDING) {
            if (state->approval_status != PASSPORT_V2_APPROVAL_SENDING) {
                state->approval_status = PASSPORT_V2_APPROVAL_NONE;
                state->view = PASSPORT_V2_VIEW_HOME;
                return true;
            }
            return false;
        }
        if (state->view == PASSPORT_V2_VIEW_APPROVAL_DETAIL && button == 0) {
            state->view = PASSPORT_V2_VIEW_APPROVAL;
            return true;
        }
        if (button == 1) {
            state->view = PASSPORT_V2_VIEW_APPROVAL_DETAIL;
            state->action.type = PASSPORT_V2_ACTION_APPROVAL_DETAIL;
            state->action.page =
                state->approval_page + 1U < state->approval_pages
                    ? state->approval_page + 1U : 0U;
        } else if ((button == 0 &&
                    state->view == PASSPORT_V2_VIEW_APPROVAL) ||
                   (button == 2 && state->approval_allow)) {
            state->approval_status = PASSPORT_V2_APPROVAL_SENDING;
            state->action.type = PASSPORT_V2_ACTION_APPROVAL_DECISION;
            copy_text(state->action.decision, sizeof(state->action.decision),
                      button == 2 ? "approve" : "reject");
        } else {
            return false;
        }
        copy_text(state->action.request_id, sizeof(state->action.request_id),
                  state->approval_request_id);
        return true;
    }
    if (state->view != PASSPORT_V2_VIEW_SESSIONS) return false;
    if (state->session_count == 0) return false;
    if (button == 0) {
        state->session_selected =
            (state->session_selected + state->session_count - 1U) %
            state->session_count;
    } else if (button == 1) {
        state->session_selected =
            (state->session_selected + 1U) % state->session_count;
    } else if (button == 2) {
        state->pending_tx = ++state->next_tx;
        state->view = PASSPORT_V2_VIEW_SWITCHING;
        state->action.type = PASSPORT_V2_ACTION_SESSION_SELECT;
        state->action.tx = state->pending_tx;
        copy_text(state->action.sid, sizeof(state->action.sid),
                  state->sessions[state->session_selected].sid);
    } else {
        return false;
    }
    return true;
}

bool passport_v2_take_action(passport_v2_state_t *state,
                             passport_v2_action_t *action) {
    if (!state || !action || state->action.type == PASSPORT_V2_ACTION_NONE) {
        return false;
    }
    *action = state->action;
    memset(&state->action, 0, sizeof(state->action));
    return true;
}

const passport_v2_companion_t *passport_v2_companion(
    const passport_v2_state_t *state) {
    if (!state) return NULL;
    const passport_v2_companion_t *asset =
        &state->companion[state->active_companion];
    return asset->valid ? asset : NULL;
}

int passport_v2_battery_segments(int soc) {
    if (soc < 0 || soc > 100) return -1;
    if (soc == 0) return 0;
    return (soc + 24) / 25;
}
