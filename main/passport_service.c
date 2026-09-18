#include "passport_service.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_bounded(char *destination, size_t capacity, const char *source) {
    if (capacity == 0) return;
    strncpy(destination, source, capacity - 1);
    destination[capacity - 1] = '\0';
}

static bool has_type(const char *line, const char *type) {
    char needle[PASSPORT_SERVICE_TEXT_MAX];
    int written = snprintf(needle, sizeof(needle), "\"type\":\"%s\"", type);
    return written > 0 && (size_t)written < sizeof(needle) && strstr(line, needle) != NULL;
}

static bool get_string(const char *line, const char *key, char *value, size_t capacity) {
    char needle[PASSPORT_SERVICE_TEXT_MAX];
    int written = snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    if (written <= 0 || (size_t)written >= sizeof(needle)) return false;
    const char *start = strstr(line, needle);
    if (!start) return false;
    start += written;
    const char *end = start;
    while (*end != '\0' && *end != '"') {
        if (*end == '\\') return false;
        end++;
    }
    if (*end != '"' || end == start || (size_t)(end - start) >= capacity) return false;
    memcpy(value, start, (size_t)(end - start));
    value[end - start] = '\0';
    return true;
}

static bool get_uint(const char *line, const char *key, unsigned *value) {
    char needle[PASSPORT_SERVICE_TEXT_MAX];
    int written = snprintf(needle, sizeof(needle), "\"%s\":", key);
    if (written <= 0 || (size_t)written >= sizeof(needle)) return false;
    const char *start = strstr(line, needle);
    if (!start) return false;
    start += written;
    if (!isdigit((unsigned char)*start)) return false;
    char *end = NULL;
    unsigned long parsed = strtoul(start, &end, 10);
    if (end == start) return false;
    *value = (unsigned)parsed;
    return true;
}

static const char *find_array_start(const char *line, const char *key) {
    char needle[PASSPORT_SERVICE_TEXT_MAX];
    int written = snprintf(needle, sizeof(needle), "\"%s\":[", key);
    if (written <= 0 || (size_t)written >= sizeof(needle)) return NULL;
    const char *hit = strstr(line, needle);
    if (!hit) return NULL;
    return hit + written - 1;  /* return pointer to '[' */
}

static const char *next_object(const char *cursor, const char *end) {
    while (cursor < end && *cursor != '{') cursor++;
    return cursor < end ? cursor : NULL;
}

static const char *find_object_end(const char *cursor, const char *end) {
    int depth = 0;
    for (; cursor < end; cursor++) {
        if (*cursor == '{') depth++;
        else if (*cursor == '}') {
            depth--;
            if (depth == 0) return cursor;
        }
    }
    return NULL;
}

static void reset_page_for_stack(passport_service_snapshot_t *state) {
    if (state->mode_locked) return;
    if (state->stack_count > 0 && state->page != PASSPORT_PAGE_COMPOSE_STACK) {
        state->page = PASSPORT_PAGE_COMPOSE_STACK;
    } else if (state->stack_count == 0 &&
               state->page == PASSPORT_PAGE_COMPOSE_STACK) {
        state->page = PASSPORT_PAGE_WEAR_HOME;
    }
}

static bool parse_task_state(const char *line, passport_service_snapshot_t *state) {
    char task_id[PASSPORT_SERVICE_ID_MAX];
    char task_state[PASSPORT_SERVICE_ID_MAX];
    char summary[PASSPORT_SERVICE_TEXT_MAX];
    unsigned progress;
    if (!get_string(line, "task_id", task_id, sizeof(task_id)) ||
        !get_string(line, "state", task_state, sizeof(task_state)) ||
        !get_string(line, "summary", summary, sizeof(summary)) ||
        !get_uint(line, "progress", &progress) || progress > 100U) return false;

    passport_task_state_t parsed_state;
    if (strcmp(task_state, "idle") == 0) parsed_state = PASSPORT_TASK_IDLE;
    else if (strcmp(task_state, "running") == 0) parsed_state = PASSPORT_TASK_RUNNING;
    else if (strcmp(task_state, "waiting_approval") == 0) {
        parsed_state = PASSPORT_TASK_WAITING_APPROVAL;
    } else if (strcmp(task_state, "done") == 0) parsed_state = PASSPORT_TASK_DONE;
    else if (strcmp(task_state, "error") == 0) parsed_state = PASSPORT_TASK_ERROR;
    else return false;

    state->task_state = parsed_state;
    state->progress = progress;
    copy_bounded(state->task_id, sizeof(state->task_id), task_id);
    copy_bounded(state->summary, sizeof(state->summary), summary);
    return true;
}

