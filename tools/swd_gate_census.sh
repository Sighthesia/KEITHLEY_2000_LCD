#!/usr/bin/env bash
# SWD census sampler: two samples 10 s apart, hex-diffed into rates.
# Run right after a power-on to profile the boot transient:
#   pass/drop  -- host snapshot accepts/rejects. pass = records accepted
#                into the snapshot (value+unit stored whole); drop = lines
#                rejected by the reading filter (labels, placeholders,
#                --.----- rotations) plus duplicate records (VFD redraws,
#                empty-field re-reads). NOT a unit gate anymore: the old
#                unit-settle hysteresis was removed (2026-09-11) because
#                it starved commits during AUTO range hunting.
#   band/nodata-- erased glyphs / no_data frames (placeholder churn)
#   present    -- commits (0 = black panel)
set -eu
cd "$(dirname "$0")/.."
ELF=KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf

sym() { arm-none-eabi-nm "$ELF" | awk -v s="$1" '$3==s {print "0x"$1}'; }
A_TICK=$(sym uwTick)
A_PRESENT=$(sym s_present_count_total)
A_PASS=$(sym s_reading_gate_pass)
A_DROP=$(sym s_reading_gate_drop)
A_BAND=$(sym s_reading_band_fills)
A_NODATA=$(sym s_no_data_frames)

sample() {
    openocd -f openocd.cfg -c "init" -c "halt" \
        -c "mdw $A_TICK 1" -c "mdw $A_PRESENT 1" \
        -c "mdw $A_PASS 1" -c "mdw $A_DROP 1" \
        -c "mdw $A_BAND 1" -c "mdw $A_NODATA 1" \
        -c "resume" -c "shutdown" 2>&1 | grep "^0x" | awk '{print $2}'
}

echo "sampling t0..."; T0=$(sample)
sleep 10
echo "sampling t1..."; T1=$(sample)

paste <(echo "$T0") <(echo "$T1") | awk -v names="tick present pass drop band nodata" '
BEGIN { split(names, n, " ") }
{
    d = strtonum("0x" $2) - strtonum("0x" $1)
    printf "%-8s t0=%-9s t1=%-9s delta=%d%s\n", n[NR], $1, $2, d,
        (NR == 1 ? " (10000 ms window)" : " (" sprintf("%.1f", d/10) "/s)")
}'
