#include <assert.h>

#include "keypad.h"

int main(void)
{
    keypad_t k;
    int raw;

    /* Mapping table (ODS TX table, rows PB0..PB3 x cols PB4..PB11;
     * logical col0=R1 ... col7=R8, pin order in hal_board.c). */
    assert(keypad_code(0, 0) == 0x41);
    assert(keypad_code(0, 7) == 0x48);
    assert(keypad_code(1, 2) == 0x4B);
    assert(keypad_code(1, 3) == 0x4C);
    assert(keypad_code(3, 7) == 0x60);
    assert(keypad_code(4, 0) == 0);   /* row out of range */
    assert(keypad_code(0, 8) == 0);   /* col out of range */
    assert(keypad_code(3, 0) == 0);   /* unwired cell (CAL EN unknown) */
    assert(keypad_code(1, 0) == 0);   /* blank cell */

    /* Debounce + edge: press -> code once, hold -> 0, release -> 0x40 once. */
    keypad_init(&k);
    raw = KEYPAD_RAW(0, 0);   /* SHIFT */
    assert(keypad_scan(&k, 0, 0) == 0);          /* idle */
    assert(keypad_scan(&k, raw, 100) == 0);      /* debounce in progress */
    assert(keypad_scan(&k, raw, 110) == 0);      /* still < 20ms */
    assert(keypad_scan(&k, raw, 120) == 0x41);   /* confirmed press */
    assert(keypad_scan(&k, raw, 130) == 0);      /* held, no repeat */
    assert(keypad_scan(&k, raw, 200) == 0);
    assert(keypad_scan(&k, 0, 210) == 0);        /* release debounce */
    assert(keypad_scan(&k, 0, 230) == 0x40);     /* confirmed release */
    assert(keypad_scan(&k, 0, 300) == 0);        /* idle */

    /* A key released before the debounce elapses is ignored entirely. */
    keypad_init(&k);
    raw = KEYPAD_RAW(2, 3);   /* STORE */
    assert(keypad_scan(&k, raw, 10) == 0);
    assert(keypad_scan(&k, 0, 15) == 0);         /* released early */
    assert(keypad_scan(&k, 0, 500) == 0);
    assert(keypad_scan(&k, raw, 600) == 0);      /* fresh press, debouncing */
    assert(keypad_scan(&k, raw, 620) == 0x54);   /* confirmed */

    /* A different key during a press restarts the debounce on the new key. */
    keypad_init(&k);
    raw = KEYPAD_RAW(0, 0);
    assert(keypad_scan(&k, raw, 0) == 0);
    raw = KEYPAD_RAW(0, 1);                       /* DCV */
    assert(keypad_scan(&k, raw, 5) == 0);
    assert(keypad_scan(&k, raw, 25) == 0x42);    /* new key confirmed */

    /* Unknown cell: nothing is ever emitted, even through release. */
    keypad_init(&k);
    raw = KEYPAD_RAW(3, 0);                       /* CAL EN -> unknown */
    assert(keypad_scan(&k, raw, 0) == 0);
    assert(keypad_scan(&k, raw, 20) == 0);
    assert(keypad_scan(&k, raw, 40) == 0);
    assert(keypad_scan(&k, 0, 60) == 0);
    assert(keypad_scan(&k, 0, 80) == 0);

    /* NULL keypad is safe. */
    assert(keypad_scan(0, raw, 0) == 0);
    keypad_init(0);

    return 0;
}
