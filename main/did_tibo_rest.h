#pragma once
#include <stdbool.h>
#include <stdint.h>
#define TIBO_REST_TEXT_MAX 80
typedef enum { TIBO_REST_IDLE = 0, TIBO_REST_PENDING, TIBO_REST_MUTED } tibo_rest_state_t;
typedef struct { tibo_rest_state_t state; uint8_t level; uint32_t reminder_count; uint32_t next_reminder_ms; char title[TIBO_REST_TEXT_MAX]; char body[TIBO_REST_TEXT_MAX]; } tibo_rest_snapshot_t;
typedef struct { tibo_rest_snapshot_t snapshot; bool beep_requested; uint32_t interval_ms; } tibo_rest_t;
void tibo_rest_init(tibo_rest_t *, uint8_t, uint32_t);
bool tibo_rest_apply_push(tibo_rest_t *, const char *, uint32_t);
void tibo_rest_tick(tibo_rest_t *, uint32_t);
void tibo_rest_mute(tibo_rest_t *);
void tibo_rest_cycle_level(tibo_rest_t *);
bool tibo_rest_take_beep(tibo_rest_t *);
void tibo_rest_snapshot(const tibo_rest_t *, tibo_rest_snapshot_t *);
