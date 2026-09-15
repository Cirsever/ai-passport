#pragma once

#include "esp_err.h"

#include <stddef.h>

esp_err_t passport_transport_usb_start(void);
esp_err_t passport_transport_usb_stop(void);
esp_err_t passport_transport_usb_send(const char *line);
esp_err_t passport_transport_usb_receive(char *line, size_t capacity);
