#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RENDER_PHASE_INITIAL_STATUS = 0,
    RENDER_PHASE_INITIAL_READING,
    RENDER_PHASE_INITIAL_TREND_STATIC,
    RENDER_PHASE_INITIAL_TREND_COLUMNS,
    RENDER_PHASE_IDLE,
    RENDER_PHASE_UPDATE_STATUS,
    RENDER_PHASE_UPDATE_READING,
    RENDER_PHASE_UPDATE_TREND_AXES,
    RENDER_PHASE_UPDATE_TREND_COLUMNS,
} render_phase_t;

#define RENDER_DIRTY_STATUS  0x01u
#define RENDER_DIRTY_READING 0x02u

typedef struct {
    render_phase_t phase;
    uint8_t pending_regions;
    bool trend_pending;
    bool initial_complete;
    bool initial_complete_edge;
} render_scheduler_t;

void render_scheduler_init(render_scheduler_t *scheduler);
void render_scheduler_request_regions(render_scheduler_t *scheduler,
                                      uint8_t regions);
void render_scheduler_request_trend(render_scheduler_t *scheduler);
void render_scheduler_complete_phase(render_scheduler_t *scheduler);
bool render_scheduler_take_initial_complete(render_scheduler_t *scheduler);
