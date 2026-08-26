#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "main_display.h"

/* Authoritative trend-axis stability rules, shared by the firmware snapshot
 * path (Core/Src/main.c) and the host regression tests.
 *
 *   new unit != resident unit        -> axis rebuild allowed immediately
 *   resident axis uninitialized      -> axis rebuild allowed once
 *   same unit and data inside axis   -> no axis rebuild
 *   same unit and data outside axis  -> mark axis candidate, no rebuild yet
 *   candidate persists >= timeout    -> axis rebuild allowed
 *
 * A rebuild requested while the ring is still filling is suppressed: the
 * range grows with every sample and auto-scaling would cross a 1/2/5
 * boundary every few seconds. Incremental column updates absorb that drift
 * until the window is full. */

#define TREND_AXIS_CANDIDATE_TIMEOUT_MS 2000u

/* Proposed replacement axis for the resident unit whose data left the
 * resident range; promoted only after TREND_AXIS_CANDIDATE_TIMEOUT_MS. */
typedef struct {
    bool valid;
    char unit[8];
    float step;
    float top;
    uint32_t first_seen_ms;
} trend_axis_candidate_t;

typedef struct {
    /* Project this frame onto the resident axis instead of the fresh
     * auto fit carried by the frame. */
    bool keep_resident;
    /* Grid+labels rebuild requested by the axis rules (already free of the
     * filling-window suppression). */
    bool axis_rebuild;
} trend_axis_verdict_t;

void trend_axis_init(main_display_trend_axis_t *resident,
                     trend_axis_candidate_t *candidate);

/* Evaluates the acceptance rules for one display snapshot. `frame` must
 * carry the auto-fitted identity plus the scaled window data bounds (as
 * produced by main_display_format_trend). Updates `candidate`; the caller
 * publishes the rendered frame identity into `resident` on every commit. */
trend_axis_verdict_t trend_axis_update(
    const main_display_trend_axis_t *resident,
    trend_axis_candidate_t *candidate, const main_display_frame_t *frame,
    float scale, bool window_full, uint32_t now_ms);
