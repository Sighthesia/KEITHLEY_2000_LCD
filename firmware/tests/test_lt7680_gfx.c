#include <assert.h>

#include "lt7680_gfx.h"

/* The geometry engine itself is LT7680 hardware, not testable on the host.
 * What is testable here is the coordinate/parameter construction logic:
 * invalid input (negative or out-of-panel coordinates, NULL array) must hit
 * the parameter-error branch before any bus access.  The host test never
 * calls lt7680_gfx_init, so s_panel.width/height stay 0 and every
 * non-negative coordinate is out of range. */
int main(void)
{
    uint8_t id[3];

    assert(lt7680_flash_read(0u, 0, 1u) == LT7680_ERR_PARAM);
    assert(lt7680_flash_read(0u, id, 0u) == LT7680_ERR_PARAM);
    assert(lt7680_flash_read(0x1000000u, id, 1u) == LT7680_ERR_PARAM);
    assert(lt7680_flash_read_jedec_id(0) == LT7680_ERR_PARAM);
    /* Out-of-range coordinates (uninitialized 0x0 panel on the host). */
    assert(lt7680_gfx_draw_line(0, 0, 1, 1, 0) == LT7680_ERR_PARAM);
    assert(lt7680_gfx_draw_line(5, 5, 10, 10, 0) == LT7680_ERR_PARAM);

    /* Negative coordinates are invalid regardless of panel size. */
    assert(lt7680_gfx_draw_line(-1, 0, 1, 1, 0) == LT7680_ERR_PARAM);
    assert(lt7680_gfx_draw_line(0, -1, 1, 1, 0) == LT7680_ERR_PARAM);
    assert(lt7680_gfx_draw_line(0, 0, -1, 1, 0) == LT7680_ERR_PARAM);
    assert(lt7680_gfx_draw_line(0, 0, 1, -1, 0) == LT7680_ERR_PARAM);

    /* Polyline: NULL array or fewer than 2 points. */
    assert(lt7680_gfx_draw_polyline(0, 2u, 0) == LT7680_ERR_PARAM);
    {
        const int16_t xy[2] = {0, 0};
        assert(lt7680_gfx_draw_polyline(xy, 1u, 0) == LT7680_ERR_PARAM);
    }

    /* Circle: negative radius or center pushed past the panel edge. */
    assert(lt7680_gfx_draw_circle(0, 0, -1, 0) == LT7680_ERR_PARAM);
    assert(lt7680_gfx_draw_circle(0, 0, 5, 0) == LT7680_ERR_PARAM);

    return 0;
}
