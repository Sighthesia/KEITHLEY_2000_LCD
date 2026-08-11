#include <assert.h>

#include "scene.h"

static int s_enter0, s_exit0, s_render0;
static int s_enter1, s_exit1, s_render1;

static void enter0(void) { s_enter0++; }
static void exit0(void) { s_exit0++; }
static void render0(void) { s_render0++; }
static void enter1(void) { s_enter1++; }
static void exit1(void) { s_exit1++; }
static void render1(void) { s_render1++; }

static const scene_t s_scene0 = { enter0, exit0, render0 };
static const scene_t s_scene1 = { enter1, exit1, render1 };

int main(void)
{
    scene_mgr_init();

    /* Empty manager is safe and has no current scene. */
    assert(scene_mgr_current_id() == SCENE_MGR_NONE);
    scene_mgr_render();

    scene_mgr_register(0, &s_scene0);
    scene_mgr_register(1, &s_scene1);
    scene_mgr_register(4, &s_scene0);   /* out of range, ignored */

    scene_mgr_enter(0);
    assert(s_enter0 == 1 && s_exit0 == 0);
    assert(scene_mgr_current_id() == 0);
    scene_mgr_render();
    assert(s_render0 == 1 && s_render1 == 0);

    /* Re-entering the current scene must not double-enter. */
    scene_mgr_enter(0);
    assert(s_enter0 == 1 && s_exit0 == 0);

    /* Switching runs exit(old) then enter(new). */
    scene_mgr_enter(1);
    assert(s_exit0 == 1 && s_enter1 == 1);
    assert(scene_mgr_current_id() == 1);
    scene_mgr_render();
    assert(s_render1 == 1 && s_render0 == 1);

    /* Unregistered / out-of-range ids are no-ops. */
    scene_mgr_enter(7);
    assert(scene_mgr_current_id() == 1);

    /* A NULL scene slot is safe to register and enter: a no-op. */
    scene_mgr_register(2, 0);
    scene_mgr_enter(2);
    assert(scene_mgr_current_id() == 1);
    scene_mgr_render();

    /* Registering NULL over a live slot clears it; entering is a no-op. */
    scene_mgr_register(1, 0);
    scene_mgr_enter(1);
    assert(scene_mgr_current_id() == 1);
    scene_mgr_render();

    return 0;
}