static bool parse_task_event(const char *line, passport_service_snapshot_t *state) {
    passport_task_event_t event = {0};
    char task_id[PASSPORT_SERVICE_ID_MAX];
    if (!get_string(line, "task_id", task_id, sizeof(task_id))) return false;
    if (!get_string(line, "event_id", event.event_id, sizeof(event.event_id))) return false;
    if (!get_string(line, "summary", event.summary, sizeof(event.summary))) return false;
    if (!get_string(line, "ts", event.ts, sizeof(event.ts))) event.ts[0] = '\0';

    /* Push at the top; keep newest-first ordering. */
    size_t keep = state->event_count;
    if (keep >= PASSPORT_SERVICE_EVENT_MAX) keep = PASSPORT_SERVICE_EVENT_MAX - 1U;
    for (size_t i = keep; i > 0; i--) {
        state->events[i] = state->events[i - 1];
    }
    state->events[0] = event;
    if (state->event_count < PASSPORT_SERVICE_EVENT_MAX) state->event_count++;
    return true;
}

static bool parse_approval(const char *line, passport_service_snapshot_t *state) {
    char request_id[PASSPORT_SERVICE_ID_MAX];
    char summary[PASSPORT_SERVICE_TEXT_MAX];
    if (!get_string(line, "request_id", request_id, sizeof(request_id)) ||
        !get_string(line, "summary", summary, sizeof(summary))) return false;
    copy_bounded(state->request_id, sizeof(state->request_id), request_id);
    copy_bounded(state->approval_summary, sizeof(state->approval_summary), summary);
    state->approval_pending = true;
    state->approval_elapsed_ms = 0;
    state->task_state = PASSPORT_TASK_WAITING_APPROVAL;
    return true;
}

static bool parse_goal_mode_state(const char *line, passport_service_snapshot_t *state) {
    char mode[PASSPORT_SERVICE_ID_MAX];
    char mode_state[PASSPORT_SERVICE_ID_MAX];
    char card_id[PASSPORT_SERVICE_ID_MAX];
    char ide[PASSPORT_SERVICE_ID_MAX];
    char session_id[PASSPORT_SERVICE_ID_MAX];
    if (!get_string(line, "mode", mode, sizeof(mode)) || strcmp(mode, "goal") != 0 ||
        !get_string(line, "state", mode_state, sizeof(mode_state)) ||
        !get_string(line, "card_id", card_id, sizeof(card_id))) return false;
    /* One-card admission: if no card has been registered yet (no physical
     * NFC reader and no prior demo_passport_service_nfc_card() call), the
     * first goal.mode.state we see on the wire adopts its card_id as the
     * active card. Once bound, any goal.mode.state carrying a different
     * card_id is rejected. This keeps the "second card rejected" rule from
     * passport-service-architecture.md while unblocking the NFC-relay flow
     * where the device itself never sees the raw card tap. */
    if (state->goal_card_id[0] == '\0') {
        copy_bounded(state->goal_card_id, sizeof(state->goal_card_id), card_id);
    } else if (strcmp(card_id, state->goal_card_id) != 0) {
        return false;
    }

    if (strcmp(mode_state, "enabled") == 0) {
        if (!get_string(line, "ide", ide, sizeof(ide))) return false;
        if (!get_string(line, "session_id", session_id, sizeof(session_id))) return false;
        state->goal_mode_state = PASSPORT_GOAL_ENABLED;
        copy_bounded(state->goal_ide, sizeof(state->goal_ide), ide);
        copy_bounded(state->goal_session_id, sizeof(state->goal_session_id), session_id);
        return true;
    }
    if (strcmp(mode_state, "disabled") == 0) {
        state->goal_mode_state = PASSPORT_GOAL_DISABLED;
        state->goal_ide[0] = '\0';
        state->goal_session_id[0] = '\0';
        return true;
    }
    return false;
}

