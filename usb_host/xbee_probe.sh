#!/bin/sh
# xbee_probe.sh - talk to the radio on UART3 from userspace, with no kernel change.
# Runs ON the device. Needs /usr/local/bin/devmem_write (this directory builds it).
#
# WHY THIS WORKS WITHOUT A DTB PATCH, AND WHY THAT IS SAFE
# -------------------------------------------------------
# serial@49020000 is status = "disabled" in the vendor device tree, so the kernel
# never binds a driver to UART3 and there is no /dev/ttyO2. That absence is the
# reason this is safe rather than the reason it is impossible: with no driver bound,
# nothing contends for the module, so userspace may own it outright. Three things
# are already true on a stock unit and were each measured, not assumed:
#
#   * the UART3 pads come up in MODE0 (function, not GPIO), set by vendor U-Boot
#   * uart3_ick, the interface clock, is already enabled
#   * the module is out of reset -- SYSS reads RESETDONE, MVR reads a real version
#
# So reaching the radio costs exactly one bit: uart3_fck. Everything this script
# touches is restored on exit, and a power cycle would restore it anyway. It writes
# no file, patches no image and needs no reboot.
#
# REGISTERS, AND WHERE THE NUMBERS CAME FROM
# ------------------------------------------
# uart3_fck = CM_FCLKEN_PER (0x48005000) bit 11
# uart3_ick = CM_ICLKEN_PER (0x48005010) bit 11   -- already set, left alone
# Both from the vanilla tree, omap3xxx-clocks.dtsi: reg 0x1000 / 0x1010 with
# ti,bit-shift = 11, relative to the CM base 0x48004000. Read them there rather
# than trusting a remembered bit layout -- the PER domain is easy to get wrong.
#
# The fclk parent is per_48m_fck, a fixed 48 MHz, so in 16x mode the divisor is
# 48e6 / (16 * baud). That is where the DLL/DLH pairs below come from; 57600 needs
# 52 (0x34), which is 0.16 % off and well inside tolerance.
#
# WHY IT SWEEPS THE BAUD RATE
# ---------------------------
# 57600 is the rate the vendor userspace uses, but an XBee leaves the factory at
# 9600 (BD = 3), and a module of unknown history may be at any of them. Silence
# at one rate therefore proves nothing about the radio, so all eight values BD
# can hold are tried -- BD = 0..7 is 1200, 2400, 4800, 9600, 19200, 38400, 57600,
# 115200 -- and only one variable changes per pass. ⚠️ Sweeping only the top five
# is the easy mistake: it leaves a module at BD = 0..2 reading as dead hardware.
# At 48 MHz the low three divide exactly (2500, 1250, 625); the top five are off
# by at most 0.16 %, well inside tolerance.
#
# WHY THE LOOPBACK SELF-TEST IS NOT OPTIONAL
# ------------------------------------------
# MCR bit 4 loops TX back into RX inside the UART, never reaching the pads or the
# module. Without it, a silent radio and a wrong register sequence produce the
# identical output, and the wrong one of those is far more likely. If the self-test
# fails, every radio line below it is meaningless -- the script says so and exits
# non-zero rather than letting a broken instrument report a clean reading.

D=/usr/local/bin/devmem_write
CM_FCLKEN_PER=0x48005000
FCLK_UART3_ON=0xc00     # bit 11 set, alongside the bit 10 this unit already has
FCLK_RESTORE=0x400

# UART3 registers. 4-byte stride, several are bank-switched by LCR.
THR=0x49020000; RHR=0x49020000; DLL=0x49020000
IER=0x49020004; DLH=0x49020004
FCR=0x49020008; EFR=0x49020008
LCR=0x4902000c; MCR=0x49020010; LSR=0x49020014
MDR1=0x49020020; MVR=0x49020050; SYSS=0x49020058

# The UART3 RX pad, for the line-state stage at the bottom. devmem_write is
# 32-bit only and pinctrl-single reports these as 16-bit registers, so the pad
# is the UPPER halfword of this word and the lower one (pad 182, a GPIO in
# MODE4) must be carried through untouched.
PADCONF_RX=0x4800219c
PAD_RX_PULLDOWN=0x0108          # PIN_INPUT_PULLDOWN | MUX_MODE0
padconf_orig=""

