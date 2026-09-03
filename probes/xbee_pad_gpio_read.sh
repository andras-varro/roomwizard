#!/bin/sh
# xbee_pad_gpio_read.sh - can the UART3 RX ball read LOW? Ask a different peripheral.
# Runs ON the device. Needs /usr/local/bin/devmem_write. Socket may be empty or not.
#
# WHY A SECOND INSTRUMENT
# -----------------------
# The UART said the RX line is held high with the pad's pullup off, and that reading
# is identical on a unit with an EMPTY socket -- so it cannot be a module holding it.
# A meter then found NO resistor from J5 pin 2 to pin 1 or pin 10, so it is not a
# discrete pull at the socket either. Both of those looked at the ball through the
# UART. This looks at the SAME ball through the GPIO module instead: mux the pad to
# MODE4 and read its input latch.
#
# IT IDENTIFIES ITS OWN PIN, which is why it sweeps whole banks. The ball's GPIO
# number is not in the vanilla 4.14 tree (mach-omap2/mux34xx.c, which carried the
# ball-to-GPIO table, was removed upstream), so rather than guess it, every bank's
# DATAIN is read with the pad pulled UP and again pulled DOWN. Exactly one bit
# should follow the pull, and that bit IS this ball.
#
# ⚠️ GPIO BANKS 3-6 ARE CLOCK-GATED OFF ON A STOCK UNIT and a read of a gated OMAP
# register returns nothing rather than a value. The first version of this script did
# not notice: four of six banks came back EMPTY, every row of the table agreed, and
# that reads exactly like "no bit follows the pull" -- the wrong conclusion, from an
# instrument that was measuring two banks out of six. So the interface clocks are
# enabled here (CM_ICLKEN_PER bits 13-17 = gpio2..gpio6, from the vanilla tree's
# omap3xxx-clocks.dtsi reg 0x1010), restored on exit, and an unreadable bank is a
# LOUD failure that refuses to report.
#
# HOW TO READ THE RESULT
#   one bit follows UP,DOWN,UP  -> the ball tracks its own pull, so it CAN read low,
#                                 and the UART's "no break" needs re-examining
#   no bit anywhere follows     -> something holds this ball high regardless, on the
#                                 SoC side of the socket
# ⚠️ Other GPIOs move on their own (LEDs, the touch IRQ), so each state is read three
# times: this ball follows the whole UP,DOWN,UP sequence, not just one transition.
#
# Bank bases are omap3.dtsi's gpio1..gpio6; DATAIN is offset 0x38.

D=/usr/local/bin/devmem_write
PADCONF=0x4800219c              # upper halfword = uart3_rx_irrx (0x4800219e)
PAD_UP_M4=0x011c                # PIN_INPUT_PULLUP   | MUX_MODE4
PAD_DN_M4=0x010c                # PIN_INPUT_PULLDOWN | MUX_MODE4
CM_ICLKEN_PER=0x48005010
GPIO_ICK=0x3e000                # bits 13..17: gpio2_ick .. gpio6_ick
BANKS="0x48310038 0x49050038 0x49052038 0x49054038 0x49056038 0x49058038"
orig=""
ick_orig=""

[ -x "$D" ] || { echo "missing $D"; exit 2; }

r() {
  v=$($D "$1" | sed -n 's/.*current value = 0x\([0-9a-fA-F]*\).*/\1/p')
  [ -n "$v" ] || v=ERR
  printf '%s' "$v"
}
w() { $D "$1" "$2" >/dev/null 2>&1; }

restore() {
  [ -n "$orig" ] && w $PADCONF 0x$orig
  [ -n "$ick_orig" ] && [ "$ick_orig" != ERR ] && w $CM_ICLKEN_PER 0x$ick_orig
}
trap restore EXIT INT TERM

orig=$(r $PADCONF)
[ "$orig" = ERR ] && { echo "cannot read $PADCONF"; exit 1; }
echo "== UART3 RX ball, read through GPIO. padconf $PADCONF was 0x$orig =="

ick_orig=$(r $CM_ICLKEN_PER)
[ "$ick_orig" = ERR ] && { echo "cannot read $CM_ICLKEN_PER"; exit 1; }
w $CM_ICLKEN_PER $(printf "0x%08x" $(( 0x$ick_orig | GPIO_ICK )))
echo "   gpio2..6 interface clocks: CM_ICLKEN_PER 0x$ick_orig -> 0x$(r $CM_ICLKEN_PER)"

# Every bank must be readable, or the sweep is blind where it matters most.
bad=0
for b in $BANKS; do
  [ "$(r $b)" = ERR ] && { echo "   UNREADABLE: $b"; bad=$((bad+1)); }
done
if [ $bad -ne 0 ]; then
  echo "   $bad bank(s) unreadable even with the clocks on. Refusing to report:"
  echo "   a blind bank is indistinguishable from a bit that does not move."
  exit 1
fi
echo "   all six banks readable."
echo "   banks: 48310038 49050038 49052038 49054038 49056038 49058038"
echo

setpad() {  # setpad <halfword>
  w $PADCONF $(printf "0x%08x" $(( (0x$orig & 0x0000ffff) | ($1 << 16) )))
  got=$(( 0x$(r $PADCONF) >> 16 ))
  if [ $(( got )) -ne $(( $1 )) ]; then
    echo "   control FAILED: padconf reads $(printf 0x%04x $got), write did not land"
    exit 1
  fi
}

snap() {  # snap <label>
  n=1
  while [ $n -le 3 ]; do
    line=""
    for b in $BANKS; do line="$line $(r $b)"; done
    echo "   $1.$n$line"
    n=$((n+1))
  done
}

setpad $PAD_UP_M4; echo "-- pad = PULLUP   | MODE4 (verified)"; snap "UP1 "
setpad $PAD_DN_M4; echo "-- pad = PULLDOWN | MODE4 (verified)"; snap "DOWN"
setpad $PAD_UP_M4; echo "-- pad = PULLUP   | MODE4 again (verified)"; snap "UP2 "
echo
echo "== padconf and CM_ICLKEN_PER restored on exit. =="
echo "   A bit reading 1,0,1 across UP/DOWN/UP is this ball."
