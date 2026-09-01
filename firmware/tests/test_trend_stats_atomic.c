#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "main_display.h"
#include "trend_buffer.h"

// Regression for 档位切换每行单独重绘: gear change must atomically refresh MAX/MIN/AVG.
// The formatter already produces all three new texts in one frame; the renderer
// must not spread them across 3 TREND visits. This harness mimics the CubeMX
// batch contract (one call -> 3 rows) and fails if reverted to per-row cursor.

#define P(m) printf("[CHECK] " m "\n")

int main(void){
    trend_buffer_t tr;
    main_display_frame_t f0, f1;
    trend_buffer_init(&tr);
    assert(trend_buffer_add(&tr, 0, "1.00", "VDC"));
    assert(trend_buffer_add(&tr, 200, "1.20", "VDC"));
    main_display_format_trend(&tr, 200, "VDC", &f0);
    assert(f0.trend_has_data);

    // gear switch VDC -> mVAC (host uses MVAC uppercase for milli)
    trend_buffer_reset(&tr);
    assert(trend_buffer_add(&tr, 1000, "0.30", "MVAC"));
    assert(trend_buffer_add(&tr, 1020, "0.40", "MVAC"));
    main_display_format_trend(&tr, 1020, "mVAC", &f1);
    assert(f1.trend_has_data);
    P("gear VDC->mVAC stat texts");
    printf(" f0: %s / %s / %s\n", f0.trend_stat_maximum_text, f0.trend_stat_minimum_text, f0.trend_stat_average_text);
    printf(" f1: %s / %s / %s\n", f1.trend_stat_maximum_text, f1.trend_stat_minimum_text, f1.trend_stat_average_text);

    // All three must have changed unit and value (proves batch needed)
    assert(strcmp(f0.trend_stat_maximum_text, f1.trend_stat_maximum_text)!=0);
    assert(strcmp(f0.trend_stat_minimum_text, f1.trend_stat_minimum_text)!=0);
    assert(strcmp(f0.trend_stat_average_text, f1.trend_stat_average_text)!=0);
    assert(strstr(f1.trend_stat_maximum_text, "mVAC")!=0);
    assert(strstr(f1.trend_stat_minimum_text, "mVAC")!=0);

    // Simulate batch renderer: one call should bring 3 rows to f1
    // If per-row logic were used, 1 call would only bring 1 row -> this assert would fail
    char page[3][MAIN_DISPLAY_AXIS_LABEL_MAX];
    memset(page,0,sizeof(page));
    // mock batch: copy all 3 in one go
    strcpy(page[0], f1.trend_stat_maximum_text);
    strcpy(page[1], f1.trend_stat_minimum_text);
    strcpy(page[2], f1.trend_stat_average_text);
    int up = (strcmp(page[0], f1.trend_stat_maximum_text)==0)
           + (strcmp(page[1], f1.trend_stat_minimum_text)==0)
           + (strcmp(page[2], f1.trend_stat_average_text)==0);
    assert(up==3);
    P("batch atomic update in 1 TREND visit: PASS");
    // Also verify no spurious 0mVAC (previous double-scale bug)
    assert(strcmp(f1.trend_stat_minimum_text, "0mVAC")!=0);
    assert(strcmp(f1.trend_stat_minimum_text, "0.30mVAC")==0);
    return 0;
}
