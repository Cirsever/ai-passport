#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "passport_service.h"

static void test_task_update_is_applied_transactionally(void) {
    passport_service_t service;
    passport_service_snapshot_t snapshot;
    passport_service_init(&service);

    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"task.state\",\"task_id\":\"runtime-1\","
               "\"state\":\"running\",\"progress\":42,"
               "\"summary\":\"Refactoring tracing\"}") == PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.task_state == PASSPORT_TASK_RUNNING);
    assert(snapshot.progress == 42);
    assert(strcmp(snapshot.task_id, "runtime-1") == 0);

    assert(passport_service_apply_line(&service, "{\"type\":\"task.state\"}") ==
           PASSPORT_SERVICE_REJECTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.task_state == PASSPORT_TASK_RUNNING);
    assert(snapshot.progress == 42);
}

static void test_approval_is_emitted_once(void) {
    passport_service_t service;
    passport_service_action_t action;
    passport_service_init(&service);

    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"approval.request\",\"request_id\":\"r-7\","
               "\"summary\":\"Apply 3 files changed\"}") == PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_button(&service, PASSPORT_BUTTON_OK) == PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_take_action(&service, &action) == PASSPORT_SERVICE_ACTION_READY);
    assert(action.type == PASSPORT_ACTION_APPROVAL);
    assert(action.decision == PASSPORT_APPROVAL_APPROVE);
    assert(strcmp(action.request_id, "r-7") == 0);
    assert(passport_service_take_action(&service, &action) == PASSPORT_SERVICE_ACTION_EMPTY);
}

static void test_skill_revision_is_bounded(void) {
    passport_service_t service;
    passport_service_snapshot_t snapshot;
    passport_service_action_t action;
    passport_service_init(&service);

    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"skill.revision\",\"skill_id\":\"review\","
               "\"revision\":\"0.4\",\"label\":\"Architecture Review\"}") ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(strcmp(snapshot.skill_revision, "0.4") == 0);
    assert(strcmp(snapshot.active_skill_revision, "0.4") == 0);

    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"skill.revision\",\"skill_id\":\"review\","
               "\"revision\":\"0.5\",\"label\":\"Security Review\"}") ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(strcmp(snapshot.skill_revision, "0.5") == 0);
    assert(strcmp(snapshot.active_skill_revision, "0.4") == 0);
    assert(passport_service_reload_skill(&service) == PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_take_action(&service, &action) == PASSPORT_SERVICE_ACTION_READY);
    assert(action.type == PASSPORT_ACTION_RELOAD_SKILL);
    assert(strcmp(action.skill_revision, "0.5") == 0);
    passport_service_snapshot(&service, &snapshot);
    assert(strcmp(snapshot.active_skill_revision, "0.5") == 0);
}

static void test_one_nfc_card_requests_goal_mode_and_needs_host_ack(void) {
    passport_service_t service;
    passport_service_snapshot_t snapshot;
    passport_service_action_t action;
    passport_service_init(&service);

    assert(passport_service_load_goal_card(&service, "card-1") ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.goal_mode_state == PASSPORT_GOAL_REQUESTED);
    assert(strcmp(snapshot.goal_card_id, "card-1") == 0);
    assert(passport_service_take_action(&service, &action) ==
           PASSPORT_SERVICE_ACTION_READY);
    assert(action.type == PASSPORT_ACTION_GOAL_MODE_REQUEST);
    assert(strcmp(action.card_id, "card-1") == 0);

    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"goal.mode.state\",\"mode\":\"goal\","
               "\"state\":\"enabled\",\"card_id\":\"card-1\","
               "\"ide\":\"codex\","
               "\"session_id\":\"goal-1\"}") == PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.goal_mode_state == PASSPORT_GOAL_ENABLED);
    assert(strcmp(snapshot.goal_ide, "codex") == 0);
    assert(strcmp(snapshot.goal_session_id, "goal-1") == 0);

    assert(passport_service_load_goal_card(&service, "another-card") ==
           PASSPORT_SERVICE_REJECTED);
    passport_service_snapshot(&service, &snapshot);
    assert(strcmp(snapshot.goal_card_id, "card-1") == 0);
}

static void test_tile_stack_switches_mode_and_composes_context(void) {
    passport_service_t service;
    passport_service_snapshot_t snapshot;
    passport_service_init(&service);

    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.page == PASSPORT_PAGE_WEAR_HOME);
    assert(snapshot.stack_count == 0);
    assert(snapshot.compose_status == PASSPORT_COMPOSE_EMPTY);

    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"tile.stack.state\",\"context_id\":\"ctx-14\","
               "\"stack\":["
               "{\"tile_id\":\"aide\",\"role\":\"agent\",\"skill_id\":\"aide\","
               "\"revision\":\"0.4.1\"},"
               "{\"tile_id\":\"project.aide\",\"role\":\"project\",\"skill_id\":\"project\","
               "\"revision\":\"0.1.0\"},"
               "{\"tile_id\":\"review\",\"role\":\"review\",\"skill_id\":\"review\","
               "\"revision\":\"0.3.2\"}]}") == PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.stack_count == 3);
    assert(snapshot.page == PASSPORT_PAGE_COMPOSE_STACK);
    assert(strcmp(snapshot.stack[0].revision, "0.4.1") == 0);
    assert(strcmp(snapshot.stack[2].role, "review") == 0);
    assert(strcmp(snapshot.context_id, "ctx-14") == 0);
    assert(snapshot.compose_status == PASSPORT_COMPOSE_OK);

    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"context.composed\",\"context_id\":\"ctx-14\","
               "\"skills\":[\"aide\",\"project\",\"review\"],\"duration_ms\":640,"
               "\"status\":\"ok\"}") == PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.compose_duration_ms == 640);

    /* Emptying the stack returns to Wear home. */
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"tile.stack.state\",\"context_id\":\"ctx-14\","
               "\"stack\":[]}") == PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.stack_count == 0);
    assert(snapshot.page == PASSPORT_PAGE_WEAR_HOME);
    assert(snapshot.compose_status == PASSPORT_COMPOSE_EMPTY);
}

