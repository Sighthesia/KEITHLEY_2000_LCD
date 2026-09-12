#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Raw canvas text is drawn in bounded token blocks. UTF-8 symbols count as one
 * token, so a block boundary can never split a legal multibyte symbol. */
#define RAW_READING_PROGRESS_BLOCK_TOKENS 8u

typedef struct {
    const char *line;
    uint8_t length;
    uint8_t offset;
    uint8_t column;
    uint8_t block_tokens;
} raw_reading_progress_t;

void raw_reading_progress_init(raw_reading_progress_t *progress,
                               const char *line, uint8_t length,
                               uint8_t block_tokens);
bool raw_reading_progress_current(const raw_reading_progress_t *progress,
                                  uint8_t *start, uint8_t *length);
void raw_reading_progress_advance(raw_reading_progress_t *progress);

/* Returns one for ASCII/invalid bytes and the complete length for a legal
 * UTF-8 sequence. Invalid sequences remain single-byte tokens and are shown
 * through the normal '?' fallback glyph. */
uint8_t raw_reading_progress_token_length(const uint8_t *text);
