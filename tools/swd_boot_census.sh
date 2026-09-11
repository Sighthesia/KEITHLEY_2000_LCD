#!/usr/bin/env bash
# HITL boot-transient profiler: waits for a fresh power-on (present_total
# returns to ~0), then samples three post-boot windows automatically.
# Usage: start this FIRST, then power-cycle the panel.
set -eu
cd "$(dirname "$0")/.."
ELF=KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf

sym() { arm-none-eabi-nm "$ELF" | awk -v s="$1" '$3==s {print "0x"$1}'; }
A_TICK=$(sym uwTick); A_PRESENT=$(sym s_present_count_total)
A_PASS=$(sym s_reading_gate_pass); A_DROP=$(sym s_reading_gate_drop)
A_BAND=$(sym s_reading_band_fills); A_NODATA=$(sym s_no_data_frames)

sample() {
    openocd -f openocd.cfg -c "init" -c "halt" \
        -c "mdw $A_TICK 1" -c "mdw $A_PRESENT 1" \
        -c "mdw $A_PASS 1" -c "mdw $A_DROP 1" \
        -c "mdw $A_BAND 1" -c "mdw $A_NODATA 1" \
        -c "resume" -c "shutdown" 2>&1 | grep "^0x" | awk '{print $2}'
}

BASE_PRESENT=$(sample | sed -n 2p)
BASE=$((16#$BASE_PRESENT))
echo "armed (present_total baseline=$BASE). Waiting for power-cycle..."

# Poll until the counter resets to below baseline (fresh boot) then climbs.
while :; do
    P=$(sample | sed -n 2p)
    [ $((16#$P)) -lt $BASE ] && break
    sleep 2
done
echo "boot detected -- window A (0-10 s post-detect)"
A0=$(sample); sleep 10; A1=$(sample)

sleep 20
echo "window B (~30-40 s)"
B0=$(sample); sleep 10; B1=$(sample)

sleep 50
echo "window C (~90-100 s)"
C0=$(sample); sleep 10; C1=$(sample)

report() {
    paste <(echo "$1") <(echo "$2") | awk -v names="tick present pass drop band nodata" -v tag="$3" '
    BEGIN { split(names, n, " ") }
    {
        d = strtonum("0x" $2) - strtonum("0x" $1)
        printf "%s %-8s delta=%d (%.1f/s)\n", tag, n[NR], d, d/10
    }'
}
echo
report "$A0" "$A1" "[A]"
report "$B0" "$B1" "[B]"
report "$C0" "$C1" "[C]"
