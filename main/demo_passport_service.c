#include "demo_passport_service.h"

#include "bsp_display.h"
#include "bsp_battery.h"
#include "passport_service.h"
#include "passport_v2_state.h"
#include "passport_ui_model.h"
#include "passport_scene.h"
#include "passport_transport_usb.h"
#include "passport_voice_worker.h"
#include "ui_pixel.h"
#include "ui_cn_16.h"

#include "esp_log.h"
#include "lvgl.h"
#include "mbedtls/sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

static const char *TAG = "demo_passport_service";

#define UI_TEXT_FONT (&ui_cn_16)

static passport_service_t s_service;
static passport_v2_state_t s_v2;
static lv_obj_t *s_screen;
static lv_obj_t *s_header;
static lv_obj_t *s_header_mode;
static lv_obj_t *s_header_link;
static lv_obj_t *s_header_link_dot;
static lv_obj_t *s_header_battery;
static lv_obj_t *s_battery_segments[4];
static lv_obj_t *s_battery_unknown;
static lv_obj_t *s_title;
static lv_obj_t *s_body_rows[PASSPORT_UI_MODEL_BODY_ROWS];
static lv_obj_t *s_approval_panel;
static lv_obj_t *s_approval_title;
static lv_obj_t *s_approval_label;
static lv_obj_t *s_voice_panel;
static lv_obj_t *s_voice_title;
static lv_obj_t *s_voice_label;
static lv_obj_t *s_voice_bars[10];
static lv_obj_t *s_hint_panel;
static lv_obj_t *s_hint;
static lv_timer_t *s_service_timer;
static unsigned s_battery_elapsed_ms;
static int s_battery_soc = -1;
static bool s_transport_ready;
static bool s_hello_sent;
static unsigned s_hello_elapsed_ms;
static passport_voice_feedback_t s_voice_feedback;
static passport_scene_t s_scene;
static passport_service_snapshot_t s_scene_snapshot;
static lv_obj_t *s_scene_obj;
static lv_obj_t *s_scene_status;
static lv_obj_t *s_scene_detail;
static uint32_t s_companion_generation;

typedef struct {
    lv_layer_t *layer;
    lv_area_t origin;
} scene_draw_context_t;

static void scene_rect(void *context, int x, int y, int w, int h, uint32_t color) {
    scene_draw_context_t *draw = context;
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.bg_opa = LV_OPA_COVER;
    dsc.radius = 0;
    lv_area_t area = {
        .x1 = draw->origin.x1 + x, .y1 = draw->origin.y1 + y,
        .x2 = draw->origin.x1 + x + w - 1, .y2 = draw->origin.y1 + y + h - 1,
    };
    lv_draw_rect(draw->layer, &dsc, &area);
}

static void scene_draw(lv_event_t *event) {
    scene_draw_context_t context = {.layer = lv_event_get_layer(event)};
    lv_obj_get_coords(lv_event_get_target_obj(event), &context.origin);
    passport_scene_draw(&s_scene, &s_scene_snapshot, scene_rect, &context);
}

static const char *scene_role(const char *role) {
    if (strcmp(role, "agent") == 0) return "主体";
    if (strcmp(role, "project") == 0) return "项目";
    if (strcmp(role, "review") == 0) return "审查";
    return "技能";
}

static bool verify_sha256(const uint8_t *data, size_t length,
                          const char *expected_hex) {
    unsigned char digest[32];
    if (!data || !expected_hex || strlen(expected_hex) != 64U) return false;
    if (mbedtls_sha256(data, length, digest, 0) != 0) return false;
    for (size_t i = 0; i < sizeof(digest); i++) {
        char pair[3] = {expected_hex[i * 2U], expected_hex[i * 2U + 1U], '\0'};
        char *end = NULL;
        unsigned long expected = strtoul(pair, &end, 16);
        if (!end || *end != '\0' || expected != digest[i]) return false;
    }
    return true;
}

