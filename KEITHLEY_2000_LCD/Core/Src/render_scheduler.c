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
        /* Frame-level resume point: a pass whose axes phase already ran
         * continues at the columns phase. Re-selecting AXES would replay
         * the axis rebuild and erase columns rendered earlier in this
         * pass (review C-1). */
        scheduler->phase = scheduler->trend_resume_at_columns
                               ? RENDER_PHASE_UPDATE_TREND_COLUMNS
                               : RENDER_PHASE_UPDATE_TREND_AXES;
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
    scheduler->trend_resume_at_columns = false;
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
    /* Low priority: only raise the flag. Starting the graph here would let
     * trend work occupy the pipeline ahead of STATUS/READING updates that
     * arrive one turn later. */
    scheduler->trend_pending = true;
}

void render_scheduler_kick(render_scheduler_t *scheduler)
{
    if (scheduler == 0) return;
    if (scheduler->phase == RENDER_PHASE_IDLE) select_pending(scheduler);
}

bool render_scheduler_yield_trend(render_scheduler_t *scheduler)
{
    if (scheduler == 0) return false;
    if (scheduler->phase != RENDER_PHASE_UPDATE_TREND_AXES &&
        scheduler->phase != RENDER_PHASE_UPDATE_TREND_COLUMNS)
        return false;
    if (scheduler->pending_regions == 0u) return false;
    /* Re-flag the unfinished pass; select_pending services the regions
     * first and then resumes it -- directly at columns when the axes
     * already completed, so an interrupted full rebuild cannot restart
     * and erase rendered columns. */
    scheduler->trend_pending = true;
    select_pending(scheduler);
    return true;
}

bool render_scheduler_should_hold_transaction(bool transaction_active,
                                              uint32_t elapsed_ms,
                                              uint32_t max_hold_ms)
{
    return transaction_active && elapsed_ms < max_hold_ms;
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
        /* Frame-level state: this pass must never re-enter its axes
         * phase, not even across a yield/preempt cycle. */
        scheduler->trend_resume_at_columns = true;
        scheduler->phase = RENDER_PHASE_UPDATE_TREND_COLUMNS;
        break;
    case RENDER_PHASE_UPDATE_TREND_COLUMNS:
        /* Pass boundary: forget the resume point so the next requested
         * pass performs a fresh axes evaluation. */
        scheduler->trend_resume_at_columns = false;
        select_pending(scheduler);
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
