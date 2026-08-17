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

    /* FIFO probe: getter returns zeroed state before any flash transaction. */
    {
        lt7680_flash_fifo_probe_t fp;
        lt7680_flash_get_fifo_probe(&fp);
        assert(fp.attempted == 0u);
        assert(fp.full_count == 0u);
        assert(fp.status_err == 0u);
        assert(fp.last_status == 0u);
    }
    /* Getter with NULL is safe. */
    lt7680_flash_get_fifo_probe(0);
    /* JEDEC probe: getter returns zeroed state before any flash transaction. */
    {
        lt7680_flash_jedec_probe_t jp;
        lt7680_flash_get_jedec_probe(&jp);
        assert(jp.attempted == 0u);
        assert(jp.raw[0] == 0u);
        assert(jp.raw[1] == 0u);
        assert(jp.raw[2] == 0u);
        assert(jp.raw[3] == 0u);
        assert(jp.status == LT7680_ERR_BUS);
    }
    /* JEDEC probe getter with NULL is safe. */
    lt7680_flash_get_jedec_probe(0);
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