[ -x "$D" ] || { echo "xbee_probe: $D is missing -- build and deploy it first"; exit 2; }

r() { $D "$1" | sed -n 's/.*current value = 0x\([0-9a-fA-F]*\).*/\1/p'; }
w() { $D "$1" "$2" >/dev/null 2>&1; }

# Leave the SoC as it was found, on every exit path including a signal.
cleanup() {
  w $MDR1 0x7
  [ -n "$padconf_orig" ] && w $PADCONF_RX 0x$padconf_orig
  w $CM_FCLKEN_PER $FCLK_RESTORE
}
trap cleanup EXIT INT TERM

hexbyte() { printf "%02x" $(( 0x$1 & 0xff )); }

# Decode only what an AT response can contain: hex digits, OK, ERROR, CR/LF.
# A general byte-to-char decoder here would be one more thing that can lie.
chr() {
  case "$1" in
    30) printf 0 ;; 31) printf 1 ;; 32) printf 2 ;; 33) printf 3 ;;
    34) printf 4 ;; 35) printf 5 ;; 36) printf 6 ;; 37) printf 7 ;;
    38) printf 8 ;; 39) printf 9 ;;
    41) printf A ;; 42) printf B ;; 43) printf C ;; 44) printf D ;;
    45) printf E ;; 46) printf F ;; 47) printf G ;;
    4b) printf K ;; 4f) printf O ;; 52) printf R ;; 53) printf S ;;
    0d) printf "<CR>" ;; 0a) printf "<LF>" ;;
    *)  printf "[%s]" "$1" ;;
  esac
}

setbaud() {  # setbaud <dll> <dlh> -- the TI-documented ordering; MDR1 last
  w $MDR1 0x7
  w $LCR 0xbf ; w $EFR 0x10
  w $LCR 0x00 ; w $IER 0x00
  w $LCR 0xbf ; w $DLL "$1" ; w $DLH "$2"
  w $LCR 0x03            # 8N1, operational bank
  w $MCR 0x03
  w $FCR 0x07            # enable and clear both FIFOs
  w $MDR1 0x00           # 16x UART mode -- the module starts running here
}

putc() {
  n=0
  while [ $n -lt 60 ]; do
    [ $(( 0x$(r $LSR) & 0x20 )) -ne 0 ] && { w $THR "$1"; return 0; }
    n=$((n+1))
  done
  echo "   !! TX timeout: THRE never set"
  return 1
}

# Read until the RX FIFO has been quiet for a while. Sets $hex and $txt.
drain() {
  hex=""; txt=""; idle=0
  while [ $idle -lt 50 ]; do
    if [ $(( 0x$(r $LSR) & 0x01 )) -ne 0 ]; then
      b=$(hexbyte "$(r $RHR)")
      hex="$hex $b"; txt="$txt$(chr "$b")"
      idle=0
    else
      idle=$((idle+1))
    fi
  done
  [ -n "$hex" ]
}

echo "== UART3 radio probe (no DTB patch, no reboot; state restored on exit) =="
w $CM_FCLKEN_PER $FCLK_UART3_ON
padconf_orig=$(r $PADCONF_RX)
echo "   uart3_fck: $(r $CM_FCLKEN_PER)   MVR: $(r $MVR)   SYSS: $(r $SYSS)"
setbaud 0x34 0x00

echo
echo "== self-test: internal loopback, TX->RX inside the UART =="
w $MCR 0x13                     # MCR bit 4 = loopback, bits 0-1 = DTR/RTS
drain >/dev/null 2>&1
putc 0x55
sleep 1
if drain; then
  echo "   sent 55, got:$hex  -- init, TX, RX and polling all work"
else
  echo "   sent 55, got NOTHING -- the probe itself is broken."
  echo "   Not continuing: a silent radio would be indistinguishable from this."
  exit 1
fi
w $MCR 0x03
drain >/dev/null 2>&1

