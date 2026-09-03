#!/bin/sh
# xbee_uart_break_semantics.sh - what does LSR BI actually mean on this UART?
# Runs ON the device. Needs /usr/local/bin/devmem_write. Touches no pads and no
# socket: everything below happens under the UART's INTERNAL loopback, so it is a
# pure instrument check and is safe with a module fitted or absent.
#
# WHY THIS EXISTS
# ---------------
# Every J5 continuity result is read off LSR bit 4 (BI), and on 2026-09-02 that bit
# was measured giving BOTH a false positive and a false negative within an hour:
#   * BI stayed SET while the pad's own pullup drove the ball high  -> "the net is
#     held low", which would have been written down as a socket fact.
#   * BI read CLEAR while a break was actively being driven under loopback -> "the
#     detector does not fire", which stops the probe dead.
# The suspected cause is that LSR's error bits describe the character at the HEAD
# of the RX FIFO rather than the live line, so they neither clear while a break
# char sits at the head nor reload while the FIFO is full. That is a hypothesis
# about this UART, and this script is the measurement that settles it.
#
# WHAT IT PRINTS
# Four states, each sampled several times, with the RX FIFO level (RXFIFO_LVL) and
# DR beside BI so a stuck flag can be told apart from a genuine one:
#   A  break ON,  FIFO freshly reset      -- BI must become SET
#   B  break ON,  FIFO left to fill       -- does BI still read SET when full?
#   C  break OFF, FIFO NOT reset          -- the false-positive case: BI should be
#                                            clear but a stale head would hold it
#   D  break OFF, FIFO reset first        -- BI must read CLEAR
# A and D are the two controls. If either fails, no J5 conclusion drawn from BI on
# this unit can be trusted, whatever the socket does.

D=/usr/local/bin/devmem_write
CM_FCLKEN_PER=0x48005000
FCLK_UART3_ON=0xc00
FCLK_RESTORE=0x400
DLL=0x49020000; DLH=0x49020004; IER=0x49020004; RHR=0x49020000
FCR=0x49020008; EFR=0x49020008
LCR=0x4902000c; MCR=0x49020010; LSR=0x49020014
MDR1=0x49020020
RXFIFO_LVL=0x49020064          # UART_RXFIFO_LVL, OMAP-specific, read-only

[ -x "$D" ] || { echo "missing $D -- deploy it first"; exit 2; }
r() { $D "$1" | sed -n 's/.*current value = 0x\([0-9a-fA-F]*\).*/\1/p'; }
w() { $D "$1" "$2" >/dev/null 2>&1; }
cleanup() { w $LCR 0x03; w $MCR 0x03; w $MDR1 0x7; w $CM_FCLKEN_PER $FCLK_RESTORE; }
trap cleanup EXIT INT TERM

w $CM_FCLKEN_PER $FCLK_UART3_ON
w $MDR1 0x7
w $LCR 0xbf; w $EFR 0x10
w $LCR 0x00; w $IER 0x00
w $LCR 0xbf; w $DLL 0x38; w $DLH 0x01      # 9600 @ 48 MHz
w $LCR 0x03; w $MCR 0x03; w $FCR 0x07; w $MDR1 0x00

sample() {   # sample <label> <n>
  i=0
  while [ $i -lt $2 ]; do
    l=$(r $LSR); f=$(r $RXFIFO_LVL)
    printf "   %-28s LSR 0x%s  BI=%d DR=%d  rxlvl=%d\n" "$1" "$l" \
      $(( (0x$l & 0x10) != 0 )) $(( 0x$l & 0x01 )) $(( 0x$f ))
    i=$((i+1))
  done
}

echo "== LSR BI semantics on UART3, internal loopback only =="
w $MCR 0x13                     # loopback on: TX folds back into RX inside the UART

echo "-- A: break ON, FIFO reset immediately before (must end SET)"
w $LCR 0x43; w $FCR 0x07
sleep 1; sample "A after 1s" 3

echo "-- B: break still ON, FIFO left to fill (is BI still readable?)"
sleep 2; sample "B filled" 3

echo "-- C: break OFF, FIFO NOT reset (false-positive case)"
w $LCR 0x03
sleep 1; sample "C stale" 3

echo "-- D: break OFF, FIFO reset first (must be CLEAR)"
w $FCR 0x07
sleep 1; sample "D flushed" 3

w $MCR 0x03
echo "== done; clock gate restored on exit. Pads were never touched. =="