static bool parse_skill(const char *line, passport_service_snapshot_t *state) {
    char skill_id[PASSPORT_SERVICE_ID_MAX];
    char revision[PASSPORT_SERVICE_ID_MAX];
    char label[PASSPORT_SERVICE_TEXT_MAX];
    if (!get_string(line, "skill_id", skill_id, sizeof(skill_id)) ||
        !get_string(line, "revision", revision, sizeof(revision)) ||
        !get_string(line, "label", label, sizeof(label))) return false;
    copy_bounded(state->skill_id, sizeof(state->skill_id), skill_id);
    copy_bounded(state->skill_revision, sizeof(state->skill_revision), revision);
    if (state->active_skill_revision[0] == '\0') {
        copy_bounded(state->active_skill_revision, sizeof(state->active_skill_revision), revision);
    }
    copy_bounded(state->skill_label, sizeof(state->skill_label), label);
    return true;
}

static bool parse_skill_updated(const char *line, passport_service_snapshot_t *state) {
    char skill_id[PASSPORT_SERVICE_ID_MAX];
    char revision[PASSPORT_SERVICE_ID_MAX];
    if (!get_string(line, "skill_id", skill_id, sizeof(skill_id)) ||
        !get_string(line, "revision", revision, sizeof(revision))) return false;
    copy_bounded(state->skill_id, sizeof(state->skill_id), skill_id);
    copy_bounded(state->skill_revision, sizeof(state->skill_revision), revision);
    if (state->active_skill_revision[0] == '\0') {
        copy_bounded(state->active_skill_revision, sizeof(state->active_skill_revision), revision);
    }
    return true;
}

static bool parse_tile_stack(const char *line, passport_service_snapshot_t *state) {
    /* Keep this parser's stack footprint small: the surrounding
     * passport_service_apply_line() has already copied a full snapshot onto
     * the stack, and this function runs on the LVGL timer task (default 4 KB
     * on esp_lvgl_port). Moving the scratch buffer and the temporary stack
     * array to file-scope is safe because the parser is not reentrant. */
    static char scratch[PASSPORT_SERVICE_LINE_MAX];
    static passport_tile_t stack[PASSPORT_SERVICE_STACK_MAX];

    char context_id[PASSPORT_SERVICE_ID_MAX];
    if (!get_string(line, "context_id", context_id, sizeof(context_id))) return false;
    const char *array_start = find_array_start(line, "stack");
    if (!array_start || *array_start != '[') return false;
    const char *cursor = array_start + 1;
    const char *array_end = strchr(cursor, ']');
    if (!array_end) return false;

    size_t count = 0;
    memset(stack, 0, sizeof(stack));

    while (count < PASSPORT_SERVICE_STACK_MAX) {
        const char *object_start = next_object(cursor, array_end);
        if (!object_start) break;
        const char *object_end = find_object_end(object_start, array_end);
        if (!object_end) return false;
        size_t length = (size_t)(object_end - object_start + 1);
        if (length >= sizeof(scratch)) return false;
        memcpy(scratch, object_start, length);
        scratch[length] = '\0';
        passport_tile_t tile = {0};
        if (!get_string(scratch, "tile_id", tile.tile_id, sizeof(tile.tile_id))) {
            return false;
        }
        (void)get_string(scratch, "role", tile.role, sizeof(tile.role));
        (void)get_string(scratch, "skill_id", tile.skill_id, sizeof(tile.skill_id));
        (void)get_string(scratch, "revision", tile.revision, sizeof(tile.revision));
        stack[count++] = tile;
        cursor = object_end + 1;
    }

    if (next_object(cursor, array_end)) return false;
    bool changed = count != state->stack_count ||
                   strcmp(context_id, state->context_id) != 0;
    for (size_t i = 0; i < count && !changed; i++) {
        changed = memcmp(&stack[i], &state->stack[i], sizeof(stack[i])) != 0;
    }
    if (changed) {
        state->stack_generation++;
        state->compose_confirmed = false;
        state->compose_duration_ms = 0;
    }
    copy_bounded(state->context_id, sizeof(state->context_id), context_id);
    for (size_t i = 0; i < count; i++) state->stack[i] = stack[i];
    for (size_t i = count; i < PASSPORT_SERVICE_STACK_MAX; i++) {
        memset(&state->stack[i], 0, sizeof(state->stack[i]));
    }
    state->stack_count = count;
    if (state->stack_selected >= count) state->stack_selected = 0;
    if (changed) {
        state->compose_status = count == 0 ? PASSPORT_COMPOSE_EMPTY : PASSPORT_COMPOSE_OK;
    }
    if (count == 0) {
        state->nfc_card_id[0] = '\0';
        state->compose_duration_ms = 0;
        state->context_id[0] = '\0';
    }
    reset_page_for_stack(state);
    return true;
}

