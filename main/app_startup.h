#pragma once

typedef enum {
    APP_STARTUP_MENU = 0,
    APP_STARTUP_PASSPORT_SERVICE,
    APP_STARTUP_DID_TIBO_REST,
} app_startup_demo_t;

app_startup_demo_t app_startup_initial_demo(void);
