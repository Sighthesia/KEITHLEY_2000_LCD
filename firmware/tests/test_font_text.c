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

    {
        font_text_glyph_t glyph;
        const uint8_t *eight;
        static const char three[] = "\xE2\x98\x83" "A";
        static const char four[] = "\xF0\x9F\x98\x80" "B";
        static const char invalid[] = "\xE2\x28\xA1" "C";
        char boundary[49];
        unsigned j;

        assert(font_text_glyph("8", 1u, &glyph));
        eight = glyph.bitmap;
        assert(eight != 0);
        assert(glyph.bytes == 1u);
        assert(font_text_width() == 12u);
        assert(font_text_height() == 24u);
        assert(eight == font_text_bitmap('8'));
        /* Rows are two bytes wide, with the first pixel in the MSB. */
        assert(eight[0] == 0x00u && eight[1] == 0x00u); /* empty row */
        assert(eight[6] == 0x1Fu && eight[7] == 0x80u); /* left edge */
        assert(eight[10] == 0x30u && eight[11] == 0xE0u); /* interior run */
        assert((eight[7] & 0x80u) != 0u); /* byte boundary bit */
        assert((eight[7] & 0x0Fu) == 0u); /* unused width bits stay clear */
        assert(eight[36] == 0x00u && eight[37] == 0x00u); /* empty row */
        assert(!font_text_glyph(0, 1u, &glyph));
        assert(!font_text_glyph("", 0u, &glyph));
        assert(font_text_glyph("\x01", 1u, &glyph));
        assert(glyph.bytes == 1u && glyph.bitmap == font_text_bitmap('?'));

        assert(font_text_glyph(three, 4u, &glyph));
        assert(glyph.bytes == 3u && glyph.bitmap == font_text_bitmap('?'));
        assert(font_text_glyph(three + glyph.bytes, 1u, &glyph));
        assert(glyph.bytes == 1u && glyph.bitmap == font_text_bitmap('A'));
        assert(font_text_glyph(four, 5u, &glyph));
        assert(glyph.bytes == 4u && glyph.bitmap == font_text_bitmap('?'));
        assert(font_text_glyph(four + glyph.bytes, 1u, &glyph));
        assert(glyph.bytes == 1u && glyph.bitmap == font_text_bitmap('B'));
        assert(font_text_glyph(invalid, 4u, &glyph));
        assert(glyph.bytes == 1u && glyph.bitmap == font_text_bitmap('?'));
        assert(font_text_glyph(invalid + glyph.bytes, 3u, &glyph));
        assert(glyph.bytes == 1u && glyph.bitmap == font_text_bitmap('('));

        for (j = 0u; j < 48u; j++) boundary[j] = 'A';
        boundary[48] = '\0';
        assert(font_text_glyph(&boundary[47], 1u, &glyph));
        assert(glyph.bytes == 1u && glyph.bitmap == font_text_bitmap('A'));
    }

    return 0;
}
