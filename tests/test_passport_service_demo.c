#include <assert.h>

#include "passport_service.h"

int main(void) {
    passport_service_t service;
    passport_service_snapshot_t snapshot;
    passport_service_init(&service);

    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"task.state\",\"task_id\":\"runtime-1\","
               "\"state\":\"running\",\"progress\":42,"
               "\"summary\":\"Refactoring tracing\"}") == PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"skill.revision\",\"skill_id\":\"review\","
               "\"revision\":\"0.4\",\"label\":\"Architecture Review\"}") ==
           PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"approval.request\",\"request_id\":\"r-7\","
               "\"summary\":\"Apply 3 files changed\"}") == PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.task_state == PASSPORT_TASK_WAITING_APPROVAL);

    assert(passport_service_load_goal_card(&service, "card-1") ==
           PASSPORT_SERVICE_ACCEPTED);
    assert(passport_service_apply_line(
               &service,
               "{\"type\":\"goal.mode.state\",\"mode\":\"goal\","
               "\"state\":\"enabled\",\"card_id\":\"card-1\","
               "\"ide\":\"codex\","
               "\"session_id\":\"goal-1\"}") == PASSPORT_SERVICE_ACCEPTED);
    passport_service_snapshot(&service, &snapshot);
    assert(snapshot.goal_mode_state == PASSPORT_GOAL_ENABLED);
    return 0;
}
