#pragma once

#include "esp_err.h"

#include <stddef.h>

/* Reference-counted USB Serial/JTAG transport. Multiple demos (Passport
 * Service, DidTiboRest) may sit on the same underlying driver; every _start()
 * must be paired with a _stop(). The driver is installed on the first start
 * and uninstalled only when the last user stops. */
esp_err_t passport_transport_usb_start(void);
esp_err_t passport_transport_usb_stop(void);
esp_err_t passport_transport_usb_send(const char *line);
esp_err_t passport_transport_usb_receive(char *line, size_t capacity);