static bool parse_context_composed(const char *line, passport_service_snapshot_t *state) {
    char context_id[PASSPORT_SERVICE_ID_MAX];
    char status[PASSPORT_SERVICE_ID_MAX];
    unsigned duration = 0;
    if (!get_string(line, "context_id", context_id, sizeof(context_id))) return false;
    if (!get_string(line, "status", status, sizeof(status))) return false;
    (void)get_uint(line, "duration_ms", &duration);
    if (state->context_id[0] != '\0' && strcmp(context_id, state->context_id) != 0) {
        return false;
    }
    if (strcmp(status, "ok") == 0) state->compose_status = PASSPORT_COMPOSE_OK;
    else if (strcmp(status, "conflict") == 0) state->compose_status = PASSPORT_COMPOSE_CONFLICT;
    else return false;
    state->compose_confirmed = true;
    state->compose_duration_ms = duration;
    return true;
}

void passport_service_init(passport_service_t *service) {
    memset(service, 0, sizeof(*service));
    service->state.task_state = PASSPORT_TASK_IDLE;
    service->state.page = PASSPORT_PAGE_WEAR_HOME;
    service->state.compose_status = PASSPORT_COMPOSE_EMPTY;
    service->state.link_idle_ms = -1;
    service->state.approval_elapsed_ms = -1;
}

passport_service_result_t passport_service_apply_line(passport_service_t *service,
                                                       const char *line) {
    if (!service || !line || strlen(line) >= PASSPORT_SERVICE_LINE_MAX) {
        return PASSPORT_SERVICE_REJECTED;
    }

    /* The snapshot is ~2 KB; keep it off the LVGL timer task stack. Parser is
     * single-threaded (LVGL timer callback), so a file-scope buffer is safe. */
    static passport_service_snapshot_t next;
    next = service->state;
    bool parsed = false;
    if (has_type(line, "nfc.present")) {
        char card_id[PASSPORT_SERVICE_ID_MAX];
        parsed = get_string(line, "card_id", card_id, sizeof(card_id));
        for (size_t i = 0; parsed && card_id[i]; i++) {
            unsigned char ch = (unsigned char)card_id[i];
            parsed = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                     (ch >= '0' && ch <= '9') || strchr("-_:.", ch) != NULL;
        }
        if (parsed) copy_bounded(next.nfc_card_id, sizeof(next.nfc_card_id), card_id);
    }
    else if (has_type(line, "task.state")) parsed = parse_task_state(line, &next);
    else if (has_type(line, "task.event")) parsed = parse_task_event(line, &next);
    else if (has_type(line, "approval.request")) parsed = parse_approval(line, &next);
    else if (has_type(line, "goal.mode.state")) parsed = parse_goal_mode_state(line, &next);
    else if (has_type(line, "skill.revision")) parsed = parse_skill(line, &next);
    else if (has_type(line, "skill.updated")) parsed = parse_skill_updated(line, &next);
    else if (has_type(line, "tile.stack.state")) parsed = parse_tile_stack(line, &next);
    else if (has_type(line, "context.composed")) parsed = parse_context_composed(line, &next);
    if (!parsed) return PASSPORT_SERVICE_REJECTED;
    /* Every accepted host frame resets the disconnect banner clock. */
    next.link_idle_ms = 0;
    service->state = next;
    return PASSPORT_SERVICE_ACCEPTED;
}

