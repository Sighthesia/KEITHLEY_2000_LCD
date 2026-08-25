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
    /* Frame-level phase state of the active trend pass: true once its
     * axes phase has completed, so any resume after a yield continues at
     * the columns phase instead of restarting the axes rebuild. Cleared
     * when the pass finishes. */
    bool trend_resume_at_columns;
    bool initial_complete;
    bool initial_complete_edge;
} render_scheduler_t;

void render_scheduler_init(render_scheduler_t *scheduler);
void render_scheduler_request_regions(render_scheduler_t *scheduler,
                                      uint8_t regions);
/* Flags one low-priority trend pass. Trend never preempts STATUS/READING:
 * the request only raises the flag, and the graph starts when the main
 * loop kicks an idle scheduler or when a running region phase completes. */
void render_scheduler_request_trend(render_scheduler_t *scheduler);
/* Starts queued work from IDLE: regions first, then the trend flag. */
void render_scheduler_kick(render_scheduler_t *scheduler);
/* Bounded-slice handoff: callable between trend slices. When region work
 * is pending, re-flags the unfinished trend and selects the higher-priority
 * phase so the main loop regains control; returns true when preempted.
 * Caller contract on true: reset the renderer's phase-local item cursor
 * before entering the selected region phase -- STATUS/READING initialize
 * their own item sequences at 0, and an interrupted axes phase replays
 * deterministically from item 0 (safe because no column of that pass
 * exists yet); a columns phase carries progress in its own column cursor,
 * which region phases never touch. A pass whose axes phase already
 * completed resumes directly at the columns phase -- a runtime axis
 * rebuild can never restart and erase columns it already rendered.
 * Region bits must be raised while the pass is active (review I-1): a
 * request that only fires while IDLE leaves every in-pass yield inert.
 * Wait bound for a pending status/reading region: at most one trend
 * slice -- <=8 column draws in COLUMNS, or one full AXES sequence
 * (~19 fast GE ops) because yielding mid-AXES would restart it from
 * item 0 on every preempt (livelock under sustained input); between
 * boundaries arriving snapshots wait coalesced. */
bool render_scheduler_yield_trend(render_scheduler_t *scheduler);
void render_scheduler_complete_phase(render_scheduler_t *scheduler);
bool render_scheduler_take_initial_complete(render_scheduler_t *scheduler);
