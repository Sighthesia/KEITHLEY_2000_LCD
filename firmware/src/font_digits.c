#include "font_digits.h"

static const char s_digit_chars[] = "0123456789.+-Ee%mukKMWVOhDAC?RFLHzs";

bool font_digit_rif_code(char c, uint32_t *kind, uint16_t *code)
{
    uint16_t i;

    if (kind == 0 || code == 0) {
        return false;
    }
    for (i = 0u; s_digit_chars[i] != '\0'; i++) {
        if (s_digit_chars[i] == c) {
            *kind = RIF_KIND_DIGIT_CHAR;
            *code = (uint8_t)c;
            return true;
        }
    }
    return false;
}

bool font_digit_rif_symbol_code(uint8_t symbol, uint32_t *kind, uint16_t *code)
{
    if (kind == 0 || code == 0 || symbol >= FONT_DIGIT_SYM_COUNT) {
        return false;
    }
    *kind = RIF_KIND_DIGIT_SYMBOL;
    *code = symbol;
    return true;
}
