#include <assert.h>

#include "font_digits.h"

/* The font is static data; the host test verifies the module contract:
 * known chars resolve to a 64x128 MSB-first bitmap, dense glyphs are not
 * blank, the DMM unit symbols (µ ° Ω) resolve through the symbol table, and
 * unknown chars resolve to NULL. */
int main(void)
{
    const uint8_t *b = font_digit_bitmap('0');
    assert(b != 0);
    assert(font_digit_width() == 64);
    assert(font_digit_height() == 128);

    /* '8' is a dense digit: some bytes must be non-zero. */
    const uint8_t *b8 = font_digit_bitmap('8');
    assert(b8 != 0);
    unsigned nonzero = 0;
    int i;
    for (i = 0; i < (64 * 128 / 8); i++) {
        if (b8[i] != 0) {
            nonzero++;
        }
    }
    assert(nonzero > 0);

    /* Unit alphabet additions must resolve (H/z/s for Hz, kΩ, s). */
    assert(font_digit_bitmap('H') != 0);
    assert(font_digit_bitmap('z') != 0);
    assert(font_digit_bitmap('s') != 0);

    /* DMM unit symbols live in the separate symbol table. */
    assert(font_digit_symbol_bitmap(FONT_DIGIT_SYM_MICRO) != 0);
    assert(font_digit_symbol_bitmap(FONT_DIGIT_SYM_DEGREE) != 0);
    assert(font_digit_symbol_bitmap(FONT_DIGIT_SYM_OHM) != 0);
    assert(font_digit_symbol_bitmap(FONT_DIGIT_SYM_COUNT) == 0);

    /* Unknown char -> NULL (not a crash). */
    assert(font_digit_bitmap('Z') == 0);

    return 0;
}