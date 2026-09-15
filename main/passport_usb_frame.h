#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "passport_line.h"

#define PASSPORT_USB_FRAME_PREFIX "@passport "
#define PASSPORT_USB_FRAME_MAX (PASSPORT_LINE_MAX + sizeof(PASSPORT_USB_FRAME_PREFIX))

bool passport_usb_frame_encode(const char *line, char *framed, size_t capacity);
bool passport_usb_frame_decode(const char *framed, char *line, size_t capacity);
