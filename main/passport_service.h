#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_SERVICE_LINE_MAX 512U
#define PASSPORT_SERVICE_TEXT_MAX 96U
#define PASSPORT_SERVICE_ID_MAX 48U
#define PASSPORT_SERVICE_STACK_MAX 4U
#define PASSPORT_SERVICE_EVENT_MAX 5U
#define PASSPORT_SERVICE_APPROVAL_TIMEOUT_MS 60000U
/* Link idle threshold: once no valid @passport frame has been observed for
 * this many milliseconds, the header renders 断线. Product decision from
 * Slice F: no full-body banner, header status only. Keep this collocated with
 * PASSPORT_SERVICE_APPROVAL_TIMEOUT_MS so both operational thresholds have
 * one source of truth. */
#define PASSPORT_SERVICE_LINK_IDLE_DISCONNECT_MS 30000

typedef enum {
    PASSPORT_TASK_IDLE = 0,
    PASSPORT_TASK_RUNNING,
    PASSPORT_TASK_WAITING_APPROVAL,
    PASSPORT_TASK_DONE,
    PASSPORT_TASK_ERROR,
} passport_task_state_t;

typedef enum {
    PASSPORT_GOAL_DISABLED = 0,
    PASSPORT_GOAL_REQUESTED,
    PASSPORT_GOAL_ENABLED,
} passport_goal_mode_state_t;

typedef enum {
    PASSPORT_BUTTON_UP = 0,
    PASSPORT_BUTTON_DOWN,
    PASSPORT_BUTTON_OK,
} passport_button_t;

typedef enum {
    PASSPORT_APPROVAL_APPROVE = 0,
    PASSPORT_APPROVAL_REJECT,
} passport_approval_decision_t;

typedef enum {
    PASSPORT_ACTION_NONE = 0,
    PASSPORT_ACTION_APPROVAL,
    PASSPORT_ACTION_GOAL_MODE_REQUEST,
    PASSPORT_ACTION_RELOAD_SKILL,
    PASSPORT_ACTION_TASK_EVENT_ACK,
} passport_action_type_t;

typedef enum {
    PASSPORT_PAGE_WEAR_HOME = 0,
    PASSPORT_PAGE_WEAR_TASK,
    PASSPORT_PAGE_COMPOSE_STACK,
} passport_page_t;

typedef enum {
    PASSPORT_COMPOSE_EMPTY = 0,
    PASSPORT_COMPOSE_OK,
    PASSPORT_COMPOSE_CONFLICT,
} passport_compose_status_t;

typedef enum {
    PASSPORT_SERVICE_REJECTED = 0,
    PASSPORT_SERVICE_ACCEPTED,
} passport_service_result_t;

typedef enum {
    PASSPORT_SERVICE_ACTION_EMPTY = 0,
    PASSPORT_SERVICE_ACTION_READY,
} passport_service_action_result_t;

typedef struct {
    char tile_id[PASSPORT_SERVICE_ID_MAX];
    char role[PASSPORT_SERVICE_ID_MAX];
    char skill_id[PASSPORT_SERVICE_ID_MAX];
    char revision[PASSPORT_SERVICE_ID_MAX];
} passport_tile_t;

typedef struct {
    char event_id[PASSPORT_SERVICE_ID_MAX];
    char ts[16];
    char summary[PASSPORT_SERVICE_TEXT_MAX];
} passport_task_event_t;

typedef struct {
    passport_task_state_t task_state;
    unsigned progress;
    char task_id[PASSPORT_SERVICE_ID_MAX];
    char summary[PASSPORT_SERVICE_TEXT_MAX];
    bool approval_pending;
    char request_id[PASSPORT_SERVICE_ID_MAX];
    char approval_summary[PASSPORT_SERVICE_TEXT_MAX];
    passport_goal_mode_state_t goal_mode_state;
    char goal_card_id[PASSPORT_SERVICE_ID_MAX];
    /* Display-only observation, independent of one-card Goal admission. */
    char nfc_card_id[PASSPORT_SERVICE_ID_MAX];
    uint32_t stack_generation;
    bool compose_confirmed;
    char goal_ide[PASSPORT_SERVICE_ID_MAX];
    char goal_session_id[PASSPORT_SERVICE_ID_MAX];
    char skill_id[PASSPORT_SERVICE_ID_MAX];
    char skill_revision[PASSPORT_SERVICE_ID_MAX];
    char active_skill_revision[PASSPORT_SERVICE_ID_MAX];
    char skill_label[PASSPORT_SERVICE_TEXT_MAX];
    passport_tile_t stack[PASSPORT_SERVICE_STACK_MAX];
    size_t stack_count;
    size_t stack_selected;
    char context_id[PASSPORT_SERVICE_ID_MAX];
    passport_compose_status_t compose_status;
    unsigned compose_duration_ms;
    passport_task_event_t events[PASSPORT_SERVICE_EVENT_MAX];
    size_t event_count;
    passport_page_t page;
    bool mode_locked;
    /* Milliseconds elapsed since the last accepted @passport frame; -1 means
     * no frame has been observed since boot. Populated by the demo layer via
     * passport_service_tick(). */
    int32_t link_idle_ms;
    /* Milliseconds elapsed since the current approval.request. Reset whenever
     * a new approval.request is accepted or the user resolves it. Populated
     * by passport_service_tick() while approval_pending is true. */
    int32_t approval_elapsed_ms;
} passport_service_snapshot_t;

typedef struct {
    passport_action_type_t type;
    passport_approval_decision_t decision;
    char request_id[PASSPORT_SERVICE_ID_MAX];
    char card_id[PASSPORT_SERVICE_ID_MAX];
    char skill_id[PASSPORT_SERVICE_ID_MAX];
    char skill_revision[PASSPORT_SERVICE_ID_MAX];
    char event_id[PASSPORT_SERVICE_ID_MAX];
} passport_service_action_t;

typedef struct {
    passport_service_snapshot_t state;
    passport_service_action_t action;
} passport_service_t;

void passport_service_init(passport_service_t *service);
passport_service_result_t passport_service_apply_line(passport_service_t *service,
                                                       const char *line);
void passport_service_snapshot(const passport_service_t *service,
                               passport_service_snapshot_t *snapshot);
passport_service_result_t passport_service_button(passport_service_t *service,
                                                   passport_button_t button);
passport_service_result_t passport_service_reload_skill(passport_service_t *service);
passport_service_result_t passport_service_load_goal_card(passport_service_t *service,
                                                            const char *card_id);
passport_service_action_result_t passport_service_take_action(
    passport_service_t *service, passport_service_action_t *action);

/* Page navigation triggered by physical button clicks. Approval-pending state
 * still takes precedence in passport_service_button(). */
passport_service_result_t passport_service_navigate(passport_service_t *service,
                                                     passport_button_t button);

/* Acknowledge the most-recent task event, if any. */
passport_service_result_t passport_service_ack_top_event(passport_service_t *service);

/* Advance internal timers by `elapsed_ms`. Callers (the demo layer) invoke it
 * from the LVGL timer callback with the tick delta. When the approval timer
 * exceeds PASSPORT_SERVICE_APPROVAL_TIMEOUT_MS the pending approval is
 * cleared. link_idle_ms is bumped monotonically until the next accepted
 * @passport frame resets it. */
void passport_service_tick(passport_service_t *service, uint32_t elapsed_ms);
