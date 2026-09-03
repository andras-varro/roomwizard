#!/bin/sh
# xbee_socket_continuity.sh - does J5 actually reach UART3? A meter + the UART.
# Runs ON the device. Needs /usr/local/bin/devmem_write. RUN WITH THE SOCKET EMPTY.
#
# WHY THIS EXISTS
# ---------------
# Every software result so far is consistent with a socket that reaches nothing:
# the AT and API sweeps are silent, and the pad-pulldown stage was measured to be
# uninformative -- a unit with an EMPTY socket reads LSR 0x60 with no break, the
# same as the units carrying a module, because something on the DOUT net outpulls
# the SoC's internal pulldown. So the link itself has never been established.
#
# The SoC is a BGA, so its pads cannot be probed. Instead each direction is tested
# with the UART at the far end, which needs no BGA access:
#
#   DIN  (J5 pin 3, the SoC's TX): the SoC drives it. LCR bit 6 holds TX LOW, so a
#        meter on pin 3 reads ~3.3 V idle and ~0 V during the hold. It is the SAME
#        pin measured in two states, so only the drive changes -- a reading that
#        moves proves continuity, and one that does not proves the trace is open
#        (or the meter is on the wrong pin, which is why both readings are taken).
#
#   DOUT (J5 pin 2, the SoC's RX): the operator drives it. With the pad set to
#        PIN_INPUT_PULLDOWN the net still sits high, so pulling pin 2 down through
#        a resistor should give the UART a continuous space and latch LSR BI.
#
# CONTROLS
#   * the break detector must be seen firing before either phase is believed: a
#     forced break under internal loopback must set BI.
#   * phase 2 takes a BASELINE with the pad already flipped and nothing pulled.
#     That baseline must read NO break, or the phase cannot attribute one.
# ⚠️ Comparisons use $(( )) on both sides: busybox [ -ne ] REJECTS hex operands and
# its error status silently takes the else branch.

D=/usr/local/bin/devmem_write
CM_FCLKEN_PER=0x48005000
FCLK_UART3_ON=0xc00
FCLK_RESTORE=0x400
DLL=0x49020000; DLH=0x49020004; IER=0x49020004
FCR=0x49020008; EFR=0x49020008
LCR=0x4902000c; MCR=0x49020010; LSR=0x49020014
MDR1=0x49020020; RHR=0x49020000
PADCONF_RX=0x4800219c
PAD_RX_PULLDOWN=0x0108
padconf_orig=""

[ -x "$D" ] || { echo "missing $D -- deploy it first"; exit 2; }

r() { $D "$1" | sed -n 's/.*current value = 0x\([0-9a-fA-F]*\).*/\1/p'; }
w() { $D "$1" "$2" >/dev/null 2>&1; }
cleanup() {
  w $LCR 0x03; w $MCR 0x03; w $MDR1 0x7
  [ -n "$padconf_orig" ] && w $PADCONF_RX 0x$padconf_orig
  w $CM_FCLKEN_PER $FCLK_RESTORE
}
trap cleanup EXIT INT TERM

w $CM_FCLKEN_PER $FCLK_UART3_ON
padconf_orig=$(r $PADCONF_RX)

# 9600 8N1: longest bit time, most robust break detection.
w $MDR1 0x7
w $LCR 0xbf; w $EFR 0x10
w $LCR 0x00; w $IER 0x00
w $LCR 0xbf; w $DLL 0x38; w $DLH 0x01
w $LCR 0x03; w $MCR 0x03; w $FCR 0x07; w $MDR1 0x00

echo "== J5 <-> UART3 continuity.  RUN THIS WITH THE SOCKET EMPTY. =="
echo "   J5 pin 1 is the DOTTED end. Pins 1-10 run down J5; pin 10 is GND."
echo

echo "== control: can this UART detect a break at all? =="
w $MCR 0x13; w $LCR 0x43
sleep 1; r $LSR >/dev/null; sleep 1
bi=$(( 0x$(r $LSR) & 0x10 ))
w $LCR 0x03; w $MCR 0x03
if [ $(( bi )) -eq 0 ]; then
  echo "   FAILED: a forced break under loopback did not set BI. Nothing below"
  echo "   could be attributed. Stopping."
  exit 1
fi
echo "   OK: forced break set BI, so the detector fires."
echo

echo "== PHASE 1 -- DIN / J5 pin 3, driven by the SoC =="
echo "   Put a DC voltmeter on J5 pin 3, black lead on pin 10 (GND)."
echo "   TX is now IDLE (should be HIGH). Holding 30 s -- read it now."
w $LCR 0x03
sleep 30
echo "   Now driving TX LOW for 30 s -- read pin 3 again."
w $LCR 0x43
sleep 30
w $LCR 0x03
echo "   Released. Two readings: ~3.3 V then ~0 V means pin 3 reaches the SoC."
echo "   Both the same means the trace does not reach this pad."
echo

echo "== PHASE 2 -- DOUT / J5 pin 2, driven by you =="
w $PADCONF_RX $(printf "0x%08x" $(( (0x$padconf_orig & 0x0000ffff) \
                                    | ($PAD_RX_PULLDOWN << 16) )))
got=$(( 0x$(r $PADCONF_RX) >> 16 ))
if [ $(( got )) -ne $(( PAD_RX_PULLDOWN )) ]; then
  echo "   FAILED: padconf write did not land (reads $(printf 0x%04x $got))."
  exit 1
fi
echo "   RX pad set to PIN_INPUT_PULLDOWN, verified by read-back."
r $LSR >/dev/null; sleep 2
base=$(r $LSR)
echo "   baseline with nothing pulled: LSR $base"
if [ $(( 0x$base & 0x10 )) -ne 0 ]; then
  echo "   BASELINE ALREADY SHOWS A BREAK -- this phase cannot attribute one."
  exit 1
fi
echo "   Baseline is clean (no break), so a break from here is yours."
echo
echo "   NOW: connect J5 pin 2 to GND through a 330 ohm - 1 kohm resistor."
echo "   ⚠️ Pin 2 is right next to pin 1, which is a LIVE 3.3 V rail. Take GND"
echo "   from pin 10 at the far end of J5, and do not let the lead slip onto pin 1."
echo "   Watching for 45 s..."
hit=0
n=0
while [ $n -lt 45 ]; do
  l=$(r $LSR)
  if [ $(( 0x$l & 0x10 )) -ne 0 ]; then
    echo "   BREAK at ~${n}s (LSR $l) -- pin 2 REACHES the SoC's RX pad."
    hit=1; break
  fi
  sleep 1
  n=$((n+1))
done
[ $hit -eq 0 ] && echo "   no break in 45 s -- pin 2 did not reach the pad."
echo
echo "== done; UART3 clock gate and the RX pad are restored on exit. =="
