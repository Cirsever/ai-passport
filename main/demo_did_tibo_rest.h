#pragma once
#include "bsp_button.h"
#include "esp_err.h"
void demo_did_tibo_rest_enter(void); void demo_did_tibo_rest_exit(void);
void demo_did_tibo_rest_key(bsp_btn_t, bsp_btn_ev_t);
esp_err_t demo_did_tibo_rest_start(void); esp_err_t demo_did_tibo_rest_stop(void);
