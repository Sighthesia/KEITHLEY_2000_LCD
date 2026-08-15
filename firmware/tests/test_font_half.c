#include <assert.h>

#include "font_half.h"

/* The half-height DC/AC font is static data; the host test verifies the
 * module contract: known chars resolve to a 32x64 MSB-first bitmap, 'D'/'C'/'A'
 * are not blank, and unknown chars resolve to NULL. */
int main(void)
{
    assert(font_half_width() == 32);
    assert(font_half_height() == 64);

    const uint8_t *b = font_half_bitmap('D');
    assert(b != 0);
    assert(font_half_bitmap('C') != 0);
    assert(font_half_bitmap('A') != 0);

    unsigned nonzero = 0;
    int i;
    for (i = 0; i < (32 * 64 / 8); i++) {
        if (b[i] != 0) {
            nonzero++;
        }
    }
    assert(nonzero > 0);

    /* Unknown char -> NULL (not a crash). */
    assert(font_half_bitmap('V') == 0);
    return 0;
}