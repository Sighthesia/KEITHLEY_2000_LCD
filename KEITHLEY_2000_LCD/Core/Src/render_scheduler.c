#include "render_scheduler.h"

static void select_pending(render_scheduler_t *scheduler)
{
    if ((scheduler->pending_regions & RENDER_DIRTY_STATUS) != 0u) {
        scheduler->pending_regions &= (uint8_t)~RENDER_DIRTY_STATUS;
        scheduler->phase = RENDER_PHASE_UPDATE_STATUS;
    } else if ((scheduler->pending_regions & RENDER_DIRTY_READING) != 0u) {
        scheduler->pending_regions &= (uint8_t)~RENDER_DIRTY_READING;
        scheduler->phase = RENDER_PHASE_UPDATE_READING;
    } else if (scheduler->trend_pending) {
        scheduler->trend_pending = false;
        scheduler->phase = RENDER_PHASE_UPDATE_TREND_AXES;
    } else {
        scheduler->phase = RENDER_PHASE_IDLE;
    }
}

void render_scheduler_init(render_scheduler_t *scheduler)
{
    if (scheduler == 0) return;
    scheduler->phase = RENDER_PHASE_INITIAL_STATUS;
    scheduler->pending_regions = 0u;
    scheduler->trend_pending = false;
    scheduler->initial_complete = false;
    scheduler->initial_complete_edge = false;
}

void render_scheduler_request_regions(render_scheduler_t *scheduler,
                                      uint8_t regions)
{
    if (scheduler == 0) return;
    scheduler->pending_regions |= regions;
    if (scheduler->phase == RENDER_PHASE_IDLE) select_pending(scheduler);
}

void render_scheduler_request_trend(render_scheduler_t *scheduler)
{
    if (scheduler == 0) return;
    scheduler->trend_pending = true;
    if (scheduler->phase == RENDER_PHASE_IDLE) select_pending(scheduler);
}

void render_scheduler_complete_phase(render_scheduler_t *scheduler)
{
    if (scheduler == 0) return;
    switch (scheduler->phase) {
    case RENDER_PHASE_INITIAL_STATUS:
        scheduler->phase = RENDER_PHASE_INITIAL_READING;
        break;
    case RENDER_PHASE_INITIAL_READING:
        scheduler->phase = RENDER_PHASE_INITIAL_TREND_STATIC;
        break;
    case RENDER_PHASE_INITIAL_TREND_STATIC:
        scheduler->phase = RENDER_PHASE_INITIAL_TREND_COLUMNS;
        break;
    case RENDER_PHASE_INITIAL_TREND_COLUMNS:
        scheduler->initial_complete = true;
        scheduler->initial_complete_edge = true;
        select_pending(scheduler);
        break;
    case RENDER_PHASE_UPDATE_TREND_AXES:
        scheduler->phase = RENDER_PHASE_UPDATE_TREND_COLUMNS;
        break;
    case RENDER_PHASE_IDLE:
        select_pending(scheduler);
        break;
    default:
        select_pending(scheduler);
        break;
    }
}

bool render_scheduler_take_initial_complete(render_scheduler_t *scheduler)
{
    bool edge;
    if (scheduler == 0) return false;
    edge = scheduler->initial_complete_edge;
    scheduler->initial_complete_edge = false;
    return edge;
}
