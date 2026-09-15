#include "passport_line.h"

#include <string.h>

void passport_line_init(passport_line_buffer_t *buffer) {
    memset(buffer, 0, sizeof(*buffer));
}

passport_line_result_t passport_line_push(passport_line_buffer_t *buffer,
                                           const uint8_t *bytes, size_t length,
                                           passport_line_callback_t callback,
                                           void *context) {
    if (!buffer || (!bytes && length > 0) || !callback) return PASSPORT_LINE_ABORTED;
    passport_line_result_t result = PASSPORT_LINE_OK;

    for (size_t i = 0; i < length; i++) {
        const char byte = (char)bytes[i];
        if (buffer->discarding) {
            if (byte == '\n') {
                buffer->discarding = false;
                buffer->length = 0;
            }
            result = PASSPORT_LINE_OVERFLOW;
            continue;
        }
        if (byte == '\n') {
            size_t line_length = buffer->length;
            if (line_length > 0 && buffer->data[line_length - 1] == '\r') line_length--;
            buffer->data[line_length] = '\0';
            if (!callback(buffer->data, line_length, context)) {
                passport_line_init(buffer);
                return PASSPORT_LINE_ABORTED;
            }
            buffer->length = 0;
            continue;
        }
        if (buffer->length >= PASSPORT_LINE_MAX) {
            buffer->length = 0;
            buffer->discarding = true;
            result = PASSPORT_LINE_OVERFLOW;
            continue;
        }
        buffer->data[buffer->length++] = byte;
    }
    return result;
}
