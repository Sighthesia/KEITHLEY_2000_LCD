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

## Verification

- `cd firmware && ./tests/run_tests.sh`: PASS
- `cmake --build KEITHLEY_2000_LCD/build/Release --target KEITHLEY_2000_LCD.elf`: PASS
- Release memory: RAM `16080 B / 20 KB` (`78.52%`), Flash `34616 B / 64 KB` (`52.82%`)
