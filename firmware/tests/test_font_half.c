#include <assert.h>

#include "font_half.h"

/* The half-height DC/AC suffix font is static data; the host test verifies
 * the module contract: 'D'/'C'/'A' resolve to a 32x56 MSB-first bitmap and
 * are not blank, and unknown chars resolve to NULL. The base unit stays at
 * digit size, so only the DC/AC suffix uses this font. */
int main(void)
{
    assert(font_half_width() == 32);
    assert(font_half_height() == 56);

    const uint8_t *b = font_half_bitmap('D');
    assert(b != 0);
    assert(font_half_bitmap('C') != 0);
    assert(font_half_bitmap('A') != 0);

    unsigned nonzero = 0;
    int i;
    for (i = 0; i < (32 * 56 / 8); i++) {
        if (b[i] != 0) {
            nonzero++;
        }
    }
    assert(nonzero > 0);

    /* Unknown char -> NULL (not a crash). */
    assert(font_half_bitmap('x') == 0);
    return 0;
}