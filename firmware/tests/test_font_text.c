#include <assert.h>

#include "font_text.h"

/* The font is static Flash data; the host test verifies the module contract:
 * every printable ASCII char resolves to a 12x24 MSB-first bitmap, space is
 * available (blank), dense glyphs are not blank, non-ASCII/control chars
 * resolve to NULL, and the DMM symbol interface is bounded. */
int main(void)
{
    assert(font_text_width() == 12);
    assert(font_text_height() == 24);
    assert(FONT_TEXT_BYTES_PER_ROW == 2);
    assert(FONT_TEXT_BYTES_PER_GLYPH == 48);

    /* Every printable ASCII char resolves. */
    int c;
    for (c = 32; c <= 126; c++) {
        assert(font_text_bitmap((char)c) != 0);
    }

    /* Space is available (returns a blank bitmap, not NULL). */
    const uint8_t *bsp = font_text_bitmap(' ');
    assert(bsp != 0);
    unsigned i;
    for (i = 0; i < FONT_TEXT_BYTES_PER_GLYPH; i++) {
        assert(bsp[i] == 0);
    }

    /* 'A' is dense: some bytes must be non-zero. */
    const uint8_t *ba = font_text_bitmap('A');
    assert(ba != 0);
    unsigned nonzero = 0;
    for (i = 0; i < FONT_TEXT_BYTES_PER_GLYPH; i++) {
        if (ba[i] != 0) {
            nonzero++;
        }
    }
    assert(nonzero > 0);

    /* Control / non-ASCII chars -> NULL (not a crash). */
    assert(font_text_bitmap((char)0x00) == 0);
    assert(font_text_bitmap((char)0x01) == 0);
    assert(font_text_bitmap((char)0x1F) == 0);
    assert(font_text_bitmap('\n') == 0);
    assert(font_text_bitmap((char)0x7F) == 0);
    assert(font_text_bitmap((char)0xFF) == 0);
    assert(font_text_bitmap((char)0xB5) == 0);

    /* DMM symbols: the required set resolves and is not blank. */
    const char *need = "AVDCHOLTREMUNGkmouhz+-.0123456789";
    for (const char *p = need; *p != '\0'; p++) {
        const uint8_t *b = font_text_bitmap(*p);
        assert(b != 0);
        nonzero = 0;
        for (i = 0; i < FONT_TEXT_BYTES_PER_GLYPH; i++) {
            if (b[i] != 0) {
                nonzero++;
            }
        }
        assert(nonzero > 0);
    }

    assert(font_text_symbol_bitmap(FONT_TEXT_SYM_MICRO) != 0);
    assert(font_text_symbol_bitmap(FONT_TEXT_SYM_OHM) != 0);
    assert(font_text_symbol_bitmap(FONT_TEXT_SYM_DEGREE) != 0);
    assert(font_text_symbol_bitmap(FONT_TEXT_SYM_PLUS_MINUS) != 0);
    assert(font_text_symbol_bitmap(FONT_TEXT_SYM_COUNT) == 0);
    assert(font_text_symbol_bitmap(99) == 0);

    return 0;
}
