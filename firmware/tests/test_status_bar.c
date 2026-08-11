#include <assert.h>
#include <string.h>

#include "status_bar.h"

int main(void)
{
    status_bar_t sb;
    const status_bar_indicator_t *ind;

    status_bar_init(&sb);

    status_bar_set(&sb, 0x08u, 0x80u);   /* HOLD on */
    assert(status_bar_active(&sb, 0x08u, 0x80u));
    assert(!status_bar_active(&sb, 0x08u, 0x40u));   /* TRIG off */

    status_bar_set(&sb, 0x09u, 0x20u);   /* AUTO on (ODS mapping) */
    assert(status_bar_active(&sb, 0x09u, 0x20u));
    assert(!status_bar_active(&sb, 0x09u, 0x10u));   /* ERR off */
    status_bar_set(&sb, 0x09u, 0x00u);
    assert(!status_bar_active(&sb, 0x09u, 0x20u));

    /* All six protocol status tags are stored independently. */
    status_bar_set(&sb, 0x06u, 0x80u);
    status_bar_set(&sb, 0x07u, 0x02u);
    status_bar_set(&sb, 0x08u, 0x40u);
    status_bar_set(&sb, 0x0Au, 0x01u);
    status_bar_set(&sb, 0x0Eu, 0x80u);
    assert(status_bar_active(&sb, 0x06u, 0x80u));
    assert(status_bar_active(&sb, 0x07u, 0x02u));
    assert(status_bar_active(&sb, 0x08u, 0x40u));
    assert(status_bar_active(&sb, 0x0Au, 0x01u));
    assert(status_bar_active(&sb, 0x0Eu, 0x80u));

    /* Core table carries the milestone-1 subset. */
    assert(status_bar_core_get(0, &ind) && strcmp(ind->label, "HOLD") == 0 &&
           ind->tag == 0x08u && ind->bit == 0x80u);
    assert(status_bar_core_get(1, &ind) && strcmp(ind->label, "REM") == 0 &&
           ind->tag == 0x06u && ind->bit == 0x80u);
    assert(status_bar_core_get(2, &ind) && strcmp(ind->label, "REL") == 0 &&
           ind->tag == 0x09u && ind->bit == 0x80u);
    assert(status_bar_core_get(3, &ind) && strcmp(ind->label, "TRIG") == 0 &&
           ind->tag == 0x08u && ind->bit == 0x40u);
    assert(status_bar_core_get(4, &ind) && strcmp(ind->label, "AUTO") == 0 &&
           ind->tag == 0x09u && ind->bit == 0x20u);
    assert(status_bar_core_get(5, &ind) && strcmp(ind->label, "ERR") == 0 &&
           ind->tag == 0x09u && ind->bit == 0x10u);
    assert(!status_bar_core_get(6, &ind));
    assert(!status_bar_core_get(0, 0));

    /* Core-active reflects the stored values. */
    status_bar_set(&sb, 0x09u, 0x20u | 0x10u);
    assert(status_bar_core_active(&sb, 4));
    assert(status_bar_core_active(&sb, 5));
    assert(!status_bar_core_active(&sb, 0));   /* HOLD off */
    status_bar_set(&sb, 0x09u, 0x00u);
    assert(!status_bar_core_active(&sb, 4));
    assert(!status_bar_core_active(&sb, 6));   /* out of range */

    return 0;
}
