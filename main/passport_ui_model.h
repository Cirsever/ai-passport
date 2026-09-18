#pragma once

#include "passport_service.h"

#define PASSPORT_UI_MODEL_TEXT_MAX 96U
#define PASSPORT_UI_MODEL_BODY_ROWS 6U
#define PASSPORT_VOICE_COMPLETED_VISIBLE_MS 2000U

typedef enum {
    PASSPORT_VOICE_FEEDBACK_HIDDEN = 0,
    PASSPORT_VOICE_FEEDBACK_ACTIVE,
    PASSPORT_VOICE_FEEDBACK_COMPLETED,
} passport_voice_feedback_view_t;

typedef struct {
    bool completed_seen;
    uint32_t completed_elapsed_ms;
} passport_voice_feedback_t;

typedef struct {
    char mode[PASSPORT_UI_MODEL_TEXT_MAX];
    char link[PASSPORT_UI_MODEL_TEXT_MAX];
    char battery[PASSPORT_UI_MODEL_TEXT_MAX];
    char title[PASSPORT_UI_MODEL_TEXT_MAX];
    char body[PASSPORT_UI_MODEL_BODY_ROWS][PASSPORT_UI_MODEL_TEXT_MAX];
    size_t body_rows;
    char hint[PASSPORT_UI_MODEL_TEXT_MAX];
    bool approval_overlay;
    char approval_summary[PASSPORT_UI_MODEL_TEXT_MAX];

    /* Voice capture overlay populated by the demo layer from the worker
     * snapshot. Kept as plain data so ui_model stays pure-C and host tests
     * can inject arbitrary values. Overlay is inhibited when
     * approval_overlay is true — approval always wins.
     * When both are false the overlay is not rendered. */
    bool voice_overlay;
    char voice_line[PASSPORT_UI_MODEL_TEXT_MAX];

    /* Compatibility mirrors for host tests that still reference the legacy
     * single-page fields. New callers should read from body[] instead. */
    char task[PASSPORT_UI_MODEL_TEXT_MAX];
    char summary[PASSPORT_UI_MODEL_TEXT_MAX];
    char card[PASSPORT_UI_MODEL_TEXT_MAX];
    char goal[PASSPORT_UI_MODEL_TEXT_MAX];
} passport_ui_model_t;

void passport_ui_model_build(const passport_service_snapshot_t *snapshot,
                             bool transport_ready,
                             int battery_soc, passport_ui_model_t *model);

void passport_voice_feedback_init(passport_voice_feedback_t *feedback);

passport_voice_feedback_view_t passport_voice_feedback_update(
    passport_voice_feedback_t *feedback, bool active, bool completed,
    uint32_t elapsed_ms);
