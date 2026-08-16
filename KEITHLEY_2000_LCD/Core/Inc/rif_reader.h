#pragma once

#include <stdint.h>

#define RIF_READER_HEADER_SIZE 64u
#define RIF_READER_ENTRY_SIZE 48u
#define RIF_READER_ALIGNMENT 4096u
#define RIF_READER_FLASH_SIZE 0x00800000u

#define RIF_KIND_DIGIT_CHAR 0x43544744u
#define RIF_KIND_DIGIT_SYMBOL 0x53544744u

typedef enum {
    RIF_OK = 0,
    RIF_ERR_PARAM,
    RIF_ERR_FORMAT,
    RIF_ERR_RANGE
} rif_status_t;

typedef struct {
    uint32_t flash_base;
    uint32_t image_size;
    uint32_t directory_offset;
    uint32_t payload_offset;
    uint16_t directory_count;
    uint16_t directory_entry_size;
} rif_image_t;

typedef struct {
    uint32_t kind;
    uint32_t id;
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t code;
    uint16_t foreground;
    uint16_t background;
} rif_entry_t;

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t foreground;
    uint16_t background;
} rif_tile_t;

rif_status_t rif_reader_parse_header(const uint8_t *data, uint16_t len,
                                     rif_image_t *out);
rif_status_t rif_reader_parse_entry(const rif_image_t *image,
                                    const uint8_t *data, uint16_t len,
                                    rif_entry_t *out);
rif_status_t rif_reader_find_glyph(const rif_image_t *image,
                                   const rif_entry_t *entry, uint32_t kind,
                                   uint16_t code, rif_tile_t *out);
