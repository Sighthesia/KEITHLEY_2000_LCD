# Task 3 Report

## Result

`rif_tile_cache_prepare()` now uses one cleanup path for every failure after a
successful Canvas snapshot. Cleanup always attempts both saved CVSSA and Canvas
stride restores, keeps `entry->ready` cleared, and returns the primary failure.
Successful metadata population and cache address advancement remain unchanged.

The host mirror in `firmware/src` is synchronized with the CubeMX source:

```text
diff -q firmware/src/rif_tile_cache.c KEITHLEY_2000_LCD/Core/Src/rif_tile_cache.c
```

## Failure Coverage

`firmware/tests/test_rif_tile_cache.c` injects failures at:

- Flash read
- Canvas base write
- Canvas width write
- Pixel write
- CRC/completion validation

Each case verifies the saved base (`0x00123456`), saved stride (`320`), and
`entry.ready == 0`. A successful build also verifies the existing cache
metadata and first cache address.

The failure-injection mock now records Canvas base/width call order. The
pixel-write failure reaches both temporary Canvas settings before cleanup and
asserts exactly two base calls and two width calls: one cache setup pair,
followed by the saved CVSSA/stride restore pair. The CRC/completion failure
asserts the final restore pair; because the implementation processes the tile
in multiple DMA chunks, its total setup-call count is intentionally not fixed
at two. The five failure classes remain covered; paths that fail before a
Canvas setting is reached are checked for the saved restore values without
claiming an operation that did not occur.

Restore error precedence is also covered: a restore-only CVSSA failure is
returned after a successful primary operation; a primary failure remains the
returned error when CVSSA restore also fails; and simultaneous CVSSA/width
restore failures return the CVSSA error, matching the implementation contract.
These priority cases assert that the final Canvas base/width events are the
cleanup restores; successful primary work may configure Canvas once per DMA
chunk, so these cases intentionally assert the terminal restore pair rather
than a fixed total count.

## Verification

- `cd firmware && ./tests/run_tests.sh`: PASS
- `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`: PASS
- Release memory: RAM `16080 B / 20 KB` (`78.52%`), Flash `34616 B / 64 KB` (`52.82%`)