static void refresh_battery_icon(void) {
    if (!s_header_battery) return;
    int segments = passport_v2_battery_segments(s_battery_soc);
    if (segments < 0) segments = 0;
    uint32_t color = s_battery_soc <= 10 ? UI_RED :
                     s_battery_soc <= 20 ? UI_ORANGE : 0x47A863;
    for (size_t i = 0; i < 4; i++) {
        lv_obj_set_style_bg_opa(
            s_battery_segments[i], (int)i < segments ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(s_battery_segments[i], lv_color_hex(color), 0);
    }
    if (s_battery_unknown) {
        if (s_battery_soc < 0) lv_obj_remove_flag(s_battery_unknown, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_battery_unknown, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *service_block(lv_obj_t *parent, int x, int y, int w, int h,
                               uint32_t bg) {
    lv_obj_t *block = lv_obj_create(parent);
    lv_obj_remove_flag(block, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(block, x, y);
    lv_obj_set_size(block, w, h);
    lv_obj_set_style_radius(block, 0, 0);
    lv_obj_set_style_bg_color(block, lv_color_hex(bg), 0);
    lv_obj_set_style_border_width(block, 0, 0);
    lv_obj_set_style_pad_all(block, 0, 0);
    return block;
}

static bool card_id_is_normalized(const char *card_id) {
    if (!card_id || card_id[0] == '\0' || strlen(card_id) >= PASSPORT_SERVICE_ID_MAX) {
        return false;
    }
    for (const char *cursor = card_id; *cursor != '\0'; cursor++) {
        if (!isalnum((unsigned char)*cursor) && *cursor != '-' && *cursor != '_' &&
            *cursor != ':' && *cursor != '.') {
            return false;
        }
    }
    return true;
}

static void refresh_ui_locked(uint32_t elapsed_ms) {
    /* Both structs are large (snapshot ~2 KB, model ~700 B). Keep them off the
     * caller's stack so LVGL and button callbacks running on the main task's
     * 4 KB stack do not trip stack-protection. */
    static passport_service_snapshot_t snapshot;
    static passport_ui_model_t model;
    passport_service_snapshot(&s_service, &snapshot);
    passport_ui_model_build(&snapshot, s_transport_ready,
                            s_battery_soc, &model);

    /* The worker publishes plain status data; this LVGL-timer callback owns
     * both the text meter and the visual bars. Keep the meter ASCII-only so
     * changing levels never changes the CJK font subset. */
    passport_voice_status_t vst;
    passport_voice_worker_snapshot(&vst);
    passport_voice_feedback_view_t voice_view = passport_voice_feedback_update(
        &s_voice_feedback, vst.active, vst.completed, elapsed_ms);
    if (voice_view == PASSPORT_VOICE_FEEDBACK_ACTIVE &&
        !model.approval_overlay) {
        int bars = (int)vst.level / 10;
        if (bars < 0) bars = 0;
        if (bars > 10) bars = 10;
        char meter[24] = "[";
        int mi = 1;
        for (int i = 0; i < 10; ++i) meter[mi++] = (i < bars) ? '#' : '-';
        meter[mi++] = ']';
        meter[mi] = '\0';
        snprintf(model.voice_line, sizeof(model.voice_line),
                 "%s %us %s", "录音中", (unsigned)(vst.elapsed_ms / 1000u),
                 meter);
        model.voice_overlay = true;
    } else if (voice_view == PASSPORT_VOICE_FEEDBACK_COMPLETED &&
               !model.approval_overlay) {
        snprintf(model.voice_line, sizeof(model.voice_line), "%s",
                 "录音已停止");
        model.voice_overlay = true;
    }

    const passport_v2_companion_t *companion_asset = passport_v2_companion(&s_v2);
    if (companion_asset &&
        companion_asset->generation != s_companion_generation) {
        passport_scene_set_companion(&s_scene, companion_asset->bytes,
                                     companion_asset->generation);
        s_companion_generation = companion_asset->generation;
    }

    bool session_view = s_v2.view == PASSPORT_V2_VIEW_SESSIONS ||
                        s_v2.view == PASSPORT_V2_VIEW_SWITCHING;
    if (session_view) {
        model.body_rows = s_v2.session_count;
        memset(model.body, 0, sizeof(model.body));
        snprintf(model.title, sizeof(model.title), "%s",
                 s_v2.view == PASSPORT_V2_VIEW_SWITCHING
                     ? "正在切换" : "选择会话");
        for (size_t i = 0; i < s_v2.session_count; i++) {
            snprintf(model.body[i], sizeof(model.body[i]), "%c %s",
                     i == s_v2.session_selected ? '>' : ' ',
                     s_v2.sessions[i].title);
        }
        if (s_v2.session_count == 0) {
            model.body_rows = 1;
            snprintf(model.body[0], sizeof(model.body[0]), "%s", "没有可用会话");
        }
        snprintf(model.hint, sizeof(model.hint), "%s",
                 s_v2.view == PASSPORT_V2_VIEW_SWITCHING
                     ? "长按 上 取消并核对"
                     : "上 下 选择  确 切换");
    }

    bool v2_approval = s_v2.view == PASSPORT_V2_VIEW_APPROVAL ||
                       s_v2.view == PASSPORT_V2_VIEW_APPROVAL_DETAIL;
    if (v2_approval) {
        model.approval_overlay = true;
        if (s_v2.view == PASSPORT_V2_VIEW_APPROVAL_DETAIL) {
            snprintf(model.approval_summary, sizeof(model.approval_summary), "%s",
                     s_v2.approval_detail[0] ? s_v2.approval_detail
                                             : s_v2.approval_summary);
            snprintf(model.hint, sizeof(model.hint), "%s", "上 返回  下 下页  确 允许");
        } else if (s_v2.approval_status == PASSPORT_V2_APPROVAL_SENDING) {
            snprintf(model.approval_summary, sizeof(model.approval_summary), "%s",
                     "正在发送决定");
            snprintf(model.hint, sizeof(model.hint), "%s", "等待 IDE 回执");
        } else if (s_v2.approval_status == PASSPORT_V2_APPROVAL_ALLOWED) {
            snprintf(model.approval_summary, sizeof(model.approval_summary), "%s",
                     "已允许");
            snprintf(model.hint, sizeof(model.hint), "%s", "决定已送达 IDE");
        } else if (s_v2.approval_status == PASSPORT_V2_APPROVAL_DENIED) {
            snprintf(model.approval_summary, sizeof(model.approval_summary), "%s",
                     "已拒绝");
            snprintf(model.hint, sizeof(model.hint), "%s", "决定已送达 IDE");
        } else if (s_v2.approval_status == PASSPORT_V2_APPROVAL_EXPIRED) {
            snprintf(model.approval_summary, sizeof(model.approval_summary), "%s",
                     "这次请求已过期");
            snprintf(model.hint, sizeof(model.hint), "%s", "返回当前会话");
        } else if (s_v2.approval_status == PASSPORT_V2_APPROVAL_UNKNOWN) {
            snprintf(model.approval_summary, sizeof(model.approval_summary), "%s",
                     "决定尚未确认");
            snprintf(model.hint, sizeof(model.hint), "%s", "回到电脑核对");
        } else {
            snprintf(model.approval_summary, sizeof(model.approval_summary), "%s",
                     s_v2.approval_summary);
            snprintf(model.hint, sizeof(model.hint), "%s",
                     s_v2.approval_allow
                         ? "上 拒绝  下 详情  确 允许"
                         : "上 拒绝  下 详情");
        }
    }

    bool show_scene = snapshot.page != PASSPORT_PAGE_WEAR_TASK && !session_view;
    bool covered = model.approval_overlay || model.voice_overlay;
    passport_scene_update(&s_scene, &snapshot, elapsed_ms, covered || !show_scene);
    s_scene_snapshot = snapshot;
    if (s_scene_obj) {
        if (show_scene && !covered) lv_obj_remove_flag(s_scene_obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_scene_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(s_scene_obj);
        char status[96];
        char detail[128];
        const bool loading = passport_scene_loading(&s_scene);
        const bool offline = !s_transport_ready ||
            snapshot.link_idle_ms > PASSPORT_SERVICE_LINK_IDLE_DISCONNECT_MS;
        if (snapshot.stack_count) {
            size_t selected = snapshot.stack_selected < snapshot.stack_count
                ? snapshot.stack_selected : 0;
            const passport_tile_t *tile = &snapshot.stack[selected];
            const char *state = !snapshot.compose_confirmed ? "等待主机" :
                snapshot.compose_status == PASSPORT_COMPOSE_CONFLICT ? "组合冲突" :
                "组合就绪";
            snprintf(status, sizeof(status), "%zu 张 / %s", snapshot.stack_count,
                     offline ? "断线" : loading ? "读取中" : state);
            snprintf(detail, sizeof(detail), "%s %s %s", scene_role(tile->role),
                     tile->tile_id, tile->revision);
        } else if (s_scene.count) {
            bool enabled = snapshot.goal_mode_state == PASSPORT_GOAL_ENABLED &&
                strcmp(s_scene.ids[0], snapshot.goal_card_id) == 0;
            snprintf(status, sizeof(status), "%s",
                     offline ? "断线" : loading ? "读取卡片" :
                     enabled ? "卡片就绪" : "等待 IDE");
            snprintf(detail, sizeof(detail), "%s", s_scene.ids[0]);
        } else {
            snprintf(status, sizeof(status), "%s",
                     snapshot.task_state == PASSPORT_TASK_IDLE ? "贴卡唤醒伙伴" : model.task);
            snprintf(detail, sizeof(detail), "%s",
                     snapshot.task_state == PASSPORT_TASK_IDLE ? "NFC / 等待卡片" : snapshot.summary);
        }
        lv_label_set_text(s_scene_status, status);
        lv_label_set_text(s_scene_detail, detail);
        lv_obj_set_style_text_color(s_scene_status, lv_color_hex(
            snapshot.compose_confirmed && snapshot.compose_status == PASSPORT_COMPOSE_CONFLICT
                ? UI_RED : UI_INK), 0);
    }
    if (show_scene && !covered) {
        snprintf(model.title, sizeof(model.title), "%s",
                 snapshot.stack_count ? "我的卡组" : "像素伙伴");
        snprintf(model.hint, sizeof(model.hint), "%s",
                 snapshot.page == PASSPORT_PAGE_COMPOSE_STACK
                     ? "上 返回  下 选卡\n长按 确 说话"
                     : snapshot.stack_count ? "上 任务  下 卡组\n长按 确 说话"
                     : "上 任务\n长按 确 说话");
    }
    if (s_header_mode) {
        lv_label_set_text(s_header_mode,
                          s_v2.negotiated && s_v2.sid[0] ? "CODEX" : model.mode);
    }
    if (s_header_link) {
        lv_label_set_text(s_header_link,
                          s_v2.negotiated && s_v2.title[0]
                              ? s_v2.title : model.link);
    }
    refresh_battery_icon();
    if (s_title) lv_label_set_text(s_title, model.title);
    if (s_header) {
        lv_obj_set_style_bg_color(
            s_header,
            lv_color_hex(strcmp(model.mode, "组合") == 0 ? UI_SKY_DARK : UI_SKY),
            0);
    }
    if (s_header_mode) {
        lv_obj_set_style_bg_color(
            s_header_mode,
            lv_color_hex(strcmp(model.mode, "组合") == 0 ? UI_ORANGE : UI_YELLOW),
            0);
    }
    if (s_header_link_dot) {
        uint32_t link_color = 0x47C96B;
        if (strcmp(model.link, "未连接") == 0) link_color = UI_YELLOW;
        if (strcmp(model.link, "断线") == 0) link_color = UI_RED;
        lv_obj_set_style_bg_color(s_header_link_dot, lv_color_hex(link_color), 0);
    }

    for (size_t i = 0; i < PASSPORT_UI_MODEL_BODY_ROWS; i++) {
        if (!s_body_rows[i]) continue;
        if (!show_scene && i < model.body_rows) {
            lv_label_set_text(s_body_rows[i], model.body[i]);
            bool selected = (session_view ||
                             strcmp(model.mode, "组合") == 0) &&
                            model.body[i][0] == '>';
            bool primary = i == 0 && strcmp(model.mode, "随身") == 0;
            lv_obj_set_style_bg_opa(
                s_body_rows[i], selected || primary ? LV_OPA_COVER : LV_OPA_TRANSP,
                0);
            lv_obj_set_style_bg_color(
                s_body_rows[i],
                lv_color_hex(selected ? UI_YELLOW : UI_MUTED), 0);
            lv_obj_set_style_text_color(
                s_body_rows[i],
                lv_color_hex(primary ? UI_SKY_DARK : UI_INK), 0);
            lv_obj_clear_flag(s_body_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_body_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_approval_panel && s_approval_label) {
        if (model.approval_overlay) {
            if (s_approval_title) {
                const char *title = "需要确认";
                if (v2_approval) {
                    if (s_v2.view == PASSPORT_V2_VIEW_APPROVAL_DETAIL) {
                        title = "操作详情";
                    } else if (strcmp(s_v2.approval_operation, "edit") == 0) {
                        title = "允许修改？";
                    } else if (strcmp(s_v2.approval_operation, "command") == 0) {
                        title = "运行命令？";
                    }
                }
                lv_label_set_text(s_approval_title, title);
            }
            lv_label_set_text(s_approval_label, model.approval_summary);
            lv_obj_clear_flag(s_approval_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_approval_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_voice_panel && s_voice_label && s_voice_title) {
        if (model.voice_overlay && !model.approval_overlay) {
            const bool active = voice_view == PASSPORT_VOICE_FEEDBACK_ACTIVE;
            lv_label_set_text(s_voice_title, active ? "录音中" : "录音已停止");
            lv_label_set_text(s_voice_label, model.voice_line);
            lv_obj_set_style_bg_color(
                s_voice_panel,
                lv_color_hex(active ? 0xDDF5FF : 0xDDF4E2), 0);
            for (size_t i = 0; i < 10; i++) {
                int level = active ? (int)vst.level : 0;
                int bar_height = active && level > (int)i * 10
                    ? 8 + (int)(i % 4U) * 6
                    : 4;
                lv_obj_set_height(s_voice_bars[i], bar_height);
                lv_obj_set_y(s_voice_bars[i], 84 - bar_height);
                lv_obj_set_style_bg_color(
                    s_voice_bars[i],
                    lv_color_hex(active ? UI_SKY_DARK : 0x47A863), 0);
            }
            lv_obj_clear_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_hint) {
        lv_label_set_text(
            s_hint,
            model.voice_overlay && !model.approval_overlay
                ? (voice_view == PASSPORT_VOICE_FEEDBACK_ACTIVE
                       ? "松开 确 结束"
                       : "录音已停止")
                : model.hint);
    }
    if (s_hint_panel) {
        uint32_t hint_color = UI_INK;
        if (model.approval_overlay) hint_color = 0x7A2020;
        else if (model.voice_overlay) hint_color = UI_SKY_DARK;
        lv_obj_set_style_bg_color(s_hint_panel, lv_color_hex(hint_color), 0);
    }
}

static void apply_incoming_line(const char *line) {
    if (passport_v2_apply_line(&s_v2, line)) {
        s_service.state.link_idle_ms = 0;
        return;
    }
    if (s_v2.negotiated &&
        (strstr(line, "\"type\":\"session.") ||
         strstr(line, "\"type\":\"approval.") ||
         strstr(line, "\"type\":\"companion."))) {
        ESP_LOGW(TAG, "stale or malformed v2 host event rejected");
        return;
    }
    if (s_v2.negotiated &&
        (strstr(line, "\"type\":\"task.state\"") ||
         strstr(line, "\"type\":\"task.event\"")) &&
        !passport_v2_route_matches_line(&s_v2, line)) {
        ESP_LOGW(TAG, "unrouted v2 task event rejected");
        return;
    }
    if (passport_service_apply_line(&s_service, line) != PASSPORT_SERVICE_ACCEPTED) {
        ESP_LOGW(TAG, "host event rejected");
        if (s_transport_ready) {
            /* Give the host operator a bounded reason on the wire. The line is
             * truncated so the payload always fits inside PASSPORT_SERVICE_LINE_MAX.
             * We echo up to 96 bytes of the offending frame so the host can
             * localise the problem without inflating firmware bookkeeping. */
            char reject[PASSPORT_SERVICE_LINE_MAX];
            char echo[96];
            size_t echo_len = 0;
            while (echo_len < sizeof(echo) - 1 && line[echo_len] != '\0') {
                char ch = line[echo_len];
                echo[echo_len++] = (ch == '"' || ch == '\\') ? '?' : ch;
            }
            echo[echo_len] = '\0';
            int written = snprintf(reject, sizeof(reject),
                                   "{\"type\":\"protocol.reject\","
                                   "\"reason\":\"unparseable\","
                                   "\"echo\":\"%s\"}", echo);
            if (written > 0 && (size_t)written < sizeof(reject)) {
                (void)passport_transport_usb_send(reject);
            }
        }
    }
}

static void send_goal_mode_request(const passport_service_action_t *action) {
    char line[PASSPORT_SERVICE_LINE_MAX];
    int written = snprintf(line, sizeof(line),
                           "{\"type\":\"goal.mode.request\",\"mode\":\"goal\","
                           "\"card_id\":\"%s\"}", action->card_id);
    if (written > 0 && (size_t)written < sizeof(line)) {
        (void)passport_transport_usb_send(line);
    }
}

static void send_task_event_ack(const passport_service_action_t *action) {
    char line[PASSPORT_SERVICE_LINE_MAX];
    int written = snprintf(line, sizeof(line),
                           "{\"type\":\"task.event.ack\",\"event_id\":\"%s\"}",
                           action->event_id);
    if (written > 0 && (size_t)written < sizeof(line)) {
        (void)passport_transport_usb_send(line);
    }
}
static void send_pending_action_locked(void) {
    /* DTR-based "connected" detection is not reliable across host bridges;
     * gate on the driver being installed only. Frames are dropped harmlessly
     * on the USB side when there is nobody listening. */
    if (!s_transport_ready) return;
    passport_v2_action_t v2_action;
    if (passport_v2_take_action(&s_v2, &v2_action)) {
        static char v2_line[PASSPORT_SERVICE_LINE_MAX];
        int written = -1;
        switch (v2_action.type) {
        case PASSPORT_V2_ACTION_SESSION_LIST:
            written = snprintf(v2_line, sizeof(v2_line),
                               "{\"type\":\"session.list\",\"tx\":%lu,\"page\":%lu}",
                               (unsigned long)v2_action.tx,
                               (unsigned long)v2_action.page);
            break;
        case PASSPORT_V2_ACTION_SESSION_SELECT:
            written = snprintf(v2_line, sizeof(v2_line),
                               "{\"type\":\"session.select\",\"tx\":%lu,"
                               "\"sid\":\"%s\"}",
                               (unsigned long)v2_action.tx, v2_action.sid);
            break;
        case PASSPORT_V2_ACTION_SESSION_QUERY:
            written = snprintf(v2_line, sizeof(v2_line),
                               "{\"type\":\"session.query\",\"tx\":%lu}",
                               (unsigned long)s_v2.pending_tx);
            break;
        case PASSPORT_V2_ACTION_APPROVAL_DETAIL:
            written = snprintf(
                v2_line, sizeof(v2_line),
                "{\"type\":\"approval.detail\",\"bridge\":\"%s\","
                "\"sid\":\"%s\",\"epoch\":%lu,\"request_id\":\"%s\","
                "\"page\":%lu}",
                s_v2.bridge, s_v2.sid, (unsigned long)s_v2.epoch,
                v2_action.request_id, (unsigned long)v2_action.page);
            break;
        case PASSPORT_V2_ACTION_APPROVAL_DECISION:
            written = snprintf(
                v2_line, sizeof(v2_line),
                "{\"type\":\"approval.decision\",\"bridge\":\"%s\","
                "\"sid\":\"%s\",\"epoch\":%lu,\"request_id\":\"%s\","
                "\"decision\":\"%s\"}",
                s_v2.bridge, s_v2.sid, (unsigned long)s_v2.epoch,
                v2_action.request_id, v2_action.decision);
            break;
        case PASSPORT_V2_ACTION_COMPANION_ACK:
            written = snprintf(
                v2_line, sizeof(v2_line),
                "{\"type\":\"companion.ack\",\"asset\":\"%s\","
                "\"offset\":%lu,\"status\":\"%s\"}",
                v2_action.asset, (unsigned long)v2_action.offset,
                v2_action.status);
            break;
        case PASSPORT_V2_ACTION_NONE:
        default:
            break;
        }
        if (written > 0 && (size_t)written < sizeof(v2_line)) {
            (void)passport_transport_usb_send(v2_line);
        }
        return;
    }
    passport_service_action_t action;
    if (passport_service_take_action(&s_service, &action) != PASSPORT_SERVICE_ACTION_READY) {
        return;
    }
    switch (action.type) {
    case PASSPORT_ACTION_GOAL_MODE_REQUEST:
        send_goal_mode_request(&action);
        break;
    case PASSPORT_ACTION_TASK_EVENT_ACK:
        send_task_event_ack(&action);
        break;
    default:
        ESP_LOGW(TAG, "unexpected pending action=%d", action.type);
        break;
    }
}

static void service_tick(lv_timer_t *timer) {
    (void)timer;
    s_battery_elapsed_ms += 100;
    if (s_battery_elapsed_ms >= 1000) {
        s_battery_elapsed_ms = 0;
        s_battery_soc = bsp_battery_soc();
    }
    passport_service_tick(&s_service, 100);
    if (s_transport_ready) {
        s_hello_elapsed_ms += 100;
        /* DTR is unreliable, so repeat discovery at a low rate. This lets a
         * Bridge started after the firmware still negotiate protocol v2. */
        if (!s_hello_sent || s_hello_elapsed_ms >= 2000U) {
            (void)passport_transport_usb_send(
                "{\"type\":\"device.hello\",\"protocol\":2,"
                "\"device\":\"FoloPassport\"}");
            s_hello_sent = true;
            s_hello_elapsed_ms = 0;
        }
        send_pending_action_locked();
        /* Reuse a single scratch buffer instead of putting 512 B on the LVGL
         * timer stack every tick. Callers finish with the buffer before the
         * next iteration, so a shared static is safe here. */
        static char line[PASSPORT_SERVICE_LINE_MAX];
        while (passport_transport_usb_receive(line, sizeof(line)) == ESP_OK) {
            apply_incoming_line(line);
        }
    }
    refresh_ui_locked(100);
}

esp_err_t demo_passport_service_nfc_card(const char *card_id) {
    if (!card_id_is_normalized(card_id)) return ESP_ERR_INVALID_ARG;
    if (!bsp_lvgl_lock(100)) return ESP_ERR_TIMEOUT;
    passport_service_result_t result = passport_service_load_goal_card(&s_service, card_id);
    if (result == PASSPORT_SERVICE_ACCEPTED) refresh_ui_locked(0);
    bsp_lvgl_unlock();
    return result == PASSPORT_SERVICE_ACCEPTED ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static lv_obj_t *service_panel(lv_obj_t *parent, int x, int y, int w, int h,
                               uint32_t bg) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(panel, 3, 0);
    lv_obj_set_style_pad_all(panel, 7, 0);
    return panel;
}

void demo_passport_service_enter(void) {
    passport_service_init(&s_service);
    passport_v2_init(&s_v2, verify_sha256);
    passport_voice_feedback_init(&s_voice_feedback);
    memset(&s_scene, 0, sizeof(s_scene));
    s_companion_generation = 0;
    s_battery_elapsed_ms = 0;
    s_battery_soc = bsp_battery_soc();

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(UI_SKY), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    s_header = service_block(s_screen, 0, 0, 240, 43, UI_SKY);
    service_block(s_screen, 0, 40, 240, 3, UI_INK);
    s_header_mode = ui_pixel_label(s_header, "MODE", UI_TEXT_FONT, UI_INK);
    lv_obj_set_pos(s_header_mode, 9, 8);
    lv_obj_set_size(s_header_mode, 48, 26);
    lv_obj_set_style_bg_opa(s_header_mode, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_header_mode, lv_color_hex(UI_YELLOW), 0);
    lv_obj_set_style_border_color(s_header_mode, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(s_header_mode, 2, 0);
    lv_obj_set_style_pad_top(s_header_mode, 2, 0);
    lv_obj_set_style_text_align(s_header_mode, LV_TEXT_ALIGN_CENTER, 0);

    s_header_link_dot = service_block(s_header, 70, 17, 8, 8, 0x47C96B);
    lv_obj_set_style_border_color(s_header_link_dot, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(s_header_link_dot, 1, 0);
    s_header_link = ui_pixel_label(s_header, "LINK", UI_TEXT_FONT, 0xFFFFFF);
    lv_obj_set_pos(s_header_link, 83, 10);
    lv_obj_set_width(s_header_link, 100);
    lv_label_set_long_mode(s_header_link, LV_LABEL_LONG_CLIP);

    s_header_battery = service_block(s_header, 191, 14, 32, 15, UI_SKY);
    lv_obj_set_style_border_color(s_header_battery, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(s_header_battery, 2, 0);
    service_block(s_header, 223, 18, 3, 7, UI_INK);
    for (size_t i = 0; i < 4; i++) {
        s_battery_segments[i] = service_block(
            s_header_battery, 3 + (int)i * 7, 3, 5, 7, 0x47A863);
    }
    s_battery_unknown = ui_pixel_label(
        s_header_battery, "?", UI_TEXT_FONT, UI_INK);
    lv_obj_set_size(s_battery_unknown, 28, 14);
    lv_obj_set_style_text_align(s_battery_unknown, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_battery_unknown);

    service_block(s_screen, 12, 51, 220, 216, UI_INK);
    lv_obj_t *body_panel = service_panel(s_screen, 8, 47, 220, 216, UI_PAPER);
    lv_obj_set_style_pad_bottom(body_panel, 3, 0);
    s_title = ui_pixel_label(body_panel, "TITLE", UI_TEXT_FONT, UI_SKY_DARK);
    lv_obj_set_pos(s_title, 0, 0);
    lv_obj_set_width(s_title, 198);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_CLIP);
    service_block(body_panel, 0, 23, 34, 3, UI_ORANGE);
    service_block(body_panel, 34, 23, 164, 1, UI_MUTED);

    for (size_t i = 0; i < PASSPORT_UI_MODEL_BODY_ROWS; i++) {
        s_body_rows[i] = ui_pixel_label(body_panel, "", UI_TEXT_FONT, UI_INK);
        lv_obj_set_pos(s_body_rows[i], 0, 31 + (int)i * 26);
        lv_obj_set_size(s_body_rows[i], 198, 23);
        lv_obj_set_style_pad_left(s_body_rows[i], 5, 0);
        lv_obj_set_style_pad_top(s_body_rows[i], 1, 0);
        lv_label_set_long_mode(s_body_rows[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(s_body_rows[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_scene_obj = service_block(body_panel, 0, 26, PASSPORT_SCENE_WIDTH,
                                 PASSPORT_SCENE_HEIGHT, UI_PAPER);
    lv_obj_add_event_cb(s_scene_obj, scene_draw, LV_EVENT_DRAW_MAIN, NULL);
    s_scene_status = ui_pixel_label(s_scene_obj, "", UI_TEXT_FONT, UI_INK);
    lv_obj_set_pos(s_scene_status, 0, 140);
    lv_obj_set_size(s_scene_status, 198, 18);
    lv_label_set_long_mode(s_scene_status, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_scene_status, LV_TEXT_ALIGN_CENTER, 0);
    s_scene_detail = ui_pixel_label(s_scene_obj, "", UI_TEXT_FONT, UI_SKY_DARK);
    lv_obj_set_pos(s_scene_detail, 0, 158);
    lv_obj_set_size(s_scene_detail, 198, 16);
    lv_label_set_long_mode(s_scene_detail, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_scene_detail, LV_TEXT_ALIGN_CENTER, 0);

    s_approval_panel = service_panel(body_panel, 1, 34, 196, 146, UI_PAPER);
    service_block(s_approval_panel, 8, 33, 34, 42, UI_SKY);
    service_block(s_approval_panel, 13, 41, 24, 28, UI_INK);
    service_block(s_approval_panel, 17, 45, 16, 20, UI_PAPER);
    s_approval_title = ui_pixel_label(
        s_approval_panel, "需要确认", UI_TEXT_FONT, 0x7A2020);
    lv_obj_set_pos(s_approval_title, 0, 0);
    lv_obj_set_width(s_approval_title, 176);
    lv_obj_set_style_text_align(s_approval_title, LV_TEXT_ALIGN_CENTER, 0);
    s_approval_label = ui_pixel_label(s_approval_panel, "", UI_TEXT_FONT, UI_INK);
    lv_obj_set_pos(s_approval_label, 49, 34);
    lv_obj_set_width(s_approval_label, 127);
    lv_label_set_long_mode(s_approval_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_approval_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_approval_panel, LV_OBJ_FLAG_HIDDEN);

    s_voice_panel = service_panel(body_panel, 1, 34, 196, 146, 0xDDF5FF);
    s_voice_title = ui_pixel_label(s_voice_panel, "录音中", UI_TEXT_FONT, UI_SKY_DARK);
    lv_obj_set_pos(s_voice_title, 0, 0);
    lv_obj_set_width(s_voice_title, 176);
    lv_obj_set_style_text_align(s_voice_title, LV_TEXT_ALIGN_CENTER, 0);
    for (size_t i = 0; i < 10; i++) {
        s_voice_bars[i] = service_block(
            s_voice_panel, 10 + (int)i * 16, 80, 9, 4, UI_SKY_DARK);
    }
    s_voice_label = ui_pixel_label(s_voice_panel, "", UI_TEXT_FONT, UI_INK);
    lv_obj_set_pos(s_voice_label, 0, 92);
    lv_obj_set_width(s_voice_label, 176);
    lv_label_set_long_mode(s_voice_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_voice_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(s_voice_panel, LV_OBJ_FLAG_HIDDEN);

    service_block(s_screen, 12, 272, 220, 48, UI_SKY_DARK);
    s_hint_panel = service_panel(s_screen, 8, 268, 220, 48, UI_INK);
    lv_obj_set_style_pad_all(s_hint_panel, 3, 0);
    s_hint = ui_pixel_label(s_hint_panel, "HINT", UI_TEXT_FONT, 0xFFFFFF);
    lv_obj_set_width(s_hint, 206);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_hint);

    refresh_ui_locked(0);
    lv_screen_load(s_screen);
}

void demo_passport_service_exit(void) {
    if (s_service_timer) {
        lv_timer_delete(s_service_timer);
        s_service_timer = NULL;
    }
    if (s_screen) {
        lv_obj_delete(s_screen);
        s_screen = NULL;
        s_header = NULL;
        s_header_mode = NULL;
        s_header_link = NULL;
        s_header_link_dot = NULL;
        s_header_battery = NULL;
        for (size_t i = 0; i < 4; i++) s_battery_segments[i] = NULL;
        s_battery_unknown = NULL;
        s_title = NULL;
        s_scene_obj = NULL;
        s_scene_status = NULL;
        s_scene_detail = NULL;
        for (size_t i = 0; i < PASSPORT_UI_MODEL_BODY_ROWS; i++) s_body_rows[i] = NULL;
        s_approval_panel = NULL;
        s_approval_title = NULL;
        s_approval_label = NULL;
        s_voice_panel = NULL;
        s_voice_title = NULL;
        s_voice_label = NULL;
        for (size_t i = 0; i < 10; i++) s_voice_bars[i] = NULL;
        s_hint_panel = NULL;
        s_hint = NULL;
    }
}

esp_err_t demo_passport_service_start(void) {
    s_hello_sent = false;
    s_hello_elapsed_ms = 0;
    if (s_service_timer) return ESP_ERR_INVALID_STATE;
    esp_err_t transport_err = passport_transport_usb_start();
    s_transport_ready = transport_err == ESP_OK;
    if (transport_err != ESP_OK) {
        ESP_LOGW(TAG, "USB transport unavailable: %s; waiting for a real host transport",
                 esp_err_to_name(transport_err));
    }
    if (!bsp_lvgl_lock(1000)) {
        if (s_transport_ready) (void)passport_transport_usb_stop();
        s_transport_ready = false;
        return ESP_ERR_TIMEOUT;
    }
    s_service_timer = lv_timer_create(service_tick, 100, NULL);
    bsp_lvgl_unlock();
    if (!s_service_timer) {
        if (s_transport_ready) (void)passport_transport_usb_stop();
        s_transport_ready = false;
        return ESP_ERR_NO_MEM;
    }
    /* Slice D voice worker starts after the timer is live so demo teardown
     * only has to check s_service_timer to know worker resources exist. */
    (void)passport_voice_worker_start();
    return ESP_OK;
}

esp_err_t demo_passport_service_stop(void) {
    /* Even if we cannot obtain the LVGL lock (e.g. the timer task is stuck),
     * the transport must still stop; otherwise the next _start() call finds
     * s_service_timer true and returns INVALID_STATE, leaving the demo shell
     * with no host link. Track the lock-failure path separately so callers
     * see the degraded teardown without silently losing state. Voice worker
     * must exit before the transport tears down to avoid a race where the
     * worker writes to a torn-down USB Serial/JTAG driver. */
    (void)passport_voice_worker_stop();
    esp_err_t lock_err = ESP_OK;
    if (s_service_timer) {
        if (bsp_lvgl_lock(1000)) {
            lv_timer_delete(s_service_timer);
            s_service_timer = NULL;
            bsp_lvgl_unlock();
        } else {
            ESP_LOGE(TAG, "LVGL lock timeout during stop; leaking timer object "
                          "but continuing transport teardown");
            lock_err = ESP_ERR_TIMEOUT;
        }
    }

    esp_err_t transport_err = passport_transport_usb_stop();
    s_transport_ready = false;
    if (lock_err != ESP_OK) return lock_err;
    return transport_err;
}

static void handle_voice_gesture(void) {
    /* Slice D: OK long-press starts capture. The worker runs on its own
     * FreeRTOS task; releasing OK requests a manual stop. */
    if (!s_transport_ready ||
        s_v2.view == PASSPORT_V2_VIEW_SESSIONS ||
        s_v2.view == PASSPORT_V2_VIEW_SWITCHING ||
        s_v2.view == PASSPORT_V2_VIEW_APPROVAL ||
        s_v2.view == PASSPORT_V2_VIEW_APPROVAL_DETAIL) {
        return;
    }
    if (s_v2.negotiated) {
        if (!s_v2.writable || !s_v2.sid[0]) return;
        passport_voice_worker_set_route(s_v2.bridge, s_v2.sid, s_v2.epoch);
    } else {
        passport_voice_worker_set_route(NULL, NULL, 0);
    }
    esp_err_t rc = passport_voice_worker_begin();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "voice worker start failed: %s", esp_err_to_name(rc));
    }
}

static void handle_voice_release(void) {
    (void)passport_voice_worker_manual_stop();
}

static void handle_button_locked(bsp_btn_t btn, bsp_btn_ev_t ev) {
    /* OK long-press starts the voice worker and OK release stops it. */
    if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_OK) {
            handle_voice_gesture();
        } else if (btn == BSP_BTN_UP) {
            passport_voice_status_t voice;
            passport_voice_worker_snapshot(&voice);
            if (!voice.active) (void)passport_v2_open_sessions(&s_v2);
        } else {
            ESP_LOGI(TAG, "long-press ignored");
        }
        return;
    }
    if (ev == BSP_BTN_RELEASE) {
        if (btn == BSP_BTN_OK) handle_voice_release();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    passport_button_t button;
    switch (btn) {
    case BSP_BTN_UP: button = PASSPORT_BUTTON_UP; break;
    case BSP_BTN_DOWN: button = PASSPORT_BUTTON_DOWN; break;
    case BSP_BTN_OK: button = PASSPORT_BUTTON_OK; break;
    default: return;
    }

    if (s_v2.view == PASSPORT_V2_VIEW_APPROVAL ||
        s_v2.view == PASSPORT_V2_VIEW_APPROVAL_DETAIL ||
        s_v2.view == PASSPORT_V2_VIEW_SESSIONS) {
        (void)passport_v2_button(&s_v2, (int)button);
        return;
    }
    if (s_v2.view == PASSPORT_V2_VIEW_SWITCHING) return;

    /* Pull only the two flags this handler needs to keep the button-callback
     * stack (bsp_btn worker) well below 2 KB. */
    const bool approval_pending = s_service.state.approval_pending;
    const passport_page_t page = s_service.state.page;

    if (approval_pending && (button == PASSPORT_BUTTON_OK ||
                             button == PASSPORT_BUTTON_UP)) {
        if (passport_service_button(&s_service, button) == PASSPORT_SERVICE_ACCEPTED) {
            passport_service_action_t action;
            if (passport_service_take_action(&s_service, &action) ==
                PASSPORT_SERVICE_ACTION_READY && s_transport_ready) {
                char line[PASSPORT_SERVICE_LINE_MAX];
                int written = snprintf(line, sizeof(line),
                                       "{\"type\":\"approval.decision\","
                                       "\"request_id\":\"%s\",\"decision\":\"%s\"}",
                                       action.request_id,
                                       action.decision == PASSPORT_APPROVAL_APPROVE
                                           ? "approve" : "reject");
                if (written > 0 && (size_t)written < sizeof(line)) {
                    (void)passport_transport_usb_send(line);
                }
            }
        }
        return;
    }

    if (page == PASSPORT_PAGE_WEAR_TASK && button == PASSPORT_BUTTON_OK) {
        (void)passport_service_ack_top_event(&s_service);
        return;
    }

    (void)passport_service_navigate(&s_service, button);
}

void demo_passport_service_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!bsp_lvgl_lock(100)) return;
    handle_button_locked(btn, ev);
    refresh_ui_locked(0);
    bsp_lvgl_unlock();
}
