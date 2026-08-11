#include "scene.h"

static const scene_t *s_scenes[SCENE_MGR_MAX_SCENES];
static uint8_t s_current;

void scene_mgr_init(void)
{
    uint8_t i;
    for (i = 0; i < SCENE_MGR_MAX_SCENES; i++) {
        s_scenes[i] = 0;
    }
    s_current = SCENE_MGR_NONE;
}

void scene_mgr_register(uint8_t id, const scene_t *s)
{
    if (id >= SCENE_MGR_MAX_SCENES) {
        return;
    }
    s_scenes[id] = s;
}

void scene_mgr_enter(uint8_t id)
{
    const scene_t *old;
    const scene_t *next;

    if (id >= SCENE_MGR_MAX_SCENES || s_scenes[id] == 0) {
        return;
    }
    if (s_current == id) {
        return;
    }
    old = (s_current != SCENE_MGR_NONE) ? s_scenes[s_current] : 0;
    next = s_scenes[id];
    if (old != 0 && old->exit != 0) {
        old->exit();
    }
    s_current = id;
    if (next->enter != 0) {
        next->enter();
    }
}

void scene_mgr_render(void)
{
    const scene_t *cur;

    if (s_current == SCENE_MGR_NONE) {
        return;
    }
    cur = s_scenes[s_current];
    if (cur != 0 && cur->render != 0) {
        cur->render();
    }
}

uint8_t scene_mgr_current_id(void)
{
    return s_current;
}
