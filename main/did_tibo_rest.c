#include "did_tibo_rest.h"
#include <stdio.h>
#include <string.h>
static bool field(const char *json, const char *name, char *out, size_t size) {
    char needle[32]; int n = snprintf(needle, sizeof(needle), "\"%s\":\"", name);
    if (n <= 0 || (size_t)n >= sizeof(needle)) return false;
    const char *start = strstr(json, needle); if (!start) return false; start += n;
    const char *end = strchr(start, '\"'); if (!end || end == start) return false;
    size_t len = (size_t)(end - start); if (len >= size) len = size - 1;
    memcpy(out, start, len); out[len] = '\0'; return true;
}
void tibo_rest_init(tibo_rest_t *rest, uint8_t level, uint32_t interval) {
    memset(rest, 0, sizeof(*rest)); rest->snapshot.level = level < 3 ? level : 1; rest->interval_ms = interval;
}
bool tibo_rest_apply_push(tibo_rest_t *rest, const char *json, uint32_t now) {
    char type[20];
    if (!json || !field(json, "type", type, sizeof(type)) || strcmp(type, "tibo.push") != 0) return false;
    (void)field(json, "title", rest->snapshot.title, sizeof(rest->snapshot.title));
    (void)field(json, "body", rest->snapshot.body, sizeof(rest->snapshot.body));
    rest->snapshot.state = TIBO_REST_PENDING; rest->snapshot.reminder_count = 0;
    rest->snapshot.next_reminder_ms = now + rest->interval_ms; rest->beep_requested = true; return true;
}
void tibo_rest_tick(tibo_rest_t *rest, uint32_t now) {
    if (rest->snapshot.state != TIBO_REST_PENDING || now < rest->snapshot.next_reminder_ms) return;
    rest->snapshot.reminder_count++; rest->snapshot.next_reminder_ms = now + rest->interval_ms; rest->beep_requested = true;
}
void tibo_rest_mute(tibo_rest_t *rest) { rest->snapshot.state = TIBO_REST_MUTED; rest->beep_requested = false; }
void tibo_rest_cycle_level(tibo_rest_t *rest) { rest->snapshot.level = (rest->snapshot.level + 1) % 3; }
bool tibo_rest_take_beep(tibo_rest_t *rest) { bool value = rest->beep_requested; rest->beep_requested = false; return value; }
void tibo_rest_snapshot(const tibo_rest_t *rest, tibo_rest_snapshot_t *snapshot) { *snapshot = rest->snapshot; }