void passport_service_snapshot(const passport_service_t *service,
                               passport_service_snapshot_t *snapshot) {
    if (!service || !snapshot) return;
    *snapshot = service->state;
}

passport_service_result_t passport_service_button(passport_service_t *service,
                                                   passport_button_t button) {
    if (!service || !service->state.approval_pending) return PASSPORT_SERVICE_REJECTED;
    if (button != PASSPORT_BUTTON_OK && button != PASSPORT_BUTTON_UP) {
        return PASSPORT_SERVICE_REJECTED;
    }
    memset(&service->action, 0, sizeof(service->action));
    service->action.type = PASSPORT_ACTION_APPROVAL;
    service->action.decision = button == PASSPORT_BUTTON_OK
                              ? PASSPORT_APPROVAL_APPROVE
                              : PASSPORT_APPROVAL_REJECT;
    copy_bounded(service->action.request_id, sizeof(service->action.request_id),
                 service->state.request_id);
    service->state.approval_pending = false;
    service->state.approval_elapsed_ms = -1;
    return PASSPORT_SERVICE_ACCEPTED;
}

passport_service_result_t passport_service_reload_skill(passport_service_t *service) {
    if (!service || service->state.skill_id[0] == '\0' ||
        service->state.skill_revision[0] == '\0') return PASSPORT_SERVICE_REJECTED;
    memset(&service->action, 0, sizeof(service->action));
    service->action.type = PASSPORT_ACTION_RELOAD_SKILL;
    copy_bounded(service->action.skill_id, sizeof(service->action.skill_id),
                 service->state.skill_id);
    copy_bounded(service->action.skill_revision, sizeof(service->action.skill_revision),
                 service->state.skill_revision);
    copy_bounded(service->state.active_skill_revision,
                 sizeof(service->state.active_skill_revision), service->state.skill_revision);
    return PASSPORT_SERVICE_ACCEPTED;
}

passport_service_result_t passport_service_load_goal_card(passport_service_t *service,
                                                            const char *card_id) {
    if (!service || !card_id || card_id[0] == '\0' ||
        strlen(card_id) >= PASSPORT_SERVICE_ID_MAX) {
        return PASSPORT_SERVICE_REJECTED;
    }
    if (service->state.goal_card_id[0] != '\0' &&
        strcmp(service->state.goal_card_id, card_id) != 0) {
        return PASSPORT_SERVICE_REJECTED;
    }
    if (service->state.goal_mode_state != PASSPORT_GOAL_DISABLED) {
        return PASSPORT_SERVICE_REJECTED;
    }
    copy_bounded(service->state.goal_card_id, sizeof(service->state.goal_card_id), card_id);
    copy_bounded(service->state.nfc_card_id, sizeof(service->state.nfc_card_id), card_id);
    service->state.goal_mode_state = PASSPORT_GOAL_REQUESTED;
    memset(&service->action, 0, sizeof(service->action));
    service->action.type = PASSPORT_ACTION_GOAL_MODE_REQUEST;
    copy_bounded(service->action.card_id, sizeof(service->action.card_id), card_id);
    return PASSPORT_SERVICE_ACCEPTED;
}

passport_service_action_result_t passport_service_take_action(
    passport_service_t *service, passport_service_action_t *action) {
    if (!service || !action || service->action.type == PASSPORT_ACTION_NONE) {
        return PASSPORT_SERVICE_ACTION_EMPTY;
    }
    *action = service->action;
    memset(&service->action, 0, sizeof(service->action));
    return PASSPORT_SERVICE_ACTION_READY;
}

