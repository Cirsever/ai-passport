#pragma once

#include "bsp_button.h"
#include "esp_err.h"

void demo_passport_service_enter(void);
void demo_passport_service_exit(void);
void demo_passport_service_key(bsp_btn_t btn, bsp_btn_ev_t ev);
esp_err_t demo_passport_service_start(void);
esp_err_t demo_passport_service_stop(void);
/* Called by a real NFC reader or phone-relay adapter after card normalization. */
esp_err_t demo_passport_service_nfc_card(const char *card_id);
