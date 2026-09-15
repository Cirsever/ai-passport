/* Integration test for the Passport Service state machine.
 *
 * Walks the full card admission → goal confirmation → task progress →
 * approval → event ack → disconnect banner loop end-to-end, asserting the
 * exact frames and actions emitted at each hop. Complements the narrower
 * unit tests in test_passport_service.c by treating the module as a black
 * box driven only through its public API. */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "passport_service.h"

static void expect_action(passport_service_t *service,
                          passport_action_type_t expected) {
    passport_service_action_t action;
    passport_service_action_result_t result =
        passport_service_take_action(service, &action);
    assert(result == PASSPORT_SERVICE_ACTION_READY);
    assert(action.type == expected);
}

static void expect_no_action(passport_service_t *service) {
    passport_service_action_t action;
    passport_service_action_result_t result =
        passport_service_take_action(service, &action);
    assert(result == PASSPORT_SERVICE_ACTION_EMPTY);
}

int main(void) {
    passport_service_t service;
    passport_service_snapshot_t snapshot;
    passport_service_init(&service);

    /* --- Real card admission --------------------------------------- */
    assert(passport_service_load_goal_card(&service, "card-42") ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.goal_mode_state == PASSPORT_GOAL_REQUESTED);
    assert(strcmp(snapshot.goal_card_id, "card-42") == 0);
    expect_action(&service, PASSPORT_ACTION_GOAL_MODE_REQUEST);

    /* Second card must be rejected while the first is active. */
    assert(passport_service_load_goal_card(&service, "card-99") ==
           PASSPORT_SERVICE_REJECTED);

    /* --- Bridge confirms the IDE session --------------------------- */
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"goal.mode.state\",\"mode\":\"goal\","
               "\"state\":\"enabled\",\"card_id\":\"card-42\","
               "\"ide\":\"codex\",\"session_id\":\"codex-a1b2\"}") ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.goal_mode_state == PASSPORT_GOAL_ENABLED);
    assert(strcmp(snapshot.goal_ide, "codex") == 0);
    assert(strcmp(snapshot.goal_session_id, "codex-a1b2") == 0);
    assert(snapshot.link_idle_ms == 0);

    /* --- Task progress and skill revision -------------------------- */
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"skill.revision\",\"skill_id\":\"review\","
               "\"revision\":\"0.4\",\"label\":\"Code Review\"}") ==
           PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"task.state\",\"task_id\":\"runtime-1\","
               "\"state\":\"running\",\"progress\":42,"
               "\"summary\":\"analyzer running\"}") ==
           PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"task.event\",\"task_id\":\"runtime-1\","
               "\"event_id\":\"e-1\",\"ts\":\"12:30\","
               "\"summary\":\"3 findings queued\"}") ==
           PASSPORT_SERVICE_ACCEPTED);

    /* --- Approval round trip --------------------------------------- */
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"approval.request\",\"request_id\":\"r-7\","
               "\"summary\":\"Apply 3 files changed\"}") ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.approval_pending);
    assert(snapshot.task_state == PASSPORT_TASK_WAITING_APPROVAL);

    /* Approve → decision action, approval clears, elapsed goes back to -1. */
    assert(passport_service_button(&service, PASSPORT_BUTTON_OK) ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_action_t action;
    assert(passport_service_take_action(&service, &action) ==
           PASSPORT_SERVICE_ACTION_READY);
    assert(action.type == PASSPORT_ACTION_APPROVAL);
    assert(action.decision == PASSPORT_APPROVAL_APPROVE);
    assert(strcmp(action.request_id, "r-7") == 0);
    passport_service_snapshot(&service, &snapshot);
    assert(!snapshot.approval_pending);
    assert(snapshot.approval_elapsed_ms == -1);

    /* --- Ack the top event ----------------------------------------- */
    assert(passport_service_ack_top_event(&service) == PASSPORT_SERVICE_ACCEPTED);
    expect_action(&service, PASSPORT_ACTION_TASK_EVENT_ACK);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.event_count == 0);

    /* --- Skill reload --------------------------------------------- */
    assert(passport_service_reload_skill(&service) == PASSPORT_SERVICE_ACCEPTED);
    expect_action(&service, PASSPORT_ACTION_RELOAD_SKILL);
    expect_no_action(&service);

    /* --- Disconnect: no frames for 40 seconds --------------------- */
    for (int i = 0; i < 400; ++i) {
        passport_service_tick(&service, 100);
    }
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.link_idle_ms >= 40000);

    /* Next accepted frame resets link_idle_ms to 0. */
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"task.state\",\"task_id\":\"runtime-1\","
               "\"state\":\"done\",\"progress\":100,"
               "\"summary\":\"reconnected\"}") ==
           PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.link_idle_ms == 0);

    printf("integration test PASS\n");
    return 0;
}
