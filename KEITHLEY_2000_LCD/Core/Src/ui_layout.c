#include "ui_layout.h"
#include "font_text.h"
#include <string.h>

uint16_t ui_measure_text(const char *text){
    uint16_t n = 0u;
    if(!text) return 0u;
    while(*text){
        if((uint8_t)text[0]==0xC2u && (uint8_t)text[1]==0xB0u){ n+=FONT_TEXT_WIDTH; text+=2; continue; }
        if((uint8_t)text[0]==0xC2u && (uint8_t)text[1]==0xB5u){ n+=FONT_TEXT_WIDTH; text+=2; continue; }
        if((uint8_t)text[0]==0xCEu && (uint8_t)text[1]==0xA9u){ n+=FONT_TEXT_WIDTH; text+=2; continue; }
        n+=FONT_TEXT_WIDTH; text++;
    }
    return n;
}

bool ui_rect_overlap(const ui_rect_t *a, const ui_rect_t *b){
    if(!a||!b) return false;
    return !(a->x + a->w <= b->x || b->x + b->w <= a->x ||
             a->y + a->h <= b->y || b->y + b->h <= a->y);
}

ui_rect_t ui_rect_from_text(uint16_t x, uint16_t y, const char *text, uint16_t h){
    ui_rect_t r; r.x=x; r.y=y; r.w=ui_measure_text(text); r.h=h; return r;
}

bool ui_layout_second_row(const char *function, const char *impedance,
                          const char *range, const char *rate,
                          uint16_t *out_function_w,
                          uint16_t *out_zin_x, uint16_t *out_imp_x,
                          uint16_t *out_range_label_x, uint16_t *out_range_x,
                          uint16_t *out_rate_label_x, uint16_t *out_rate_x,
                          uint16_t *out_lamps_x){
    const uint16_t gap_meta = 18u;
    const uint16_t gap_label = 6u;
    uint16_t fw = ui_measure_text(function);
    uint16_t imp_w = ui_measure_text(impedance);
    uint16_t range_w = ui_measure_text(range);
    uint16_t rate_w = ui_measure_text(rate);
    uint16_t zin_label_w = 3u*FONT_TEXT_WIDTH;
    uint16_t range_label_w = 5u*FONT_TEXT_WIDTH;
    uint16_t rate_label_w = 4u*FONT_TEXT_WIDTH;
    uint16_t lamps_w = (4u+3u+4u)*FONT_TEXT_WIDTH + 24u; // FILT+gap+REL+gap+MATH
    uint16_t total;
    bool squeezed = false;
    uint16_t bx = 240u;
    // function at 12, width fw
    if(out_function_w) *out_function_w = fw;
    if(out_zin_x) *out_zin_x = bx;
    if(out_imp_x) *out_imp_x = (uint16_t)(bx + zin_label_w + gap_label);
    bx = (uint16_t)(bx + zin_label_w + gap_label + imp_w + gap_meta);
    if(out_range_label_x) *out_range_label_x = bx;
    if(out_range_x) *out_range_x = (uint16_t)(bx + range_label_w + gap_label);
    bx = (uint16_t)(bx + range_label_w + gap_label + range_w + gap_meta);
    if(out_rate_label_x) *out_rate_label_x = bx;
    if(out_rate_x) *out_rate_x = (uint16_t)(bx + rate_label_w + gap_label);
    bx = (uint16_t)(bx + rate_label_w + gap_label + rate_w + gap_meta);
    if(out_lamps_x) *out_lamps_x = bx;
    total = (uint16_t)(bx + lamps_w);
    if(total > 960u){
        squeezed = true;
        // simple squeeze: prioritize function, then truncate rate/range values if needed
        // for now just signal squeezed, caller may truncate
    }
    return !squeezed;
}

bool ui_layout_top_bar(const char *brand, const char *active,
                       const char *temperature, const char *uptime,
                       uint16_t *out_brand_x, uint16_t *out_active_x,
                       uint16_t *out_temp_x, uint16_t *out_uptime_x){
    uint16_t brand_w = ui_measure_text(brand);
    uint16_t active_w = ui_measure_text(active);
    uint16_t temp_w = ui_measure_text(temperature);
    uint16_t uptime_w = ui_measure_text(uptime);
    uint16_t brand_x = 12u;
    uint16_t uptime_x = (uint16_t)(960u - uptime_w - 12u);
    uint16_t temp_gap = 24u;
    uint16_t temp_x = (uint16_t)(uptime_x - temp_gap - temp_w);
    uint16_t active_x = active_w ? (uint16_t)((960u - active_w)/2u) : 0u;
    ui_rect_t brand_r = {brand_x,0u,brand_w,24u};
    ui_rect_t active_r = {active_x,0u,active_w,24u};
    ui_rect_t temp_r = {temp_x,0u,temp_w,24u};
    // detect overlap brand vs active
    if(active_w && ui_rect_overlap(&brand_r,&active_r)){
        // push active to after brand with gap
        active_x = (uint16_t)(brand_x + brand_w + 12u);
        active_r.x = active_x;
    }
    if(active_w && ui_rect_overlap(&active_r,&temp_r)){
        // squeeze active if overlaps temp
        // truncate active would be needed; for now just keep and let caller decide
    }
    if(out_brand_x) *out_brand_x = brand_x;
    if(out_active_x) *out_active_x = active_x;
    if(out_temp_x) *out_temp_x = temp_x;
    if(out_uptime_x) *out_uptime_x = uptime_x;
    return !(ui_rect_overlap(&brand_r,&active_r) || ui_rect_overlap(&active_r,&temp_r));
}
