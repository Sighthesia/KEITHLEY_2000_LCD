#include "rif_reader.h"

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static uint8_t is_aligned(uint32_t value)
{
    return (value & (RIF_READER_ALIGNMENT - 1u)) == 0u;
}

rif_status_t rif_reader_parse_header(const uint8_t *data, uint16_t len,
                                     rif_image_t *out)
{
    uint32_t directory_size;

    if (data == 0 || out == 0 || len < RIF_READER_HEADER_SIZE) {
        return RIF_ERR_PARAM;
    }
    if (data[0] != 'K' || data[1] != '2' || data[2] != 'R' || data[3] != 'F' ||
        read_le16(data + 4u) != 1u || read_le16(data + 6u) != 0u ||
        read_le16(data + 26u) != RIF_READER_ENTRY_SIZE) {
        return RIF_ERR_FORMAT;
    }

    out->image_size = read_le32(data + 8u);
    out->directory_offset = read_le32(data + 16u);
    directory_size = read_le32(data + 20u);
    out->directory_count = read_le16(data + 24u);
    out->directory_entry_size = read_le16(data + 26u);
    out->payload_offset = read_le32(data + 28u);
    out->flash_base = read_le32(data + 36u);

    if (!is_aligned(out->flash_base) || out->image_size > RIF_READER_FLASH_SIZE ||
        out->flash_base > RIF_READER_FLASH_SIZE - out->image_size ||
        out->directory_offset != RIF_READER_HEADER_SIZE ||
        directory_size != (uint32_t)out->directory_count * RIF_READER_ENTRY_SIZE ||
        out->payload_offset < out->directory_offset + directory_size ||
        !is_aligned(out->payload_offset) || out->payload_offset > out->image_size) {
        return RIF_ERR_RANGE;
    }
    return RIF_OK;
}

rif_status_t rif_reader_parse_entry(const rif_image_t *image,
                                    const uint8_t *data, uint16_t len,
                                    rif_entry_t *out)
{
    if (image == 0 || data == 0 || out == 0 || len < RIF_READER_ENTRY_SIZE) {
        return RIF_ERR_PARAM;
    }

    out->kind = read_le32(data);
    out->id = read_le32(data + 4u);
    out->offset = read_le32(data + 8u);
    out->size = read_le32(data + 12u);
    out->width = read_le16(data + 16u);
    out->height = read_le16(data + 18u);
    out->stride = read_le16(data + 20u);
    out->code = data[22u];
    out->crc32 = read_le32(data + 24u);
    out->foreground = read_le16(data + 28u);
    out->background = read_le16(data + 30u);

    if (!is_aligned(out->offset) || out->offset < image->payload_offset ||
        out->offset > image->image_size || out->size > image->image_size - out->offset) {
        return RIF_ERR_RANGE;
    }
    return RIF_OK;
}

rif_status_t rif_reader_find_glyph(const rif_image_t *image,
                                   const rif_entry_t *entry, uint32_t kind,
                                   uint16_t code, rif_tile_t *out)
{
    if (image == 0 || entry == 0 || out == 0) {
        return RIF_ERR_PARAM;
    }
    if (entry->kind != kind || entry->code != code) {
        return RIF_ERR_FORMAT;
    }
    if (entry->width != 64u || entry->height != 128u ||
        entry->stride != 128u || entry->size != 16384u ||
        entry->offset > image->image_size - entry->size) {
        return RIF_ERR_FORMAT;
    }
    out->offset = image->flash_base + entry->offset;
    out->size = entry->size;
    out->width = entry->width;
    out->height = entry->height;
    out->stride = entry->stride;
    out->foreground = entry->foreground;
    out->background = entry->background;
    return RIF_OK;
}
