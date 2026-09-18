#include "passport_scene.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void apply(passport_service_t *service, const char *line) {
    assert(passport_service_apply_line(service, line) == PASSPORT_SERVICE_ACCEPTED);
}

static void check_rect(void *ctx, int x, int y, int w, int h, uint32_t color) {
    (void)color;
    unsigned *count = ctx;
    ++*count;
    assert(x >= 0 && y >= 0 && w > 0 && h > 0);
    assert(x + w <= PASSPORT_SCENE_WIDTH && y + h <= PASSPORT_SCENE_HEIGHT);
}

static void test_observation_and_timing(void) {
    passport_service_t service;
    passport_scene_t scene = {0};
    passport_service_init(&service);
    apply(&service, "{\"type\":\"nfc.present\",\"card_id\":\"card-1\"}");
    assert(service.state.goal_card_id[0] == '\0');
    assert(service.state.goal_mode_state == PASSPORT_GOAL_DISABLED);
    assert(service.action.type == PASSPORT_ACTION_NONE);
    passport_scene_update(&scene, &service.state, 100, false);
    assert(scene.count == 1 && scene.elapsed_ms == 0);
    passport_scene_update(&scene, &service.state, 3199, false);
    assert(passport_scene_loading(&scene));
    apply(&service, "{\"type\":\"nfc.present\",\"card_id\":\"card-1\"}");
    passport_scene_update(&scene, &service.state, 5000, true);
    assert(scene.elapsed_ms == 3199);
    passport_scene_update(&scene, &service.state, 1, false);
    assert(!passport_scene_loading(&scene) && scene.count == 1);
    passport_scene_update(&scene, &service.state, UINT32_MAX, false);
    assert(scene.elapsed_ms == 3200);
    passport_service_snapshot_t before = service.state;
    const char *invalid[] = {
        "{\"type\":\"nfc.present\",\"card_id\":\"\"}",
        "{\"type\":\"nfc.present\",\"card_id\":\"a b\"}",
        "{\"type\":\"nfc.present\",\"card_id\":\"卡\"}",
        "{\"type\":\"nfc.present\",\"card_id\":\"123456789012345678901234567890123456789012345678\"}"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        assert(passport_service_apply_line(&service, invalid[i]) == PASSPORT_SERVICE_REJECTED);
        assert(memcmp(&before, &service.state, sizeof(before)) == 0);
    }
}

static void test_stack_lifecycle(void) {
    passport_service_t service;
    passport_scene_t scene = {0};
    passport_service_init(&service);
    const char *stack = "{\"type\":\"tile.stack.state\",\"context_id\":\"c1\","
        "\"stack\":[{\"tile_id\":\"a\"},{\"tile_id\":\"b\"},{\"tile_id\":\"c\"}]}";
    apply(&service, stack);
    assert(!service.state.compose_confirmed);
    passport_scene_update(&scene, &service.state, 0, false);
    assert(scene.count == 3);
    passport_scene_update(&scene, &service.state, 1399, false);
    assert(passport_scene_loading(&scene));
    passport_scene_update(&scene, &service.state, 1, false);
    assert(!passport_scene_loading(&scene));
    assert(!service.state.compose_confirmed);
    apply(&service, "{\"type\":\"context.composed\",\"context_id\":\"c1\",\"status\":\"conflict\"}");
    apply(&service, stack);
    passport_scene_update(&scene, &service.state, 0, false);
    assert(!passport_scene_loading(&scene));
    assert(service.state.compose_confirmed);
    assert(service.state.compose_status == PASSPORT_COMPOSE_CONFLICT);
    apply(&service, "{\"type\":\"tile.stack.state\",\"context_id\":\"c2\","
        "\"stack\":[{\"tile_id\":\"b\"},{\"tile_id\":\"a\"},{\"tile_id\":\"c\"}]}");
    passport_scene_update(&scene, &service.state, 0, true);
    assert(passport_scene_loading(&scene) && strcmp(scene.ids[0], "b") == 0);
    passport_scene_update(&scene, &service.state, 5000, true);
    assert(scene.elapsed_ms == 0);
    apply(&service, "{\"type\":\"tile.stack.state\",\"context_id\":\"c3\",\"stack\":[]}");
    passport_scene_update(&scene, &service.state, 0, false);
    assert(scene.count == 0);
    apply(&service, stack);
    passport_scene_update(&scene, &service.state, 0, false);
    assert(scene.count == 3 && scene.elapsed_ms == 0);
    assert(passport_service_apply_line(&service,
        "{\"type\":\"tile.stack.state\",\"context_id\":\"c4\","
        "\"stack\":[{\"tile_id\":\"a\"},{\"tile_id\":\"b\"},{\"tile_id\":\"c\"},"
        "{\"tile_id\":\"d\"},{\"tile_id\":\"e\"}]}") == PASSPORT_SERVICE_REJECTED);
    assert(scene.count == 3);
    apply(&service, "{\"type\":\"tile.stack.state\",\"context_id\":\"c5\",\"stack\":[]}");
    assert(passport_service_load_goal_card(&service, "local-reader") == PASSPORT_SERVICE_ACCEPTED);
    passport_scene_update(&scene, &service.state, 0, false);
    assert(scene.count == 1 && strcmp(scene.ids[0], "local-reader") == 0);
}

static void test_geometry(void) {
    passport_service_snapshot_t snapshot = {0};
    for (size_t n = 0; n <= 4; n++) {
        snapshot.stack_count = n;
        passport_scene_t scene = {.count = n};
        for (uint32_t ms = 0; ms <= 3200; ms += 100) {
            scene.elapsed_ms = ms;
            scene.phase_ms = ms % 2400;
            for (size_t selected = 0; selected < (n ? n : 1); selected++) {
                snapshot.stack_selected = selected;
                unsigned rects = 0;
                passport_scene_draw(&scene, &snapshot, check_rect, &rects);
                assert(rects > 20 && rects < 200);
            }
        }
    }
}

int main(void) {
    test_observation_and_timing();
    test_stack_lifecycle();
    test_geometry();
    puts("Passport scene: PASS");
    return 0;
}
