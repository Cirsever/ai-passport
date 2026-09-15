#include <assert.h>
#include <string.h>

#include "passport_usb_frame.h"

int main(void) {
    const char *json = "{\"type\":\"device.hello\",\"protocol\":1}";
    char framed[PASSPORT_USB_FRAME_MAX];
    char decoded[PASSPORT_USB_FRAME_MAX];

    assert(passport_usb_frame_encode(json, framed, sizeof(framed)));
    assert(strcmp(framed, "@passport {\"type\":\"device.hello\",\"protocol\":1}") == 0);
    assert(passport_usb_frame_decode(framed, decoded, sizeof(decoded)));
    assert(strcmp(decoded, json) == 0);
    assert(!passport_usb_frame_decode("I (123) boot: ready", decoded, sizeof(decoded)));
    return 0;
}
