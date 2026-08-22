#include <assert.h>
#include <string.h>

#include "rif_reader.h"

static void put16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

int main(void)
{
    uint8_t header[RIF_READER_HEADER_SIZE] = {0};
    uint8_t entry[RIF_READER_ENTRY_SIZE] = {0};
    rif_image_t image;
    rif_entry_t parsed;
    rif_tile_t tile;

    memcpy(header, "K2RF", 4u);
    put16(header + 4u, 1u);
    put16(header + 26u, RIF_READER_ENTRY_SIZE);
    put32(header + 8u, 0x0000C000u);
    put32(header + 16u, RIF_READER_HEADER_SIZE);
    put32(header + 20u, RIF_READER_ENTRY_SIZE);
    put16(header + 24u, 1u);
    put32(header + 28u, 0x1000u);
    assert(rif_reader_parse_header(header, sizeof(header), &image) == RIF_OK);

    memcpy(entry, "DGTC", 4u);
    put32(entry + 8u, 0x1000u);
    /* Transposed fb-orientation tile: 128x64, stride 256. */
    put32(entry + 12u, 16384u);
    put16(entry + 16u, 128u);
    put16(entry + 18u, 64u);
    put16(entry + 20u, 256u);
    entry[22] = '8';
    put16(entry + 28u, 0x07E6u);
    assert(rif_reader_parse_entry(&image, entry, sizeof(entry), &parsed) == RIF_OK);
    assert(rif_reader_find_glyph(&image, &parsed, RIF_KIND_DIGIT_CHAR, '8', &tile) == RIF_OK);
    assert(tile.offset == 0x1000u && tile.background == 0u);
    assert(rif_reader_find_glyph(&image, &parsed, RIF_KIND_DIGIT_CHAR, '9', &tile) == RIF_ERR_FORMAT);

    header[0] = 'X';
    assert(rif_reader_parse_header(header, sizeof(header), &image) == RIF_ERR_FORMAT);
    header[0] = 'K';
    put32(header + 36u, 1u);
    assert(rif_reader_parse_header(header, sizeof(header), &image) == RIF_ERR_RANGE);
    put32(header + 36u, 0u);
    assert(rif_reader_parse_header(header, sizeof(header), &image) == RIF_OK);
    put32(entry + 8u, 0x1001u);
    assert(rif_reader_parse_entry(&image, entry, sizeof(entry), &parsed) == RIF_ERR_RANGE);
    return 0;
}
