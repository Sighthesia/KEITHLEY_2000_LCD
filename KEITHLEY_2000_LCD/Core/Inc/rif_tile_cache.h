#pragma once

#include <stdint.h>

#include "lt7680_bus.h"
#include "rif_reader.h"

typedef struct {
    uint32_t kind;
    uint16_t code;
    uint32_t address;
    uint16_t stride;
    uint16_t width;
    uint16_t height;
    uint8_t ready;
} rif_tile_cache_entry_t;

void rif_tile_cache_init(void);
lt7680_status_t rif_tile_cache_lookup(uint32_t kind, uint16_t code,
                                      rif_tile_cache_entry_t *entry);
lt7680_status_t rif_tile_cache_prepare(uint32_t kind, uint16_t code,
                                        const rif_tile_t *tile,
                                        rif_tile_cache_entry_t *entry);
