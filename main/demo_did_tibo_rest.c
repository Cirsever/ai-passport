#include "demo_did_tibo_rest.h"
#include "did_tibo_rest.h"
#include "passport_transport_usb.h"
#include "passport_line.h"
#include "bsp_audio.h"
#include "bsp_display.h"
#include "ui_pixel.h"
#include "lvgl.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <stdio.h>

static tibo_rest_t s_rest; static lv_obj_t *s_screen, *s_status; static lv_timer_t *s_timer; static TaskHandle_t s_beep_task;
static const char *TAG = "demo_tibo_rest";
static void refresh(void) { tibo_rest_snapshot_t x; tibo_rest_snapshot(&s_rest, &x); if (s_status) lv_label_set_text_fmt(s_status, "TIBO PUSH DEMO\n%s\n%s\n\nSTATE %s  REMIND %lu\nCODEX LEVEL %u\n\nUP: MUTE\nDOWN: LEVEL", x.title[0] ? x.title : "waiting for USB", x.body, x.state == TIBO_REST_PENDING ? "PENDING" : x.state == TIBO_REST_MUTED ? "MUTED" : "IDLE", (unsigned long)x.reminder_count, (unsigned)x.level); }
static void beep_task(void *arg) { (void)arg; int16_t samples[320]; for (;;) { ulTaskNotifyTake(pdTRUE, portMAX_DELAY); if (bsp_audio_set_format(16000, 16, 1) != ESP_OK) continue; for (int i=0;i<320;i++) samples[i] = (i % 16 < 8) ? 5000 : -5000; (void)bsp_audio_write(samples, sizeof(samples)); } }
static void tick(lv_timer_t *timer) { (void)timer; char line[PASSPORT_LINE_MAX + 1]; while (passport_transport_usb_receive(line, sizeof(line)) == ESP_OK) { if (tibo_rest_apply_push(&s_rest, line, (uint32_t)lv_tick_get())) { ESP_LOGI(TAG, "received tibo.push"); (void)passport_transport_usb_send("{\"type\":\"tibo.received\"}"); } else ESP_LOGW(TAG, "ignored non-Tibo USB line"); } tibo_rest_tick(&s_rest, (uint32_t)lv_tick_get()); if (tibo_rest_take_beep(&s_rest) && s_beep_task) { ESP_LOGI(TAG, "beep requested"); xTaskNotifyGive(s_beep_task); } refresh(); }
void demo_did_tibo_rest_enter(void) { tibo_rest_init(&s_rest, 1, 30000); s_screen=ui_pixel_screen_create("DID TIBO REST"); lv_obj_t *p=ui_pixel_panel_create(s_screen,13,52,214,190,UI_PAPER); s_status=lv_label_create(p); lv_obj_set_width(s_status,190); lv_obj_set_style_text_align(s_status,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(s_status); ui_pixel_mascot_create(s_screen,101,266); refresh(); lv_screen_load(s_screen); }
void demo_did_tibo_rest_exit(void) { if(s_timer){lv_timer_delete(s_timer);s_timer=NULL;} if(s_screen){lv_obj_delete(s_screen);s_screen=NULL;s_status=NULL;} }
esp_err_t demo_did_tibo_rest_start(void) { if(passport_transport_usb_start()!=ESP_OK)return ESP_ERR_NOT_FOUND; if(xTaskCreate(beep_task,"tibo_beep",3072,NULL,4,&s_beep_task)!=pdPASS)return ESP_ERR_NO_MEM; if(!bsp_lvgl_lock(500))return ESP_ERR_TIMEOUT; s_timer=lv_timer_create(tick,250,NULL); bsp_lvgl_unlock(); ESP_LOGI(TAG, "DidTiboRest started; reminder interval=30000ms"); return s_timer?ESP_OK:ESP_ERR_NO_MEM; }
esp_err_t demo_did_tibo_rest_stop(void) { if(s_timer){if(!bsp_lvgl_lock(500))return ESP_ERR_TIMEOUT;lv_timer_delete(s_timer);s_timer=NULL;bsp_lvgl_unlock();} if(s_beep_task){vTaskDelete(s_beep_task);s_beep_task=NULL;} return passport_transport_usb_stop(); }
void demo_did_tibo_rest_key(bsp_btn_t btn,bsp_btn_ev_t ev) { if(ev!=BSP_BTN_CLICK)return; if(btn==BSP_BTN_UP){tibo_rest_mute(&s_rest); ESP_LOGI(TAG, "muted");} else if(btn==BSP_BTN_DOWN){tibo_rest_cycle_level(&s_rest); tibo_rest_snapshot_t snapshot; tibo_rest_snapshot(&s_rest, &snapshot); ESP_LOGI(TAG, "level=%u", (unsigned)snapshot.level);} else return; if(bsp_lvgl_lock(100)){refresh();bsp_lvgl_unlock();} }
