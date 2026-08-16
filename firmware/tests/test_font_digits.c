#include <assert.h>

#include "font_digits.h"

/* The target stores 64x128 glyph pixels in U5. Verify the compact identity
 * mapping used to find the corresponding RIF directory record. */
int main(void)
{
    uint32_t kind;
    uint16_t code;

    assert(font_digit_rif_code('8', &kind, &code));
    assert(kind == RIF_KIND_DIGIT_CHAR && code == '8');
    assert(font_digit_rif_code('H', &kind, &code));
    assert(font_digit_rif_code('z', &kind, &code));
    assert(font_digit_rif_code('s', &kind, &code));
    assert(!font_digit_rif_code('Z', &kind, &code));
    assert(!font_digit_rif_code('\0', &kind, &code));
    assert(font_digit_rif_symbol_code(FONT_DIGIT_SYM_MICRO, &kind, &code));
    assert(kind == RIF_KIND_DIGIT_SYMBOL && code == FONT_DIGIT_SYM_MICRO);
    assert(font_digit_rif_symbol_code(FONT_DIGIT_SYM_DEGREE, &kind, &code));
    assert(font_digit_rif_symbol_code(FONT_DIGIT_SYM_OHM, &kind, &code));
    assert(!font_digit_rif_symbol_code(FONT_DIGIT_SYM_COUNT, &kind, &code));

    return 0;
}
