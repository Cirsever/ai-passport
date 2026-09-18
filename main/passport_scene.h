#pragma once

#include "passport_service.h"

#define PASSPORT_SCENE_SINGLE_MS 3200U
#define PASSPORT_SCENE_MULTI_MS 1400U
#define PASSPORT_SCENE_STAGGER_MS 120U
#define PASSPORT_SCENE_WIDTH 198
#define PASSPORT_SCENE_HEIGHT 174

/* No LVGL dependencies: the same geometry can be exported by host tools. */
typedef struct {
    char ids[PASSPORT_SERVICE_STACK_MAX][PASSPORT_SERVICE_ID_MAX];
    size_t count;
    uint32_t generation;
    uint32_t elapsed_ms;
    uint32_t phase_ms;
    uint8_t companion_palette[32];
    uint8_t companion_indices[512];
    uint32_t companion_generation;
    bool companion_valid;
} passport_scene_t;

typedef void (*passport_scene_rect_fn)(void *ctx, int x, int y, int w, int h,
                                       uint32_t color);

void passport_scene_update(passport_scene_t *scene,
                           const passport_service_snapshot_t *snapshot,
                           uint32_t elapsed_ms, bool paused);
bool passport_scene_loading(const passport_scene_t *scene);
void passport_scene_set_companion(passport_scene_t *scene,
                                  const uint8_t *packed_asset,
                                  uint32_t generation);
void passport_scene_draw(const passport_scene_t *scene,
                         const passport_service_snapshot_t *snapshot,
                         passport_scene_rect_fn rect, void *ctx);
