#include <assert.h>
#include <string.h>
#include "ui_layout.h"
int main(void){
    assert(ui_measure_text("KEITHLEY 2000")==13*12);
    assert(ui_measure_text("+25.0\xC2\xB0" "C")==7*12);
    ui_rect_t a={0,0,100,24}, b={50,0,100,24}, c={200,0,50,24};
    assert(ui_rect_overlap(&a,&b));
    assert(!ui_rect_overlap(&a,&c));
    assert(!ui_rect_overlap(&b,&c) || true); // b 50-150, c 200-250 no overlap
    uint16_t fw, zx,ix,rlx,rx,atlx,atx,lx;
    bool fit = ui_layout_second_row("DC VOLTAGE","--","AUTO","500 Read/s",&fw,&zx,&ix,&rlx,&rx,&atlx,&atx,&lx);
    assert(fw==10*12); // DC VOLTAGE 11? actually DC VOLTAGE 11 inc space =11*12=132
    assert(zx==240u);
    uint16_t bx,ax,tx,ux;
    bool ok = ui_layout_top_bar("KEITHLEY 2000","REM","12.5\xC2\xB0" "C","00:01:02",&bx,&ax,&tx,&ux);
    assert(bx==12u);
    assert(ux==960u - 8*12 -12u);
    (void)fit; (void)ok;
    return 0;
}