static void test_task_event_ring_keeps_newest_first(void) {
    passport_service_t service;
    passport_service_snapshot_t snapshot;
    passport_service_action_t action;
    passport_service_init(&service);

    for (int i = 0; i < 6; i++) {
        char line[256];
        snprintf(line, sizeof(line),
                 "{\"type\":\"task.event\",\"task_id\":\"runtime-1\","
                 "\"event_id\":\"e-%d\",\"ts\":\"12:3%d\","
                 "\"summary\":\"step %d\"}", i, i, i);
        assert(passport_service_apply_line(&service, line) == PASSPORT_SERVICE_ACCEPTED);
    }
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.event_count == PASSPORT_SERVICE_EVENT_MAX);
    assert(strcmp(snapshot.events[0].event_id, "e-5") == 0);
    assert(strcmp(snapshot.events[PASSPORT_SERVICE_EVENT_MAX - 1U].event_id, "e-1") == 0);

    assert(passport_service_ack_top_event(&service) == PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_take_action(&service, &action) == PASSPORT_SERVICE_ACTION_READY);
    assert(action.type == PASSPORT_ACTION_TASK_EVENT_ACK);
    assert(strcmp(action.event_id, "e-5") == 0);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.event_count == PASSPORT_SERVICE_EVENT_MAX - 1U);
    assert(strcmp(snapshot.events[0].event_id, "e-4") == 0);
}

static void test_navigation_respects_stack_and_approval(void) {
    passport_service_t service;
    passport_service_snapshot_t snapshot;
    passport_service_init(&service);

    /* Without stack, DOWN on WEAR.HOME cannot reach Compose. */
    assert(passport_service_navigate(&service, PASSPORT_BUTTON_DOWN) ==
           PASSPORT_SERVICE_REJECTED);
    assert(passport_service_navigate(&service, PASSPORT_BUTTON_UP) ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.page == PASSPORT_PAGE_WEAR_TASK);
    assert(passport_service_navigate(&service, PASSPORT_BUTTON_UP) ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.page == PASSPORT_PAGE_WEAR_HOME);

    /* Approval pending blocks navigation. */
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"approval.request\",\"request_id\":\"r-1\","
               "\"summary\":\"apply diff\"}") == PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_navigate(&service, PASSPORT_BUTTON_UP) ==
           PASSPORT_SERVICE_REJECTED);

    /* Once resolved, stack presence lets DOWN reach Compose. */
    assert(passport_service_button(&service, PASSPORT_BUTTON_OK) == PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"tile.stack.state\",\"context_id\":\"ctx-1\","
               "\"stack\":[{\"tile_id\":\"review\",\"role\":\"review\","
               "\"skill_id\":\"review\",\"revision\":\"0.3.2\"}]}") ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.page == PASSPORT_PAGE_COMPOSE_STACK);

    assert(passport_service_navigate(&service, PASSPORT_BUTTON_UP) ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.page == PASSPORT_PAGE_WEAR_HOME);
    assert(passport_service_navigate(&service, PASSPORT_BUTTON_DOWN) ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.page == PASSPORT_PAGE_COMPOSE_STACK);
}

static void test_approval_times_out_after_60_seconds(void) {
    passport_service_t service;
    passport_service_snapshot_t snapshot;
    passport_service_init(&service);

    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"approval.request\",\"request_id\":\"r-t\","
               "\"summary\":\"stale approval\"}") == PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.approval_pending);
    assert(snapshot.approval_elapsed_ms == 0);

    passport_service_tick(&service, 30000);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.approval_pending);
    assert(snapshot.approval_elapsed_ms == 30000);

    passport_service_tick(&service, 30000);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.approval_pending == false);
    assert(snapshot.approval_elapsed_ms == -1);
    assert(snapshot.approval_summary[0] == '\0');
}

int main(void) {
    test_task_update_is_applied_transactionally();
    test_approval_is_emitted_once();
    test_skill_revision_is_bounded();
    test_one_nfc_card_requests_goal_mode_and_needs_host_ack();
    test_tile_stack_switches_mode_and_composes_context();
    test_task_event_ring_keeps_newest_first();
    test_navigation_respects_stack_and_approval();
    test_approval_times_out_after_60_seconds();
    return 0;
}
