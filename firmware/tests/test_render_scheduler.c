#include <assert.h>

#include "render_scheduler.h"

int main(void)
{
    render_scheduler_t scheduler;

    render_scheduler_init(&scheduler);
    assert(scheduler.phase == RENDER_PHASE_INITIAL_STATUS);
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
     * columns, but it must never return to the full-clear initial phase. */
    render_scheduler_request_trend(&scheduler);
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
    return 0;
}
