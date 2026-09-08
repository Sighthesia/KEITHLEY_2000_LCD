#include <assert.h>
#include <string.h>

#include "keypad.h"

int main(void)
{
    keypad_t k;
    uint32_t a;
    uint32_t b;

    /* Name table mirrors the code table ("" = unwired cell). */
    assert(strcmp(keypad_name(0, 0), "SHIFT") == 0);
    assert(strcmp(keypad_name(0, 7), "FREQ") == 0);
    assert(strcmp(keypad_name(1, 7), "TEMP") == 0);
    assert(strcmp(keypad_name(2, 0), "LOCAL") == 0);
    assert(strcmp(keypad_name(3, 7), "EXIT") == 0);
    assert(keypad_name(1, 0)[0] == '\0');
    assert(keypad_name(3, 0)[0] == '\0');
    assert(keypad_name(4, 0)[0] == '\0');

    /* Mapping table (ODS TX table, rows PB0..PB3 x cols PB4..PB11;
     * logical col0=R9 ... col7=R16, pin order in hal_board.c). */
    assert(keypad_code(0, 0) == 0x41);
    assert(keypad_code(0, 7) == 0x48);
    assert(keypad_code(1, 2) == 0x4B);
    assert(keypad_code(1, 3) == 0x4C);
    assert(keypad_code(3, 7) == 0x60);
    assert(keypad_code(4, 0) == 0);   /* row out of range */
    assert(keypad_code(0, 8) == 0);   /* col out of range */
    assert(keypad_code(3, 0) == 0);   /* unwired cell (CAL EN unknown) */
    assert(keypad_code(1, 0) == 0);   /* blank cell */

    /* Timer debounce: a changed set commits after 100 ms stability.
     * Single press -> code once, hold -> silence, all released -> 0x40. */
    keypad_init(&k);
    a = KEYPAD_CELL(0, 0);   /* SHIFT */
    assert(keypad_scan(&k, 0u, 0) == 0);        /* idle */
    assert(keypad_scan(&k, a, 100) == 0);       /* candidate */
    assert(keypad_scan(&k, a, 150) == 0);       /* 50 ms: not yet */
    assert(keypad_scan(&k, a, 200) == 0x41);    /* 100 ms: press */
    assert(keypad_scan(&k, a, 250) == 0);       /* held, no repeat */
    assert(keypad_scan(&k, 0u, 300) == 0);      /* release candidate */
    assert(keypad_scan(&k, 0u, 400) == 0x40);   /* 100 ms: all released */
    assert(keypad_scan(&k, 0u, 500) == 0);      /* idle */

    /* Quick tap (< 100 ms) is eaten by design. */
    keypad_init(&k);
    assert(keypad_scan(&k, KEYPAD_CELL(2, 3), 0) == 0);
    assert(keypad_scan(&k, 0u, 50) == 0);
    assert(keypad_scan(&k, 0u, 500) == 0);

    /* Chatter inside the window emits nothing; restabilized press works. */
    keypad_init(&k);
    b = KEYPAD_CELL(0, 1);   /* DCV */
    assert(keypad_scan(&k, b, 0) == 0);
    assert(keypad_scan(&k, 0u, 30) == 0);
    assert(keypad_scan(&k, b, 60) == 0);
    assert(keypad_scan(&k, 0u, 90) == 0);
    assert(keypad_scan(&k, b, 200) == 0);       /* restabilize, tick=200 */
    assert(keypad_scan(&k, b, 300) == 0x42);

    /* Combo: SHIFT, then DCV added; one member released (no 0x40 while
     * DCV still held); all released -> single 0x40. */
    keypad_init(&k);
    a = KEYPAD_CELL(0, 0);
    b = KEYPAD_CELL(0, 1);
    assert(keypad_scan(&k, a, 0) == 0);
    assert(keypad_scan(&k, a, 100) == 0x41);
    assert(keypad_scan(&k, a, 110) == 0);       /* queue drained */
    assert(keypad_scan(&k, a | b, 200) == 0);
    assert(keypad_scan(&k, a | b, 300) == 0x42);/* only the added key */
    assert(keypad_scan(&k, a | b, 310) == 0);
    assert(keypad_scan(&k, b, 400) == 0);       /* SHIFT up, DCV held */
    assert(keypad_scan(&k, b, 500) == 0);       /* still no 0x40 */
    assert(keypad_scan(&k, 0u, 600) == 0);
    assert(keypad_scan(&k, 0u, 700) == 0x40);
    assert(keypad_scan(&k, 0u, 800) == 0);

    /* Unknown cell: sanitized away, never emits, even through release. */
    keypad_init(&k);
    assert(keypad_scan(&k, KEYPAD_CELL(3, 0), 0) == 0);   /* CAL EN */
    assert(keypad_scan(&k, KEYPAD_CELL(3, 0), 200) == 0);
    assert(keypad_scan(&k, 0u, 300) == 0);
    assert(keypad_scan(&k, 0u, 500) == 0);

    /* NULL keypad is safe. */
    assert(keypad_scan(0, a, 0) == 0);
    keypad_init(0);

    return 0;
}
