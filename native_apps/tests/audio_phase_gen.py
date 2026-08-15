#!/usr/bin/env python3
"""Generate three 3 s stereo 48 kHz test tones that differ ONLY in channel phase.

Companion: audio_phase_test.sh, which plays them with click markers.

    python3 audio_phase_gen.py [outdir]      # defaults to $PWD, NOT the script dir
    scp ph_*.wav audio_phase_test.sh root@<ip>:/tmp/ && ssh root@<ip> sh /tmp/audio_phase_test.sh

WHY THIS EXISTS.  hw:0,0 is stereo-only (measured, SYSTEM_ANALYSIS.md#34-audio), while the
speaker is mono -- so a mono generator has to put its sample somewhere.  The docs described
the speaker as driven by the TWL4030 "HandsfreeL/R class-D bridge" with both switches on and
both feeding the single SPKR1, which admits two incompatible readings:

  sums   -> SPKR1 sees L + R.  Duplicating a mono sample into both channels is correct
            and is the LOUDEST option.
  bridged-> SPKR1 sees L - R.  Duplicating into both channels CANCELS TO SILENCE, and the
            backend must write L = -R instead.

Real music never has L == R, so playing music cannot distinguish them.  These three can:

  ph_left.wav  L =  s, R =  0   reference
  ph_dup.wav   L =  s, R =  s   what a mono-source backend would write
  ph_anti.wav  L =  s, R = -s   anti-phase

MEASURED RESULT -- .188, 2026-08-14, by ear:  left audible, dup SLIGHTLY LOUDER than left,
anti INAUDIBLE.  Complete cancellation on anti-phase is only possible if the speaker sees
L + R, so THE SPEAKER SUMS and duplication is correct.  "Bridge" in the docs refers to each
Handsfree amp being internally bridge-tied, not to SPKR1 bridging L against R.

Re-run this on a new unit before assuming it holds there -- it is one panel, like B3c.
Verify the generator itself first (peaks equal, R==0 / R==L / R==-L in every loud frame):
the files must differ ONLY in phase or the loudness comparison measures the wrong thing.
"""
import math
import struct
import sys

RATE = 48000
DUR = 3.0
FREQ = 440.0
AMPL = 12000          # ~-8.7 dBFS: loud enough to judge, short of any clipping
FADE = 0.03           # 30 ms, so the onset is not a click mistaken for the tone


def write_wav(path, mode):
    n = int(RATE * DUR)
    fade = FADE * RATE
    frames = bytearray()
    for i in range(n):
        env = min(1.0, i / fade, (n - i) / fade)
        s = int(AMPL * env * math.sin(2 * math.pi * FREQ * i / RATE))
        if mode == "dup":
            left, right = s, s
        elif mode == "left":
            left, right = s, 0
        elif mode == "anti":
            left, right = s, -s
        else:
            raise ValueError(mode)
        frames += struct.pack("<hh", left, right)

    data = bytes(frames)
    header = (
        b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt "
        + struct.pack("<IHHIIHH", 16, 1, 2, RATE, RATE * 4, 4, 16)
        + b"data" + struct.pack("<I", len(data))
    )
    with open(path, "wb") as f:
        f.write(header + data)
    return len(header) + len(data)


if __name__ == "__main__":
    import os
    out = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
    for mode in ("left", "dup", "anti"):
        p = os.path.join(out, "ph_%s.wav" % mode)
        print("%s  %d bytes" % (p, write_wav(p, mode)))
