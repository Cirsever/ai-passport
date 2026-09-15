#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>

#define PASSPORT_TCP_PORT 4242
#define PASSPORT_TCP_SSID "Passport-MVP"
#define PASSPORT_TCP_PASSWORD "passport-mvp"

esp_err_t passport_transport_tcp_start(void);
esp_err_t passport_transport_tcp_stop(void);
esp_err_t passport_transport_tcp_send(const char *line);
esp_err_t passport_transport_tcp_receive(char *line, size_t capacity);
bool passport_transport_tcp_connected(void);

