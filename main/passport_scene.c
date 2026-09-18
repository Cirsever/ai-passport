#include "passport_scene.h"

#include <string.h>

#define INK 0x263B4DU
#define PAPER 0xFFF7E3U
#define BLUE 0x69BBD1U
#define GREEN 0x8DBF91U
#define GOLD 0xF1C76BU
#define RED 0xD97667U

static const char *card_id(const passport_service_snapshot_t *s, size_t i) {
    if (s->stack_count) return s->stack[i].tile_id;
    if (s->nfc_card_id[0]) return s->nfc_card_id;
    return s->stack_generation == 0 ? s->goal_card_id : "";
}

void passport_scene_update(passport_scene_t *scene,
                           const passport_service_snapshot_t *s,
                           uint32_t delta, bool paused) {
    size_t count = s->stack_count;
    if (count > PASSPORT_SERVICE_STACK_MAX) count = PASSPORT_SERVICE_STACK_MAX;
    if (!count && card_id(s, 0)[0]) count = 1;
    bool changed = count != scene->count || s->stack_generation != scene->generation;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(scene->ids[i], card_id(s, i)) != 0) changed = true;
    }
    if (changed) {
        memset(scene->ids, 0, sizeof(scene->ids));
        for (size_t i = 0; i < count; i++) {
            strncpy(scene->ids[i], card_id(s, i), PASSPORT_SERVICE_ID_MAX - 1);
        }
        scene->count = count;
        scene->generation = s->stack_generation;
        scene->elapsed_ms = 0;
    } else if (!paused) {
        uint32_t limit = count > 1 ? PASSPORT_SCENE_MULTI_MS : PASSPORT_SCENE_SINGLE_MS;
        if (delta >= limit - scene->elapsed_ms) scene->elapsed_ms = limit;
        else scene->elapsed_ms += delta;
    }
    if (!paused) scene->phase_ms = (scene->phase_ms + delta % 2400U) % 2400U;
}

bool passport_scene_loading(const passport_scene_t *scene) {
    return scene->count && scene->elapsed_ms <
        (scene->count > 1 ? PASSPORT_SCENE_MULTI_MS : PASSPORT_SCENE_SINGLE_MS);
}

void passport_scene_set_companion(passport_scene_t *scene,
                                  const uint8_t *packed_asset,
                                  uint32_t generation) {
    if (!scene || !packed_asset || generation == scene->companion_generation) return;
    memcpy(scene->companion_palette, packed_asset, sizeof(scene->companion_palette));
    memcpy(scene->companion_indices, packed_asset + sizeof(scene->companion_palette),
           sizeof(scene->companion_indices));
    scene->companion_generation = generation;
    scene->companion_valid = true;
}