echo
echo "== command mode, every standard rate; one variable changes per pass =="
answered=1
try() {  # try <label> <dll> <dlh>
  setbaud "$2" "$3"
  drain >/dev/null 2>&1
  sleep 2                       # the guard interval +++ requires before it
  putc 0x2b; putc 0x2b; putc 0x2b
  sleep 2                       # and after it
  if drain; then
    echo "   $1  +++ ->$hex   [$txt]   <<< ANSWERED"; answered=0; return 0
  fi
  for b in 41 54; do putc 0x$b; done; putc 0x0d   # bare AT, in case it is already in command mode
  sleep 1
  if drain; then
    echo "   $1  AT  ->$hex   [$txt]   <<< ANSWERED"; answered=0; return 0
  fi
  echo "   $1  silent"
}

try "  1200" 0xc4 0x09
try "  2400" 0xe2 0x04
try "  4800" 0x71 0x02
try "  9600" 0x38 0x01
try " 19200" 0x9c 0x00
try " 38400" 0x4e 0x00
try " 57600" 0x34 0x00
try "115200" 0x1a 0x00

# ---------------------------------------------------------------------------
# API MODE -- the one configuration that answers nothing above and is not a fault
# ---------------------------------------------------------------------------
# AP=1 is a setting, not a series: a module left in API mode by a previous owner
# treats "+++" and "AT" as payload and replies to neither, at any rate, which is
# indistinguishable from a dead radio in the sweep above. It does answer a framed
# command, so this costs one more sweep and is worth it before blaming hardware.
#
# The frame is an AT Command (0x08) reading VR, the firmware version, which every
# XBee firmware answers:
#     7E 00 04 08 01 'V' 'R' cksum
# 7E starts a frame; 00 04 is the length, counting the four bytes after it and
# excluding the checksum; 01 is the frame ID; cksum is 0xFF minus the sum of
# those four, so 0xFF - (08+01+56+52) = 0xFF - 0xB1 = 0x4E. A reply starts 7E and
# carries frame type 0x88 (AT Command Response). No guard interval is involved,
# so this sweep is quick.
echo
echo "== API mode? a framed command, in case AP=1 is why the AT sweep saw nothing =="
apianswered=1
tryapi() {  # tryapi <label> <dll> <dlh>
  setbaud "$2" "$3"
  drain >/dev/null 2>&1
  for b in 7e 00 04 08 01 56 52 4e; do putc 0x$b || return 1; done
  sleep 1
  if drain; then
    echo "   $1  frame ->$hex   <<< ANSWERED, so the module is in API mode"
    apianswered=0; return 0
  fi
  echo "   $1  silent"
}

tryapi "  1200" 0xc4 0x09
tryapi "  2400" 0xe2 0x04
tryapi "  4800" 0x71 0x02
tryapi "  9600" 0x38 0x01
tryapi " 19200" 0x9c 0x00
tryapi " 38400" 0x4e 0x00
tryapi " 57600" 0x34 0x00
tryapi "115200" 0x1a 0x00

# ---------------------------------------------------------------------------
# IS ANYTHING OUT THERE AT ALL? -- the one thing the loopback cannot see
# ---------------------------------------------------------------------------
# MCR bit 4 loops TX into RX *inside* the module, so the self-test above proves
# the UART and proves nothing about the pads. And the RX pad comes up
# PIN_INPUT_PULLUP, so the line reads idle-high whether a module is driving it
# or not -- which is exactly why a radio in the wrong mode and a pad that
# reaches nothing produce the identical silence above.
#
# Flipping that one pad to PIN_INPUT_PULLDOWN separates them. An XBee DOUT is a
# push-pull 3.3 V output and beats the SoC's internal pulldown, so a powered,
# correctly-oriented module holds the line HIGH. With nothing driving it the
# pulldown wins, the UART sees a continuous space, and LSR sets BI (bit 4).
# Bit values are PIN_INPUT_PULLUP / PIN_INPUT_PULLDOWN straight out of the
# vanilla tree's include/dt-bindings/pinctrl/omap.h, not a remembered layout.
#
# TWO CONTROLS, neither optional, because "no break" is the interesting answer
# and both failure modes of this instrument produce it for free:
#   * a break detector that can never fire. So force a break first -- LCR bit 6
#     drives TX low, and under loopback that lands on RX -- and require BI.
#   * a padconf write that did not land. So read the register back and require
#     the pad to have actually changed before believing the reading.
echo
echo "== is anything driving the RX pad? (pullup off, so the pad cannot fake it) =="
setbaud 0x38 0x01               # 9600: longest bit time, most robust break detect

