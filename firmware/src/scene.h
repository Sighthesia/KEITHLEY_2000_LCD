#pragma once

#include <stdint.h>

/* Independent full-screen scene framework (ADR-0002). At most
 * SCENE_MGR_MAX_SCENES scenes can be registered, one per id. */
#define SCENE_MGR_MAX_SCENES 4u
#define SCENE_MGR_NONE 0xFFu

typedef struct {
    void (*enter)(void);
    void (*exit)(void);
    void (*render)(void);
} scene_t;

void scene_mgr_init(void);
void scene_mgr_register(uint8_t id, const scene_t *s);
void scene_mgr_enter(uint8_t id);
void scene_mgr_render(void);
uint8_t scene_mgr_current_id(void);
