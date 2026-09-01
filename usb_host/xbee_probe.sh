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
# 9600 (BD = 3), and a module of unknown history may be at either. Silence at one
# rate therefore proves nothing about the radio, so every standard rate is tried
# and only one variable changes per pass.
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

[ -x "$D" ] || { echo "xbee_probe: $D is missing -- build and deploy it first"; exit 2; }

r() { $D "$1" | sed -n 's/.*current value = 0x\([0-9a-fA-F]*\).*/\1/p'; }
w() { $D "$1" "$2" >/dev/null 2>&1; }

# Leave the SoC as it was found, on every exit path including a signal.
cleanup() { w $MDR1 0x7; w $CM_FCLKEN_PER $FCLK_RESTORE; }
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

try "  9600" 0x38 0x01
try " 19200" 0x9c 0x00
try " 38400" 0x4e 0x00
try " 57600" 0x34 0x00
try "115200" 0x1a 0x00

echo
if [ $answered -eq 0 ]; then
  echo "== a radio answered. ATVR names the series; ATSH/ATSL are its address. =="
else
  echo "== silent at every rate, with the instrument proving itself first. =="
  echo "   The UART is not the remaining suspect. What is left is the module and"
  echo "   the socket: orientation against the pin-1 dot, whether it survived being"
  echo "   powered, and a Series 2 left in API mode, which answers no AT at all."
  echo "   Swapping in a spare separates the module from the socket in one step."
fi
exit $answered
