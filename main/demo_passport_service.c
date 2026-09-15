#include "demo_passport_service.h"

#include "bsp_display.h"
#include "bsp_battery.h"
#include "passport_service.h"
#include "passport_ui_model.h"
#include "passport_transport_usb.h"
#include "ui_pixel.h"
#include "ui_cn_16.h"

#include "esp_log.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

static const char *TAG = "demo_passport_service";

#define UI_TEXT_FONT (&ui_cn_16)

static passport_service_t s_service;
static lv_obj_t *s_screen;
static lv_obj_t *s_header_mode;
static lv_obj_t *s_header_link;
static lv_obj_t *s_header_battery;
static lv_obj_t *s_title;
static lv_obj_t *s_body_rows[PASSPORT_UI_MODEL_BODY_ROWS];
static lv_obj_t *s_approval_panel;
static lv_obj_t *s_approval_label;
static lv_obj_t *s_disconnect_panel;
static lv_obj_t *s_disconnect_label;
static lv_obj_t *s_hint;
static lv_timer_t *s_service_timer;
static unsigned s_battery_elapsed_ms;
static int s_battery_soc = -1;
static bool s_transport_ready;
static bool s_hello_sent;

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

static void refresh_ui_locked(void) {
    /* Both structs are large (snapshot ~2 KB, model ~700 B). Keep them off the
     * caller's stack so LVGL and button callbacks running on the main task's
     * 4 KB stack do not trip stack-protection. */
    static passport_service_snapshot_t snapshot;
    static passport_ui_model_t model;
    passport_service_snapshot(&s_service, &snapshot);
    passport_ui_model_build(&snapshot, s_transport_ready,
                            s_battery_soc, &model);

    if (s_header_mode) lv_label_set_text(s_header_mode, model.mode);
    if (s_header_link) lv_label_set_text(s_header_link, model.link);
    if (s_header_battery) lv_label_set_text(s_header_battery, model.battery);
    if (s_title) lv_label_set_text(s_title, model.title);

    for (size_t i = 0; i < PASSPORT_UI_MODEL_BODY_ROWS; i++) {
        if (!s_body_rows[i]) continue;
        if (i < model.body_rows) {
            lv_label_set_text(s_body_rows[i], model.body[i]);
            lv_obj_clear_flag(s_body_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_body_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_approval_panel && s_approval_label) {
        if (model.approval_overlay) {
            lv_label_set_text(s_approval_label, model.approval_summary);
            lv_obj_clear_flag(s_approval_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_approval_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_disconnect_panel && s_disconnect_label) {
        if (model.disconnected_banner[0]) {
            lv_label_set_text(s_disconnect_label, model.disconnected_banner);
            lv_obj_clear_flag(s_disconnect_panel, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_disconnect_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_hint) lv_label_set_text(s_hint, model.hint);
}

static void apply_incoming_line(const char *line) {
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
        /* Send the hello once per driver session; DTR is unreliable across
         * host bridges, so drop the connected() gate and rely on RX traffic
         * (link_idle_ms) to determine liveness. */
        if (!s_hello_sent) {
            (void)passport_transport_usb_send(
                "{\"type\":\"device.hello\",\"protocol\":1,"
                "\"device\":\"FoloPassport\"}");
            s_hello_sent = true;
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
    refresh_ui_locked();
}

esp_err_t demo_passport_service_nfc_card(const char *card_id) {
    if (!card_id_is_normalized(card_id)) return ESP_ERR_INVALID_ARG;
    if (!bsp_lvgl_lock(100)) return ESP_ERR_TIMEOUT;
    passport_service_result_t result = passport_service_load_goal_card(&s_service, card_id);
    if (result == PASSPORT_SERVICE_ACCEPTED) refresh_ui_locked();
    bsp_lvgl_unlock();
    return result == PASSPORT_SERVICE_ACCEPTED ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static lv_obj_t *service_panel(lv_obj_t *parent, int x, int y, int w, int h,
                               uint32_t bg) {
    /* Flat panel without the pixel-art drop shadow that ui_pixel_panel_create()
     * paints. The three service panels sit flush against each other, so a
     * shadow underneath the header would land on top of the body content and
     * eat the labels. */
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(bg), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_pad_all(panel, 4, 0);
    return panel;
}

void demo_passport_service_enter(void) {
    passport_service_init(&s_service);
    s_battery_elapsed_ms = 0;
    s_battery_soc = bsp_battery_soc();

    /* Slice F uses a plain paper-white background so the three info panels
     * are the only elements on the display. The pixel-art screen decorations
     * (cloud, grass, drop-shadow title plate) belong to the demo menu and
     * were clipping into the header. */
    s_screen = lv_obj_create(NULL);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(UI_PAPER), 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    lv_obj_t *header = service_panel(s_screen, 10, 12, 220, 26, UI_PAPER);
    lv_obj_set_style_pad_all(header, 4, 0);
    s_header_mode = ui_pixel_label(header, "MODE", UI_TEXT_FONT, UI_INK);
    lv_obj_set_pos(s_header_mode, 0, 0);
    s_header_link = ui_pixel_label(header, "LINK", UI_TEXT_FONT, UI_INK);
    lv_obj_set_pos(s_header_link, 60, 0);
    s_header_battery = ui_pixel_label(header, "BAT --", UI_TEXT_FONT, UI_INK);
    lv_obj_set_pos(s_header_battery, 140, 0);

    lv_obj_t *body = service_panel(s_screen, 10, 46, 220, 216, UI_PAPER);
    s_title = ui_pixel_label(body, "TITLE", UI_TEXT_FONT, UI_INK);
    lv_obj_set_pos(s_title, 0, 0);
    lv_obj_set_width(s_title, 208);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_CLIP);
    for (size_t i = 0; i < PASSPORT_UI_MODEL_BODY_ROWS; i++) {
        s_body_rows[i] = ui_pixel_label(body, "", UI_TEXT_FONT, UI_INK);
        lv_obj_set_pos(s_body_rows[i], 0, 22 + (int)i * 20);
        lv_obj_set_width(s_body_rows[i], 208);
        lv_label_set_long_mode(s_body_rows[i], LV_LABEL_LONG_CLIP);
        lv_obj_add_flag(s_body_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_approval_panel = service_panel(body, 4, 48, 200, 128, UI_YELLOW);
    s_approval_label = ui_pixel_label(s_approval_panel, "", UI_TEXT_FONT, UI_INK);
    lv_obj_set_width(s_approval_label, 188);
    lv_label_set_long_mode(s_approval_label, LV_LABEL_LONG_WRAP);
    lv_obj_center(s_approval_label);
    lv_obj_add_flag(s_approval_panel, LV_OBJ_FLAG_HIDDEN);

    /* Full-body overlay for the disconnected banner. Painted on top of the
     * task/compose rows so a stale link does not misrepresent live state. */
    s_disconnect_panel = service_panel(body, 4, 4, 200, 200, UI_MUTED);
    s_disconnect_label = ui_pixel_label(s_disconnect_panel, "", UI_TEXT_FONT, UI_INK);
    lv_obj_set_width(s_disconnect_label, 188);
    lv_label_set_long_mode(s_disconnect_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_disconnect_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_disconnect_label);
    lv_obj_add_flag(s_disconnect_panel, LV_OBJ_FLAG_HIDDEN);

    /* Hint has to accommodate two 18 px lines when a Chinese cue wraps
     * (approval / compose pages emit hints wider than 208 px). Give it
     * 46 px so the wrapped second line is not clipped. */
    lv_obj_t *hint_panel = service_panel(s_screen, 10, 270, 220, 42, UI_MUTED);
    lv_obj_set_style_pad_all(hint_panel, 3, 0);
    s_hint = ui_pixel_label(hint_panel, "HINT", UI_TEXT_FONT, UI_INK);
    lv_obj_set_width(s_hint, 208);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_hint);

    refresh_ui_locked();
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
        s_header_mode = NULL;
        s_header_link = NULL;
        s_header_battery = NULL;
        s_title = NULL;
        for (size_t i = 0; i < PASSPORT_UI_MODEL_BODY_ROWS; i++) s_body_rows[i] = NULL;
        s_approval_panel = NULL;
        s_approval_label = NULL;
        s_disconnect_panel = NULL;
        s_disconnect_label = NULL;
        s_hint = NULL;
    }
}

esp_err_t demo_passport_service_start(void) {
    s_hello_sent = false;
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
    return ESP_OK;
}

esp_err_t demo_passport_service_stop(void) {
    if (s_service_timer) {
        if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
        lv_timer_delete(s_service_timer);
        s_service_timer = NULL;
        bsp_lvgl_unlock();
    }

    esp_err_t transport_err = passport_transport_usb_stop();
    s_transport_ready = false;
    return transport_err;
}

static uint32_t s_voice_capture_seq;

static void send_voice_capture_burst(void) {
    if (!s_transport_ready) return;
    /* MVP stub: no audio worker yet (Slice D). Emit the exact three-frame
     * envelope the Codex adapter expects so host-side integration can be
     * exercised without waiting for the audio pipeline. Real PCM data lands
     * with Slice D + P0-5; keep the frames bounded and self-terminating. */
    char line[PASSPORT_SERVICE_LINE_MAX];
    uint32_t seq = ++s_voice_capture_seq;
    int written = snprintf(line, sizeof(line),
                           "{\"type\":\"voice.capture.start\","
                           "\"request_id\":\"v-%u\","
                           "\"sample_rate\":16000,\"codec\":\"pcm16\"}",
                           (unsigned)seq);
    if (written > 0 && (size_t)written < sizeof(line)) {
        (void)passport_transport_usb_send(line);
    }
    written = snprintf(line, sizeof(line),
                       "{\"type\":\"voice.capture.stop\","
                       "\"request_id\":\"v-%u\","
                       "\"duration_ms\":0,"
                       "\"reason\":\"stub\","
                       "\"text\":\"OK long-press stub utterance\"}",
                       (unsigned)seq);
    if (written > 0 && (size_t)written < sizeof(line)) {
        (void)passport_transport_usb_send(line);
    }
}

static void handle_button_locked(bsp_btn_t btn, bsp_btn_ev_t ev) {
    /* OK long-press is reserved for voice / Codex-level actions in the
     * architecture doc. The real audio worker is Slice D; until then we
     * emit a bounded voice.capture.* burst so the host adapter can be
     * exercised end-to-end and the demo shell never gets to treat this
     * gesture as "exit demo". */
    if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_OK) {
            send_voice_capture_burst();
        } else {
            ESP_LOGI(TAG, "long-press ignored (only OK long-press is wired)");
        }
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
    refresh_ui_locked();
    bsp_lvgl_unlock();
}
