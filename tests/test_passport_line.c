#include <assert.h>
#include <string.h>

#include "passport_line.h"

typedef struct {
    unsigned count;
    char last[PASSPORT_LINE_MAX + 1];
} capture_t;

static bool capture_line(const char *line, size_t length, void *context) {
    capture_t *capture = context;
    assert(length <= PASSPORT_LINE_MAX);
    memcpy(capture->last, line, length);
    capture->last[length] = '\0';
    capture->count++;
    return true;
}

static void test_partial_lines_are_reassembled(void) {
    passport_line_buffer_t buffer;
    capture_t capture = {0};
    passport_line_init(&buffer);

    assert(passport_line_push(&buffer, (const uint8_t *)"one\n", 4,
                              capture_line, &capture) == PASSPORT_LINE_OK);
    assert(passport_line_push(&buffer, (const uint8_t *)"two", 3,
                              capture_line, &capture) == PASSPORT_LINE_OK);
    assert(passport_line_push(&buffer, (const uint8_t *)"\r\n", 2,
                              capture_line, &capture) == PASSPORT_LINE_OK);
    assert(capture.count == 2);
    assert(strcmp(capture.last, "two") == 0);
}

static void test_oversized_line_is_discarded_until_newline(void) {
    passport_line_buffer_t buffer;
    capture_t capture = {0};
    char oversized[PASSPORT_LINE_MAX + 2];
    memset(oversized, 'x', sizeof(oversized));
    passport_line_init(&buffer);

    assert(passport_line_push(&buffer, (const uint8_t *)oversized, sizeof(oversized),
                              capture_line, &capture) == PASSPORT_LINE_OVERFLOW);
    assert(capture.count == 0);
    assert(passport_line_push(&buffer, (const uint8_t *)"\nnext\n", 6,
                              capture_line, &capture) == PASSPORT_LINE_OVERFLOW);
    assert(capture.count == 1);
    assert(strcmp(capture.last, "next") == 0);
}

int main(void) {
    test_partial_lines_are_reassembled();
    test_oversized_line_is_discarded_until_newline();
    return 0;
}
