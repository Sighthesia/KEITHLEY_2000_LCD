#include <assert.h>
#include <string.h>

#include "keypad.h"

int main(void)
{
    keypad_t k;
    int raw;

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

    /* Debounce + edge: 2 consecutive stable scans spanning 15 ms confirm a
     * press (once); a release needs 100 ms of stable zeros (rocking a held
     * carbon pill must not split the hold). */
    keypad_init(&k);
    raw = KEYPAD_RAW(0, 0);   /* SHIFT */
    assert(keypad_scan(&k, 0, 0) == 0);          /* idle */
    assert(keypad_scan(&k, raw, 100) == 0);      /* streak 1 */
    assert(keypad_scan(&k, raw, 110) == 0);      /* streak 2, only 10 ms */
    assert(keypad_scan(&k, raw, 120) == 0x41);   /* 20 ms: confirmed press */
    assert(keypad_scan(&k, raw, 130) == 0);      /* held, no repeat */
    assert(keypad_scan(&k, raw, 200) == 0);
    assert(keypad_scan(&k, 0, 210) == 0);        /* release streak 1 */
    assert(keypad_scan(&k, 0, 260) == 0);        /* 50 ms: not yet */
    assert(keypad_scan(&k, 0, 310) == 0x40);     /* 100 ms: confirmed */
    assert(keypad_scan(&k, 0, 400) == 0);        /* idle */

    /* Rocking: brief dropouts inside a hold emit nothing and re-pressing
     * the same key emits nothing either (single hold, single code). */
    keypad_init(&k);
    raw = KEYPAD_RAW(0, 5);   /* OHM */
    assert(keypad_scan(&k, raw, 1000) == 0);
    assert(keypad_scan(&k, raw, 1020) == 0x46);  /* confirmed press */
    assert(keypad_scan(&k, raw, 1100) == 0);     /* held */
    assert(keypad_scan(&k, 0, 1110) == 0);       /* 10 ms dropout */
    assert(keypad_scan(&k, raw, 1120) == 0);     /* contact back: still held */
    assert(keypad_scan(&k, 0, 1130) == 0);       /* 20 ms dropout */
    assert(keypad_scan(&k, raw, 1140) == 0);     /* still held, no repeat */
    assert(keypad_scan(&k, raw, 1200) == 0);
    assert(keypad_scan(&k, 0, 1210) == 0);       /* real release starts */
    assert(keypad_scan(&k, 0, 1310) == 0x40);    /* 100 ms: confirmed */

    /* A key released before the streak completes is ignored entirely. */
    keypad_init(&k);
    raw = KEYPAD_RAW(2, 3);   /* STORE */
    assert(keypad_scan(&k, raw, 10) == 0);
    assert(keypad_scan(&k, 0, 15) == 0);         /* released early */
    assert(keypad_scan(&k, 0, 500) == 0);
    assert(keypad_scan(&k, raw, 600) == 0);      /* fresh press, streak 1 */
    assert(keypad_scan(&k, raw, 615) == 0x54);   /* confirmed */

    /* A different key during a press restarts the streak on the new key. */
    keypad_init(&k);
    raw = KEYPAD_RAW(0, 0);
    assert(keypad_scan(&k, raw, 0) == 0);
    raw = KEYPAD_RAW(0, 1);                       /* DCV */
    assert(keypad_scan(&k, raw, 5) == 0);
    assert(keypad_scan(&k, raw, 15) == 0);       /* 10 ms, not yet */
    assert(keypad_scan(&k, raw, 20) == 0x42);    /* new key confirmed */

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
