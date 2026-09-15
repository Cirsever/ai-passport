#pragma once

#include "passport_service.h"

#define PASSPORT_UI_MODEL_TEXT_MAX 96U
#define PASSPORT_UI_MODEL_BODY_ROWS 6U

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
    /* Non-empty when the host link has been idle past its stale threshold; the
     * demo layer paints it as a full-body disconnected banner. */
    char disconnected_banner[PASSPORT_UI_MODEL_TEXT_MAX];

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
