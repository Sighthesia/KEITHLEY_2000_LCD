#!/usr/bin/env bash
# SWD display-health loop: does the panel ever commit a frame?
#
#   tools/swd_display_loop.sh            # sample running firmware
#   tools/swd_display_loop.sh --flash    # flash first, then sample
#
# RED   = zero successful present_page calls after the settle window
#         (boot-blank keeps the panel dark; zero commits == black screen).
# GREEN = at least one commit (the panel has been lit).
#
# s_present_count_total is monotonic -- never window-reset, unlike
# s_perf_display_commits_window (which the silent build zeroes every loop).
set -u
ELF="KEITHLEY_2000_LCD/build/Release/KEITHLEY_2000_LCD.elf"
SETTLE="${SETTLE:-12}"

cd "$(dirname "$0")/.." || exit 1

if [ "${1:-}" = "--flash" ]; then
    echo "== flashing =="
    openocd -f openocd.cfg \
        -c "init" -c "halt" \
        -c "program $ELF verify" \
        -c "reset" -c "shutdown" 2>&1 | grep -E "Programming|Error" | head -3
fi

sym() { arm-none-eabi-nm "$ELF" | awk -v s="$1" '$3==s {print "0x"$1}'; }

A_PRESENT=$(sym s_present_count_total)
A_STAGE=$(sym s_reading_only_stage)
A_DIRTY=$(sym s_reading_only_dirty)
A_FR=$(sym s_frame_rendering)
A_INIT=$(sym s_initial_page_pending)
A_DISP=$(sym s_display_due_tick)
A_TEMP=$(sym s_temperature_tick)
A_TICK=$(sym uwTick)

echo "== reset + settle ${SETTLE}s =="
openocd -f openocd.cfg -c "init" -c "reset" -c "sleep 200" -c "resume" \
    -c "shutdown" >/dev/null 2>&1
sleep "$SETTLE"

OUT=$(openocd -f openocd.cfg -c "init" -c "halt" \
    -c "mdw $A_TICK 1" \
    -c "mdw $A_PRESENT 1" \
    -c "mdb $A_STAGE 1" \
    -c "mdb $A_DIRTY 1" \
    -c "mdb $A_FR 1" \
    -c "mdb $A_INIT 1" \
    -c "mdw $A_DISP 1" \
    -c "mdw $A_TEMP 1" \
    -c "resume" -c "shutdown" 2>/dev/null)

echo "$OUT" | grep "^0x"

TICK=$(echo "$OUT" | awk -v a="$A_TICK" '$1==a {print $2}')
PRESENT=$(echo "$OUT" | awk -v a="$A_PRESENT" '$1==a {print $2}')
STAGE=$(echo "$OUT" | awk -v a="$A_STAGE" '$1==a {print $2}')
DIRTY=$(echo "$OUT" | awk -v a="$A_DIRTY" '$1==a {print $2}')
FR=$(echo "$OUT" | awk -v a="$A_FR" '$1==a {print $2}')
INIT=$(echo "$OUT" | awk -v a="$A_INIT" '$1==a {print $2}')
DUE=$(echo "$OUT" | awk -v a="$A_DISP" '$1==a {print $2}')
TEMP=$(echo "$OUT" | awk -v a="$A_TEMP" '$1==a {print $2}')

echo
echo "tick=$TICK present_total=$PRESENT stage=$STAGE dirty=$DIRTY frame_rendering=$FR init_pending=$INIT"
if [ -n "$TICK" ] && [ -n "$DUE" ]; then
    echo "display_due_age=$(( 0x$TICK - 0x$DUE ))ms  temperature_age=$(( 0x$TICK - 0x$TEMP ))ms"
fi

if [ "${PRESENT:-0}" -gt 0 ] 2>/dev/null; then
    echo "VERDICT: GREEN ($PRESENT commits -- panel has been lit)"
else
    echo "VERDICT: RED (zero commits -- panel can only be black)"
fi
