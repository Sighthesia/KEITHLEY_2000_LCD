#include <assert.h>

#include "font_digits.h"

/* The font is static data; the host test verifies the module contract:
 * known chars resolve to a 48x96 MSB-first bitmap, dense glyphs are not
 * blank, and unknown chars resolve to NULL. */
int main(void)
{
    const uint8_t *b = font_digit_bitmap('0');
    assert(b != 0);
    assert(font_digit_width() == 48);
    assert(font_digit_height() == 96);

    /* '8' is a dense digit: some bytes must be non-zero. */
    const uint8_t *b8 = font_digit_bitmap('8');
    assert(b8 != 0);
    unsigned nonzero = 0;
    int i;
    for (i = 0; i < (48 * 96 / 8); i++) {
        if (b8[i] != 0) {
            nonzero++;
        }
    }
    assert(nonzero > 0);

    /* Unknown char -> NULL (not a crash). */
    assert(font_digit_bitmap('Z') == 0);

    return 0;
}
