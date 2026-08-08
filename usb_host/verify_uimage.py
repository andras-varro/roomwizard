#!/usr/bin/env python3
"""verify_uimage.py — is this uImage intact, and what is its USB power budget?

    verify_uimage.py <file> [--expect-power 0xfa|0x32] [--quiet]

Exits 0 only if every check passes.  Prints one line per check, in the form

    magic=27051956 OK
    header=8a4b6b1e/8a4b6b1e OK
    data=5de3ec9f/4bb38c98 BAD          <- stored/computed
    power=0xfa (250) 500mA OK

so a failure names the numbers rather than saying "invalid".  `--expect-power`
turns the power reading into an assertion; without it the value is reported and
not judged.

── Why this exists ─────────────────────────────────────────────────────────

There is no boot-time md5 and no signature on this device: the uImage header CRC
and data CRC are the ONLY gate between a bad write to p1 and a unit that does not
come up (SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery), and a unit that does not
come up has no serial console to say why.  So the file that lib/rw-usbpower.sh is
about to put on p1 gets checked here first, and then checked AGAIN after the write
by re-reading it off the card.

It replaces usb_host/verify_patch.sh, which shelled out to `mkimage -l` and
`dtc -I dtb` — neither of which is installed in this WSL — from a hardcoded
/mnt/c/work/roomwizard/usb_host.  Pure Python, no external tools.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from uimage import (  # noqa: E402
    UIMAGE_MAGIC,
    UImageError,
    be32,
    find_power_offset,
    uimage_crcs,
)


def main(argv):
    path = None
    expect = None
    quiet = False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in ("-h", "--help"):
            print(__doc__.strip())
            return 0
        elif a == "--quiet":
            quiet = True
        elif a == "--expect-power":
            i += 1
            if i >= len(argv):
                print("--expect-power needs a value", file=sys.stderr)
                return 2
            try:
                expect = int(argv[i], 0)
            except ValueError:
                print("not a number: %s" % argv[i], file=sys.stderr)
                return 2
        elif a.startswith("-"):
            print("unknown option: %s" % a, file=sys.stderr)
            return 2
        elif path is None:
            path = a
        else:
            print("unexpected argument: %s" % a, file=sys.stderr)
            return 2
        i += 1

    if path is None:
        print("usage: verify_uimage.py <file> [--expect-power N] [--quiet]", file=sys.stderr)
        return 2

    say = (lambda *a: None) if quiet else (lambda *a: print(*a))

    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        print("verify_uimage: cannot read %s: %s" % (path, e), file=sys.stderr)
        return 1

    bad = 0

    # ── the uImage header ──
    try:
        shcrc, chcrc, sdcrc, cdcrc = uimage_crcs(data)
    except UImageError as e:
        print("magic=%08x BAD (%s)" % (be32(data, 0) if len(data) >= 4 else 0, e))
        return 1
    say("magic=%08x OK" % UIMAGE_MAGIC)

    for label, stored, computed in (
        ("header", shcrc, chcrc),
        ("data", sdcrc, cdcrc),
    ):
        if stored == computed:
            say("%s=%08x/%08x OK" % (label, stored, computed))
        else:
            print("%s=%08x/%08x BAD" % (label, stored, computed))
            bad += 1

    # The header's own size field, which U-Boot uses to bound the data CRC. A
    # short read that stopped mid-file can still CRC clean against itself if the
    # header travelled with it, so the length is asserted separately.
    declared = be32(data, 12)
    actual = len(data) - 64
    if declared == actual:
        say("size=%d OK" % declared)
    else:
        print("size=%d/%d BAD" % (declared, actual))
        bad += 1

    # ── the power property ──
    try:
        dtb_off, pow_off = find_power_offset(data)
    except UImageError as e:
        print("power=absent BAD (%s)" % e)
        return 1
    val = be32(data, pow_off)
    verdict = "OK"
    if expect is not None and val != expect:
        verdict = "BAD (want 0x%02x)" % expect
        bad += 1
    say("dtb=0x%x power_at=0x%x" % (dtb_off, pow_off))
    line = "power=0x%02x (%d) %dmA %s" % (val, val, val * 2, verdict)
    if verdict == "OK":
        say(line)
    else:
        print(line)

    if bad:
        print("verify_uimage: %s FAILED %d check(s)" % (path, bad), file=sys.stderr)
        return 1
    say("verify_uimage: %s OK" % path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
