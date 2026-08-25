#include <assert.h>

#include "render_scheduler.h"

int main(void)
{
    render_scheduler_t scheduler;

    render_scheduler_init(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_INITIAL_STATUS);
    assert(!scheduler.trend_resume_at_columns);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_INITIAL_READING);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_INITIAL_TREND_STATIC);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_INITIAL_TREND_COLUMNS);
    assert(!render_scheduler_take_initial_complete(&scheduler));
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_IDLE);
    assert(render_scheduler_take_initial_complete(&scheduler));
    assert(!render_scheduler_take_initial_complete(&scheduler));

    /* Periodic trend work is incremental: it may update dynamic axes and
     * columns, but it must never return to the full-clear initial phase.
     * Trend is a low-priority PENDING flag: it starts only when the main
     * loop kicks the idle scheduler, never straight from the request. */
    render_scheduler_request_trend(&scheduler);
    render_scheduler_kick(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_TREND_AXES);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_IDLE);

    /* Routine host updates select only their dirty local regions. */
    render_scheduler_request_regions(&scheduler, RENDER_DIRTY_READING);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_READING);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_IDLE);

    /* A trend already in progress retains unrelated dirty regions and hands
     * off to them without restarting either trend phase. */
    render_scheduler_request_trend(&scheduler);
    render_scheduler_kick(&scheduler);
    render_scheduler_request_regions(&scheduler,
        RENDER_DIRTY_STATUS | RENDER_DIRTY_READING);
    render_scheduler_request_trend(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_TREND_AXES);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_STATUS);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_READING);
    render_scheduler_complete_phase(&scheduler);
    /* The coalesced request raised during the active graph is serviced next,
     * proving that neither continuous UI dirties nor graph dirties deadlock. */
    assert(scheduler.phase == RENDER_PHASE_UPDATE_TREND_AXES);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_IDLE);

    /* Hidden-frame page invariant, scheduler side: an active hidden frame
     * remains on its phase until that phase completes. A pending reading
     * update is not restarted by a trend request -- the request is queued
     * and serviced only after the reading phase finishes. */
    render_scheduler_request_regions(&scheduler, RENDER_DIRTY_READING);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_READING);
    render_scheduler_request_trend(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_READING);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_TREND_AXES);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_IDLE);

    /* Likewise a request arriving mid-status-phase neither restarts the
     * running phase nor drops the still-pending reading: the queue is
     * serviced strictly after the active phase completes. */
    render_scheduler_request_regions(&scheduler,
        RENDER_DIRTY_STATUS | RENDER_DIRTY_READING);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_STATUS);
    render_scheduler_request_regions(&scheduler, RENDER_DIRTY_READING);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_STATUS);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_UPDATE_READING);
    render_scheduler_complete_phase(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_IDLE);

    /* --- Task 4: readings have priority over trend work --- */

    /* A queued trend is a pending low-priority flag: it must not occupy the
     * pipeline. A reading update requested after it is selected first, and
     * the deferred trend only starts once the reading phase finishes. */
    {
        render_scheduler_t low;
        render_scheduler_init(&low);
        render_scheduler_kick(&low);
        while (low.phase != RENDER_PHASE_IDLE)
            render_scheduler_complete_phase(&low);
        assert(render_scheduler_take_initial_complete(&low));

        render_scheduler_request_trend(&low);
        assert(low.phase == RENDER_PHASE_IDLE);
        render_scheduler_kick(&low);
        assert(low.phase == RENDER_PHASE_UPDATE_TREND_AXES);
        /* Restart the scenario with the trend still only pending. */
        render_scheduler_init(&low);
        render_scheduler_kick(&low);
        while (low.phase != RENDER_PHASE_IDLE)
            render_scheduler_complete_phase(&low);
        render_scheduler_request_trend(&low);
        assert(low.phase == RENDER_PHASE_IDLE);
        render_scheduler_request_regions(&low, RENDER_DIRTY_READING);
        assert(low.phase == RENDER_PHASE_UPDATE_READING);
        render_scheduler_complete_phase(&low);
        /* The flag survived: the deferred graph runs after the reading. */
        assert(low.phase == RENDER_PHASE_UPDATE_TREND_AXES);
        render_scheduler_complete_phase(&low);
        assert(low.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);
        render_scheduler_complete_phase(&low);
        assert(low.phase == RENDER_PHASE_IDLE);
    }

    /* Bounded trend slices: one column slice yields back to a waiting
     * reading update without abandoning or restarting the graph. The
     * remaining columns resume after the region phases complete. */
    {
        render_scheduler_t slice;
        render_scheduler_init(&slice);
        render_scheduler_kick(&slice);
        while (slice.phase != RENDER_PHASE_IDLE)
            render_scheduler_complete_phase(&slice);
        assert(render_scheduler_take_initial_complete(&slice));

        /* No pending work: a yield attempt keeps the scheduler untouched. */
        render_scheduler_request_trend(&slice);
        assert(slice.phase == RENDER_PHASE_IDLE);
        assert(!render_scheduler_yield_trend(&slice));
        render_scheduler_kick(&slice);
        assert(slice.phase == RENDER_PHASE_UPDATE_TREND_AXES);
        render_scheduler_complete_phase(&slice);
        assert(slice.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);

        /* Mid-graph reading arrival: yield hands control to the reading
         * band for one turn and re-flags the unfinished trend. This pass
         * already completed its axes phase, so the graph must resume
         * DIRECTLY at the columns phase. */
        render_scheduler_request_regions(&slice, RENDER_DIRTY_READING);
        assert(slice.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);
        assert(render_scheduler_yield_trend(&slice));
        assert(slice.phase == RENDER_PHASE_UPDATE_READING);
        render_scheduler_complete_phase(&slice);
        /* Regression (review C-1): re-entering the axes phase here would
         * erase every column already rendered in this pass while the
         * column cursor stays past them -- they could never be redrawn. */
        assert(slice.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);
        render_scheduler_complete_phase(&slice);
        assert(slice.phase == RENDER_PHASE_IDLE);

        /* Yield inside the axes phase resumes the axes phase itself (the
         * pass has not completed it yet), then flows on into columns. */
        render_scheduler_request_trend(&slice);
        render_scheduler_kick(&slice);
        assert(slice.phase == RENDER_PHASE_UPDATE_TREND_AXES);
        render_scheduler_request_regions(&slice,
                                         RENDER_DIRTY_STATUS |
                                         RENDER_DIRTY_READING);
        assert(render_scheduler_yield_trend(&slice));
        assert(slice.phase == RENDER_PHASE_UPDATE_STATUS);
        render_scheduler_complete_phase(&slice);
        assert(slice.phase == RENDER_PHASE_UPDATE_READING);
        render_scheduler_complete_phase(&slice);
        assert(slice.phase == RENDER_PHASE_UPDATE_TREND_AXES);
        render_scheduler_complete_phase(&slice);
        assert(slice.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);
        render_scheduler_complete_phase(&slice);
        assert(slice.phase == RENDER_PHASE_IDLE);
    }

    /* --- Review C-1 regression: a runtime FULL trend rebuild (axis
     * identity change) that yields to a reading mid-columns must resume at
     * the columns phase. Re-running UPDATE_TREND_AXES would replay the
     * background clear and erase columns already rendered in this pass;
     * with the column cursor past them they could never be redrawn. The
     * scheduler therefore carries frame-level state recording that this
     * pass finished its axes, cleared again at the pass boundary. --- */
    {
        render_scheduler_t full;
        render_scheduler_init(&full);
        assert(!full.trend_resume_at_columns);
        render_scheduler_kick(&full);
        while (full.phase != RENDER_PHASE_IDLE)
            render_scheduler_complete_phase(&full);
        assert(render_scheduler_take_initial_complete(&full));

        /* Full-rebuild pass: axes complete, then a partial column slice. */
        render_scheduler_request_trend(&full);
        render_scheduler_kick(&full);
        assert(full.phase == RENDER_PHASE_UPDATE_TREND_AXES);
        render_scheduler_complete_phase(&full);
        assert(full.trend_resume_at_columns);
        assert(full.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);

        render_scheduler_request_regions(&full, RENDER_DIRTY_READING);
        assert(render_scheduler_yield_trend(&full));
        assert(full.phase == RENDER_PHASE_UPDATE_READING);
        render_scheduler_complete_phase(&full);
        /* THE FIX UNDER TEST: columns continue; axes are NOT re-entered. */
        assert(full.phase == RENDER_PHASE_UPDATE_TREND_COLUMNS);
        assert(full.trend_resume_at_columns);
        render_scheduler_complete_phase(&full);
        assert(full.phase == RENDER_PHASE_IDLE);
        /* The pass boundary clears the frame-level resume point. */
        assert(!full.trend_resume_at_columns);

        /* A brand-new request therefore evaluates its axes again. */
        render_scheduler_request_trend(&full);
        render_scheduler_kick(&full);
        assert(full.phase == RENDER_PHASE_UPDATE_TREND_AXES);
    }
    return 0;
}
