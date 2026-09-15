#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PASSPORT_LINE_MAX 512U

typedef enum {
    PASSPORT_LINE_OK = 0,
    PASSPORT_LINE_OVERFLOW,
    PASSPORT_LINE_ABORTED,
} passport_line_result_t;

typedef bool (*passport_line_callback_t)(const char *line, size_t length, void *context);

typedef struct {
    char data[PASSPORT_LINE_MAX + 1];
    size_t length;
    bool discarding;
} passport_line_buffer_t;

void passport_line_init(passport_line_buffer_t *buffer);
passport_line_result_t passport_line_push(passport_line_buffer_t *buffer,
                                           const uint8_t *bytes, size_t length,
                                           passport_line_callback_t callback,
                                           void *context);

