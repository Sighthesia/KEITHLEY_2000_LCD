#include <assert.h>

#include "panel_transform.h"

int main(void)
{
    uint16_t fx, fy;

    panel_transform_init(960, 320, 320, 960);
    panel_transform_ui_to_fb(0, 0, &fx, &fy);
    assert(fx == 0 && fy == 0);
    panel_transform_ui_to_fb(959, 319, &fx, &fy);
    assert(fx == 319 && fy == 959);
    panel_transform_ui_to_fb(100, 200, &fx, &fy);
    assert(fx == 200 && fy == 100);
    assert(panel_transform_ui_width() == 960);
    assert(panel_transform_ui_height() == 320);
    assert(panel_transform_fb_width() == 320);
    assert(panel_transform_fb_height() == 960);

    /* fb_to_ui is the exact inverse of the transpose. */
    panel_transform_fb_to_ui(200, 100, &fx, &fy);
    assert(fx == 100 && fy == 200);

    /* Future physical-landscape panel: the same UI space maps directly. */
    panel_transform_init(960, 320, 960, 320);
    panel_transform_ui_to_fb(100, 200, &fx, &fy);
    assert(fx == 100 && fy == 200);
    panel_transform_fb_to_ui(100, 200, &fx, &fy);
    assert(fx == 100 && fy == 200);

    panel_transform_init(960, 320, 320, 960);
    {
        uint16_t fw, fh;
        panel_transform_ui_rect_to_fb(10, 20, 100, 30, &fx, &fy, &fw, &fh);
        assert(fx == 20 && fy == 10 && fw == 30 && fh == 100);
    }

    /* Invalid dimensions: init rejected, transforms are safe no-ops. */
    panel_transform_init(0, 320, 320, 960);
    panel_transform_ui_to_fb(5, 5, &fx, &fy);
    assert(fx == 0 && fy == 0);
    assert(panel_transform_ui_width() == 0);

    /* Mismatched transpose dims are rejected too. */
    panel_transform_init(960, 320, 320, 320);
    panel_transform_ui_to_fb(5, 5, &fx, &fy);
    assert(fx == 0 && fy == 0);

    /* NULL output pointers must not crash. */
    panel_transform_init(960, 320, 320, 960);
    panel_transform_ui_to_fb(100, 200, 0, 0);
    panel_transform_fb_to_ui(100, 200, 0, 0);

    return 0;
}
