#include <assert.h>
#include <string.h>

#include "raw_reading_progress.h"

int main(void)
{
    const char line[] = "ABCdef  ----.--- \xC2\xB5\xCE\xA9\xF0\x9F\x94\x8B xyz";
    raw_reading_progress_t progress;
    uint8_t start;
    uint8_t length;

    assert(raw_reading_progress_token_length((const uint8_t *)"A") == 1u);
    assert(raw_reading_progress_token_length((const uint8_t *)"\xC2\xB5") == 2u);
    assert(raw_reading_progress_token_length((const uint8_t *)"\xF0\x9F\x94\x8B") == 4u);
    assert(raw_reading_progress_token_length((const uint8_t *)"\xC2x") == 1u);

    raw_reading_progress_init(&progress, line, (uint8_t)strlen(line), 8u);
    assert(raw_reading_progress_current(&progress, &start, &length));
    assert(start == 0u && length == 8u);
    raw_reading_progress_advance(&progress);
    assert(raw_reading_progress_current(&progress, &start, &length));
    assert(start == 8u && length == 8u);
    assert(memcmp(line + start, "---.---", 7u) != 0);

    while (raw_reading_progress_current(&progress, &start, &length))
        raw_reading_progress_advance(&progress);
    assert(!raw_reading_progress_current(&progress, &start, &length));

    /* A block boundary must not split the four-byte unknown legal token. */
    raw_reading_progress_init(&progress, "1234567\xF0\x9F\x94\x8B", 11u, 7u);
    assert(raw_reading_progress_current(&progress, &start, &length));
    assert(start == 0u && length == 7u);
    raw_reading_progress_advance(&progress);
    assert(raw_reading_progress_current(&progress, &start, &length));
    assert(start == 7u && length == 4u);
    return 0;
}