/* Small fixed sprites: each bit is one crisp square; no bitmap allocation. */
static void sprite(passport_scene_rect_fn r, void *ctx, int x, int y,
                   const uint16_t *rows, int n, int columns, int scale,
                   uint32_t color) {
    for (int row = 0; row < n; row++) {
        for (int col = 0; col < columns; col++) {
            if (rows[row] & (1U << (columns - col - 1))) {
                r(ctx, x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

static void robot(passport_scene_rect_fn r, void *ctx, int x, int y, int p,
                  bool blink) {
    static const uint16_t outline[] = {
        0x018, 0x018, 0x07e, 0x0ff, 0x1ff, 0x1ff,
        0x1ff, 0x0ff, 0x07e, 0x066, 0x066, 0x0e7
    };
    sprite(r, ctx, x, y, outline, 12, 9, p, INK);
    r(ctx, x + p * 2, y + p * 4, p * 5, p * 3, PAPER);
    r(ctx, x + p * 3, y + p * 4, p, blink ? p : p * 2, BLUE);
    r(ctx, x + p * 5, y + p * 4, p, blink ? p : p * 2, BLUE);
    r(ctx, x + p * 3, y + p * 8, p * 3, p, GOLD);
}

static uint32_t palette_color(const uint8_t *palette, unsigned index) {
    uint16_t rgb565 = (uint16_t)palette[index * 2U] |
                      (uint16_t)palette[index * 2U + 1U] << 8;
    uint32_t red = (rgb565 >> 11) & 0x1FU;
    uint32_t green = (rgb565 >> 5) & 0x3FU;
    uint32_t blue = rgb565 & 0x1FU;
    return ((red * 255U / 31U) << 16) |
           ((green * 255U / 63U) << 8) |
           (blue * 255U / 31U);
}

static void companion(const passport_scene_t *scene,
                      passport_scene_rect_fn r, void *ctx,
                      int x, int y, int scale) {
    for (int row = 0; row < 32; row++) {
        int run_start = -1;
        unsigned run_color = 0;
        for (int column = 0; column <= 32; column++) {
            unsigned index = 0;
            if (column < 32) {
                uint8_t packed = scene->companion_indices[row * 16 + column / 2];
                index = column & 1 ? packed & 0x0FU : packed >> 4;
            }
            if (index != run_color) {
                if (run_color && run_start >= 0) {
                    r(ctx, x + run_start * scale, y + row * scale,
                      (column - run_start) * scale, scale,
                      palette_color(scene->companion_palette, run_color));
                }
                run_start = column;
                run_color = index;
            }
        }
    }
}

void passport_scene_draw(const passport_scene_t *scene,
                         const passport_service_snapshot_t *s,
                         passport_scene_rect_fn r, void *ctx) {
    r(ctx, 0, 0, 198, 174, PAPER);
    r(ctx, 0, 0, 198, 117, 0xE4F1ED);
    /* Distant pixel clouds and a dotted reader field. */
    r(ctx, 8, 16, 28, 5, 0xFFFFFF);
    r(ctx, 14, 10, 16, 6, 0xFFFFFF);
    r(ctx, 163, 31, 29, 5, 0xFFFFFF);
    r(ctx, 169, 25, 17, 6, 0xFFFFFF);
    for (int x = 10; x < 198; x += 18) r(ctx, x, 108, 2, 2, 0xACCFC6);
    r(ctx, 20, 117, 164, 8, 0xD7D2BE);
    r(ctx, 16, 112, 164, 9, INK);
    r(ctx, 19, 112, 158, 4, BLUE);
    for (int x = 83; x < 116; x += 11) {
        r(ctx, x, 118, 6, 2, scene->phase_ms / 200U % 3U ==
            (unsigned)(x - 83) / 11U ? GOLD : 0x789099);
    }
    if (!scene->count) {
        if (scene->companion_valid) {
            companion(scene, r, ctx, 67,
                      27 + (scene->phase_ms < 1200 ? 0 : 2), 2);
        } else {
            robot(r, ctx, 77, 43 + (scene->phase_ms < 1200 ? 0 : 2), 5,
                  scene->phase_ms > 2200);
        }
        r(ctx, 36, 56, 12, 3, BLUE);
        r(ctx, 42, 50, 3, 15, BLUE);
        r(ctx, 151, 74, 9, 3, GOLD);
        r(ctx, 154, 71, 3, 9, GOLD);
    }
    static const uint32_t colors[] = {BLUE, GOLD, GREEN, RED};
    for (size_t layer = scene->count; layer > 0; layer--) {
        size_t i = layer - 1;
        int x = 28 + (int)i * 13;
        int y = 30 - (int)i * 8;
        uint32_t delay = (uint32_t)(scene->count - 1U - i) * PASSPORT_SCENE_STAGGER_MS;
        uint32_t age = scene->elapsed_ms > delay ? scene->elapsed_ms - delay : 0;
        if (age < 400U) y += (int)((400U - age) * 18U / 400U);
        uint32_t accent = colors[i];
        if (s->stack_count) {
            const char *role = s->stack[i].role;
            if (strcmp(role, "project") == 0) accent = GOLD;
            else if (strcmp(role, "skill") == 0) accent = GREEN;
            else if (strcmp(role, "review") == 0) accent = RED;
        }
        bool selected = i == s->stack_selected;
        r(ctx, x + 4, y + 4, 126, 75, 0xAAB9AF);
        r(ctx, x, y, 126, 75, selected ? INK : 0x77908B);
        r(ctx, x + 3, y + 3, 120, 69, PAPER);
        r(ctx, x + 3, y + 3, 120, 8, accent);
        /* Visible side tab keeps every layer countable and selectable. */
        r(ctx, x + 114, y + 15, 9, 17, selected ? GOLD : accent);
        for (size_t dot = 0; dot <= i; dot++)
            r(ctx, x + 116, y + 17 + (int)dot * 3, 4, 2, INK);
        if (i != 0) continue;
        if (scene->companion_valid) {
            companion(scene, r, ctx, x + 11, y + 18, 1);
        } else {
            robot(r, ctx, x + 13, y + 19, 3, scene->phase_ms > 2200);
        }
        r(ctx, x + 51, y + 26, 45, 4, INK);
        r(ctx, x + 51, y + 36, 32, 3, accent);
        for (int dot = 0; dot < 5; dot++)
            r(ctx, x + 51 + dot * 9, y + 48, 5, 5, dot < 3 ? accent : 0xDEDACB);
        if (passport_scene_loading(scene)) {
            int scan = (int)(scene->phase_ms % 900U) * 53 / 900;
            r(ctx, x + 5, y + 15 + scan, 105, 2, BLUE);
            r(ctx, x + 5, y + 17 + scan, 105, 1, 0xB1DDE0);
        }
    }
    /* Real task progress, not a made-up loading percentage. */
    if (!scene->count && s->task_state != PASSPORT_TASK_IDLE) {
        unsigned progress = s->progress > 100 ? 100 : s->progress;
        r(ctx, 27, 131, 144, 7, INK);
        r(ctx, 29, 133, 140, 3, 0xD9DFD6);
        if (progress) r(ctx, 29, 133, (int)(140 * progress / 100), 3, GREEN);
    } else if (passport_scene_loading(scene)) {
        for (unsigned i = 0; i < 3; i++) {
            r(ctx, 85 + (int)i * 11, 132, 5, 5,
              scene->phase_ms / 200U % 3U == i ? INK : 0xC4D6CF);
        }
    }
}