w $MCR 0x13; w $LCR 0x43        # loopback + break enable: TX held low onto RX
sleep 1
r $LSR >/dev/null               # BI is latched; this read arms a clean one
sleep 1
bi_ctl=$(( 0x$(r $LSR) & 0x10 ))
w $LCR 0x03; w $MCR 0x03; drain >/dev/null 2>&1

if [ $bi_ctl -eq 0 ]; then
  echo "   control FAILED: a forced break did not set LSR BI, so this stage"
  echo "   cannot tell a driven line from an undriven one. Reporting nothing."
  driving=2
else
  w $PADCONF_RX $(printf "0x%08x" $(( (0x$padconf_orig & 0x0000ffff) \
                                      | ($PAD_RX_PULLDOWN << 16) )))
  got=$(( 0x$(r $PADCONF_RX) >> 16 ))
  if [ $got -ne $(( PAD_RX_PULLDOWN )) ]; then
    echo "   control FAILED: pad still reads $(printf 0x%04x $got), the write did"
    echo "   not land, so the pullup is still holding the line up. Reporting nothing."
    driving=2
  else
    r $LSR >/dev/null; drain >/dev/null 2>&1
    sleep 1
    lsr=$(r $LSR)
    if [ $(( 0x$lsr & 0x10 )) -ne 0 ]; then
      echo "   LSR $lsr -- BREAK. With the pullup off the line falls, so NOTHING is"
      echo "   driving DOUT: the pad is not reaching a powered module."
      driving=1
    else
      echo "   LSR $lsr -- no break: something is holding the line high. ⚠️ Read the"
      echo "   caveat at the end before calling that a working socket."
      driving=0
    fi
  fi
  w $PADCONF_RX 0x$padconf_orig
fi

echo
if [ $answered -eq 0 ]; then
  echo "== a radio answered. ATVR names the series; ATSH/ATSL are its address. =="
elif [ $apianswered -eq 0 ]; then
  echo "== the radio is alive and in API mode. Not a fault -- a setting. =="
  echo "   Either speak API frames from game code, or send ATAP0 as a frame and"
  echo "   ATWR to persist it, and the AT sweep above will answer next run."
  answered=0
elif [ $driving -eq 0 ]; then
  echo "== silent to AT and to API, and the RX line is held HIGH. =="
  echo "   API mode is ruled out by the frame sweep, and every BD value by the"
  echo "   eight-rate sweep. What is left is a pin the socket leaves floating --"
  echo "   SM sleep on pin 9, or D6 RTS flow control on pin 16."
  echo "   ⚠️ This does NOT yet rule out the wiring: the forced break above proves"
  echo "   the detector fires, not that THIS pad can ever read low, and a board"
  echo "   pullup on the DOUT net would read identical with no module fitted."
  echo "   Re-run once with the module OUT: it must say BREAK. Until it has, treat"
  echo "   \"held high\" as \"not measured\" rather than as a working socket."
elif [ $driving -eq 1 ]; then
  echo "== silent AND nothing is driving DOUT. =="
  echo "   The module is not talking to the pad at all. Check orientation against"
  echo "   the pin-1 dot first -- pin 1 is a live 3.3 V rail, so a reversed part has"
  echo "   already had its effect -- then continuity from the socket to the pads."
else
  echo "== silent at every rate, with the instrument proving itself first. =="
  echo "   The UART is not the remaining suspect. What is left is the module and"
  echo "   the socket: orientation against the pin-1 dot, whether it survived being"
  echo "   powered, and a module left in API mode, which answers no AT at all."
fi
exit $answered
