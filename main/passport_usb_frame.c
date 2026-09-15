#include "passport_usb_frame.h"

#include <string.h>

bool passport_usb_frame_encode(const char *line, char *framed, size_t capacity) {
    if (!line || !framed) return false;
    size_t line_length = strlen(line);
    size_t prefix_length = strlen(PASSPORT_USB_FRAME_PREFIX);
    if (line_length >= PASSPORT_LINE_MAX || prefix_length + line_length + 1 > capacity) {
        return false;
    }
    memcpy(framed, PASSPORT_USB_FRAME_PREFIX, prefix_length);
    memcpy(framed + prefix_length, line, line_length + 1);
    return true;
}

bool passport_usb_frame_decode(const char *framed, char *line, size_t capacity) {
    if (!framed || !line) return false;
    size_t prefix_length = strlen(PASSPORT_USB_FRAME_PREFIX);
    if (strncmp(framed, PASSPORT_USB_FRAME_PREFIX, prefix_length) != 0) return false;
    size_t line_length = strlen(framed + prefix_length);
    if (line_length >= PASSPORT_LINE_MAX || line_length + 1 > capacity) return false;
    memcpy(line, framed + prefix_length, line_length + 1);
    return true;
}
