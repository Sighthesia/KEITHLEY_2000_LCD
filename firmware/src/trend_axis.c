#include <string.h>

#include "trend_axis.h"

static float unit_scale(const char *unit)
{
    if (unit == 0) return 1.0f;
    if (unit[0] == 'm') return 1000.0f;
    if (unit[0] == 'u' || ((uint8_t)unit[0] == 0xC2u &&
                           (uint8_t)unit[1] == 0xB5u)) return 1000000.0f;
    if (unit[0] == 'k') return 0.001f;
    if (unit[0] == 'M' && unit[1] != '\0') return 0.000001f;
    return 1.0f;
}

static void unit_family(const char *unit, char *family, uint8_t size)
{
    const char *base = unit;
    uint8_t skip = 0u;

    if (unit != 0 && (unit[0] == 'm' || unit[0] == 'u' ||
                      unit[0] == 'k' || unit[0] == 'M')) skip = 1u;
    else if (unit != 0 && (uint8_t)unit[0] == 0xC2u &&
             (uint8_t)unit[1] == 0xB5u) skip = 2u;
    if (unit != 0) base = unit + skip;
    if (size == 0u) return;
    strncpy(family, base, size - 1u);
    family[size - 1u] = '\0';
}

static bool unit_matches(const main_display_trend_axis_t *resident,
                         const char *axis_unit)
{
    char resident_family[8];
    char axis_family[8];

    if (resident == 0 || !resident->valid || axis_unit == 0) return false;
    unit_family(resident->unit, resident_family, sizeof(resident_family));
    unit_family(axis_unit, axis_family, sizeof(axis_family));
    return strcmp(resident_family, axis_family) == 0;
}

/* Scaled window data bounds still fit inside the resident axis span
 * [top - 3*step .. top] (base units). Outside data only marks a candidate;
 * it never rebuilds on its own. */
static bool data_inside(const main_display_frame_t *frame,
                        const main_display_trend_axis_t *resident,
                        float scale)
{
    float resident_scale = unit_scale(resident->unit);
    float bottom = (resident->top - 3.0f * resident->step) / resident_scale;
    float top = resident->top / resident_scale;
    (void)scale;
    return frame->trend_minimum >= bottom && frame->trend_maximum <= top;
}

void trend_axis_init(main_display_trend_axis_t *resident,
                     trend_axis_candidate_t *candidate)
{
    if (resident != 0)
    {
        resident->valid = false;
        resident->unit[0] = '\0';
        resident->step = 0.0f;
        resident->top = 0.0f;
    }
    if (candidate != 0)
    {
        candidate->valid = false;
        candidate->unit[0] = '\0';
        candidate->step = 0.0f;
        candidate->top = 0.0f;
        candidate->first_seen_ms = 0u;
    }
}

trend_axis_verdict_t trend_axis_update(
    const main_display_trend_axis_t *resident,
    trend_axis_candidate_t *candidate, const main_display_frame_t *frame,
    float scale, bool window_full, uint32_t now_ms)
{
    trend_axis_verdict_t verdict = {false, false};
    bool same_unit;
    bool keep = false;

    if (candidate == 0 || frame == 0 || !frame->trend_has_data)
    {
        /* No data: nothing to rebuild and any old candidate is moot. The
         * caller's commit-time publication invalidates the resident axis
         * for this frame, so the next populated frame re-fits once. */
        if (candidate != 0) candidate->valid = false;
        return verdict;
    }
    same_unit = unit_matches(resident, frame->trend_axis_unit);
    if (!same_unit)
    {
        /* New unit (or uninitialized resident): rebuild is allowed
         * immediately; any old candidate is moot. */
        candidate->valid = false;
    }
    else if (data_inside(frame, resident, scale))
    {
        candidate->valid = false;
        keep = true;
    }
    else
    {
        /* Same unit, data outside the resident axis: mark a candidate but
         * do not rebuild yet. The candidate's geometry tracks the latest
         * auto fit; persistence is measured from the FIRST out-of-range
         * sighting. */
        if (!candidate->valid)
        {
            candidate->valid = true;
            candidate->first_seen_ms = now_ms;
        }
        strncpy(candidate->unit, frame->trend_axis_unit,
                sizeof(candidate->unit) - 1u);
        candidate->unit[sizeof(candidate->unit) - 1u] = '\0';
        candidate->step = frame->trend_axis_step;
        candidate->top = frame->trend_axis_top;
        if ((uint32_t)(now_ms - candidate->first_seen_ms) >=
            TREND_AXIS_CANDIDATE_TIMEOUT_MS)
        {
            /* Persisted: promote by rendering the fresh auto identity
             * already in the frame; the commit publishes it as the new
             * resident axis. */
            candidate->valid = false;
        }
        else
        {
            keep = true;
        }
    }
    verdict.keep_resident = keep;
    /* frame->trend_has_data is guaranteed above, so this mirrors the
     * production term `(trend_has_data && !keep_resident)`. */
    verdict.axis_rebuild = !keep;
    if (!window_full && same_unit)
    {
        /* Filling-window freeze: the range grows with every sample, so an
         * auto-scaling rebuild would cross a 1/2/5 boundary every few
         * seconds and each crossing costs a full grid+label rebuild. */
        verdict.axis_rebuild = false;
    }
    return verdict;
}
