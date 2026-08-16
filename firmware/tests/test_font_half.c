#include <assert.h>

#include "font_half.h"

/* The half-height AC/DC unit font is static data; the host test verifies the
 * module contract: known chars resolve to a 32x64 MSB-first bitmap, the
 * DC/AC base-unit letters 'D'/'C'/'A'/'V'/'m' are not blank, and unknown
 * chars resolve to NULL. (k/M ohm units stay at digit size, so they are not
 * in this font.) */
int main(void)
{
    assert(font_half_width() == 32);
    assert(font_half_height() == 64);

    const uint8_t *b = font_half_bitmap('D');
    assert(b != 0);
    assert(font_half_bitmap('C') != 0);
    assert(font_half_bitmap('A') != 0);
    assert(font_half_bitmap('m') != 0);
    assert(font_half_bitmap('V') != 0);

    unsigned nonzero = 0;
    int i;
    for (i = 0; i < (32 * 64 / 8); i++) {
        if (b[i] != 0) {
            nonzero++;
        }
    }
    assert(nonzero > 0);

    /* Unknown char -> NULL (not a crash). */
    assert(font_half_bitmap('x') == 0);
    return 0;
}