passport_service_result_t passport_service_navigate(passport_service_t *service,
                                                     passport_button_t button) {
    if (!service) return PASSPORT_SERVICE_REJECTED;
    if (service->state.approval_pending) return PASSPORT_SERVICE_REJECTED;

    passport_service_snapshot_t *state = &service->state;
    switch (state->page) {
    case PASSPORT_PAGE_WEAR_HOME:
        if (button == PASSPORT_BUTTON_UP) {
            state->page = PASSPORT_PAGE_WEAR_TASK;
            return PASSPORT_SERVICE_ACCEPTED;
        }
        if (button == PASSPORT_BUTTON_DOWN && state->stack_count > 0) {
            state->page = PASSPORT_PAGE_COMPOSE_STACK;
            return PASSPORT_SERVICE_ACCEPTED;
        }
        return PASSPORT_SERVICE_REJECTED;
    case PASSPORT_PAGE_WEAR_TASK:
        if (button == PASSPORT_BUTTON_UP) {
            state->page = PASSPORT_PAGE_WEAR_HOME;
            return PASSPORT_SERVICE_ACCEPTED;
        }
        return PASSPORT_SERVICE_REJECTED;
    case PASSPORT_PAGE_COMPOSE_STACK:
        if (button == PASSPORT_BUTTON_UP) {
            state->page = PASSPORT_PAGE_WEAR_HOME;
            return PASSPORT_SERVICE_ACCEPTED;
        }
        if (button == PASSPORT_BUTTON_DOWN && state->stack_count > 0) {
            state->stack_selected = (state->stack_selected + 1U) % state->stack_count;
            return PASSPORT_SERVICE_ACCEPTED;
        }
        return PASSPORT_SERVICE_REJECTED;
    }
    return PASSPORT_SERVICE_REJECTED;
}

passport_service_result_t passport_service_ack_top_event(passport_service_t *service) {
    if (!service || service->state.event_count == 0) return PASSPORT_SERVICE_REJECTED;
    passport_task_event_t top = service->state.events[0];
    memset(&service->action, 0, sizeof(service->action));
    service->action.type = PASSPORT_ACTION_TASK_EVENT_ACK;
    copy_bounded(service->action.event_id, sizeof(service->action.event_id), top.event_id);
    for (size_t i = 0; i + 1U < service->state.event_count; i++) {
        service->state.events[i] = service->state.events[i + 1U];
    }
    service->state.event_count--;
    memset(&service->state.events[service->state.event_count], 0,
           sizeof(service->state.events[0]));
    return PASSPORT_SERVICE_ACCEPTED;
}

void passport_service_tick(passport_service_t *service, uint32_t elapsed_ms) {
    if (!service || elapsed_ms == 0) return;
    passport_service_snapshot_t *state = &service->state;

    if (state->link_idle_ms < 0) {
        state->link_idle_ms = (int32_t)elapsed_ms;
    } else if ((uint32_t)state->link_idle_ms + elapsed_ms > INT32_MAX) {
        state->link_idle_ms = INT32_MAX;
    } else {
        state->link_idle_ms += (int32_t)elapsed_ms;
    }

    if (state->approval_pending) {
        if (state->approval_elapsed_ms < 0) state->approval_elapsed_ms = 0;
        if ((uint32_t)state->approval_elapsed_ms + elapsed_ms > INT32_MAX) {
            state->approval_elapsed_ms = INT32_MAX;
        } else {
            state->approval_elapsed_ms += (int32_t)elapsed_ms;
        }
        if ((uint32_t)state->approval_elapsed_ms >=
            PASSPORT_SERVICE_APPROVAL_TIMEOUT_MS) {
            state->approval_pending = false;
            state->approval_elapsed_ms = -1;
            state->approval_summary[0] = '\0';
            state->request_id[0] = '\0';
            if (state->task_state == PASSPORT_TASK_WAITING_APPROVAL) {
                state->task_state = PASSPORT_TASK_IDLE;
            }
        }
    }
}
