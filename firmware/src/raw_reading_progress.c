#include "raw_reading_progress.h"

static bool continuation(uint8_t byte)
{
    return (byte & 0xC0u) == 0x80u;
}

uint8_t raw_reading_progress_token_length(const uint8_t *text)
{
    uint8_t first;
    uint8_t length;
    uint8_t i;

    if (text == 0 || *text == '\0') return 0u;
    first = text[0];
    if (first < 0x80u) return 1u;
    if (first >= 0xC2u && first <= 0xDFu) length = 2u;
    else if (first >= 0xE0u && first <= 0xEFu) length = 3u;
    else if (first >= 0xF0u && first <= 0xF4u) length = 4u;
    else return 1u;
    for (i = 1u; i < length; i++)
        if (!continuation(text[i])) return 1u;
    return length;
}

void raw_reading_progress_init(raw_reading_progress_t *progress,
                               const char *line, uint8_t length,
                               uint8_t block_tokens)
{
    if (progress == 0) return;
    progress->line = line;
    progress->length = length;
    progress->offset = 0u;
    progress->column = 0u;
    progress->block_tokens = block_tokens != 0u
                                 ? block_tokens
                                 : RAW_READING_PROGRESS_BLOCK_TOKENS;
}

bool raw_reading_progress_current(const raw_reading_progress_t *progress,
                                  uint8_t *start, uint8_t *length)
{
    uint8_t end;
    uint8_t tokens = 0u;

    if (start != 0) *start = 0u;
    if (length != 0) *length = 0u;
    if (progress == 0 || progress->line == 0 || progress->offset >= progress->length)
        return false;
    end = progress->offset;
    while (end < progress->length && tokens < progress->block_tokens) {
        uint8_t token = raw_reading_progress_token_length(
            (const uint8_t *)&progress->line[end]);
        if (token == 0u || token > progress->length - end)
            token = 1u;
        end = (uint8_t)(end + token);
        tokens++;
    }
    if (start != 0) *start = progress->offset;
    if (length != 0) *length = (uint8_t)(end - progress->offset);
    return true;
}

void raw_reading_progress_advance(raw_reading_progress_t *progress)
{
    uint8_t start;
    uint8_t length;
    uint8_t end;
    uint8_t tokens = 0u;

    if (progress != 0 && raw_reading_progress_current(progress, &start, &length)) {
        progress->offset = (uint8_t)(start + length);
        end = progress->offset;
        while (end > start && tokens < progress->block_tokens) {
            uint8_t token = raw_reading_progress_token_length(
                (const uint8_t *)&progress->line[start]);
            if (token == 0u || token > end - start) token = 1u;
            start = (uint8_t)(start + token);
            tokens++;
        }
        progress->column = (uint8_t)(progress->column + tokens);
    }
}
