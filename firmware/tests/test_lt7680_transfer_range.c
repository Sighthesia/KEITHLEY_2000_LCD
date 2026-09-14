#include <assert.h>
#include <stdint.h>

#include "lt7680_transfer_range.h"

int main(void)
{
    const uint32_t limit = 0x01000000u;

    assert(lt7680_validate_2d_source(0x00300000u, 128u, 80u, 44u, limit));
    assert(!lt7680_validate_2d_source(0x00FFF000u, 320u, 128u, 68u,
                                      limit));
    assert(!lt7680_validate_2d_source(0x00300000u, 79u, 80u, 44u, limit));
    assert(lt7680_validate_2d_destination(0x00100000u, 320u, 0u, 0u,
                                          128u, 68u, limit));
    /* This sample is in range under the specified SDRAM-only formula. */
    assert(lt7680_validate_2d_destination(0x00100000u, 320u, 0u, 892u,
                                          128u, 68u, limit));
    /* The exclusive end address may land exactly on the SDRAM limit. */
    assert(lt7680_validate_2d_destination(0x00FFFF00u, 128u, 0u, 0u,
                                           128u, 1u, limit));
    /* This destination really crosses the SDRAM limit. */
    assert(!lt7680_validate_2d_destination(0x00F70000u, 320u, 0u, 892u,
                                           128u, 68u, limit));

    /* The final row may use the stride's padding without exceeding SDRAM. */
    assert(lt7680_validate_2d_source(0u, 128u, 80u, 2u, 416u));
    assert(!lt7680_validate_2d_source(0u, 128u, 80u, 2u, 415u));
    assert(lt7680_validate_2d_destination(0u, 128u, 20u, 1u, 80u, 2u,
                                           0x0300u));
    assert(!lt7680_validate_2d_destination(0u, 128u, 20u, 1u, 80u, 2u,
                                            0x02C7u));
    /* A non-zero x must fit within each row's destination stride. */
    assert(!lt7680_validate_2d_destination(0u, 128u, 49u, 0u, 80u, 1u,
                                            limit));

    assert(!lt7680_validate_2d_source(0u, 0u, 1u, 1u, limit));
    assert(!lt7680_validate_2d_source(0u, 1u, 0u, 1u, limit));
    assert(!lt7680_validate_2d_source(0u, 1u, 1u, 0u, limit));
    assert(!lt7680_validate_2d_destination(0u, 0u, 0u, 0u, 1u, 1u,
                                           limit));
    assert(!lt7680_validate_2d_destination(0u, 1u, 0u, 0u, 0u, 1u,
                                           limit));
    assert(!lt7680_validate_2d_destination(0u, 1u, 0u, 0u, 1u, 0u,
                                           limit));

    /* Large coordinates must be rejected without 32-bit intermediate wrap. */
    assert(!lt7680_validate_2d_source(UINT32_MAX - 1u, 2u, 2u, 2u,
                                      UINT32_MAX));
    assert(!lt7680_validate_2d_destination(UINT32_MAX - 1u, 2u, 0u, 1u,
                                            2u, 2u, UINT32_MAX));

    return 0;
}
