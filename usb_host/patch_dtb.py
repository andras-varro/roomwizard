#!/usr/bin/env python3
"""patch_dtb.py — raise the MUSB USB power budget in a uImage's device tree.

    patch_dtb.py [<in> [<out>]]

Defaults are `uImage-system` and `uImage-system-patched`, relative to the CURRENT
DIRECTORY — the contract usb_host/README.md documents and the reason
usb_host/build-and-deploy.sh has to `cd "$SCRIPT_DIR"` before calling it
(../IMPROVEMENT_PLAN.md B19).  ⚠️ New callers should pass both paths explicitly:
a rules-driven installer works in a temp directory and must not depend on its cwd.

What it changes and why it cannot be a boot script: the `power` property of the
`usb_otg_hs` node, 0x32 (100 mA) -> 0xfa (500 mA).  omap2430.c:452 reads that
value from the device tree at driver PROBE, before any init script exists, and the
tree is appended to the kernel inside uImage-system — so there is no file on the
normal filesystem to edit.  That one number is the sole reason p1 enters the
picture (../IMPROVEMENT_PLAN.md F15).

⚠️ This writes <out>; it never touches <in>.  Putting the result on p1 is
lib/rw-usbpower.sh's job, gated on md5 and backed up first.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from uimage import (  # noqa: E402
    POWER_VENDOR,
    POWER_WANTED,
    UImageError,
    be32,
    find_power_offset,
    uimage_crcs,
    uimage_fix_crcs,
)

import struct  # noqa: E402


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("-")]
    if len(argv) > 1 and argv[1] in ("-h", "--help"):
        print(__doc__.strip())
        return 0
    if len(args) > 2:
        print("usage: patch_dtb.py [<in> [<out>]]", file=sys.stderr)
        return 2

    src = args[0] if len(args) >= 1 else "uImage-system"
    dst = args[1] if len(args) >= 2 else "uImage-system-patched"

    try:
        with open(src, "rb") as f:
            data = bytearray(f.read())
    except OSError as e:
        print("patch_dtb: cannot read %s: %s" % (src, e), file=sys.stderr)
        return 1

    print("in:   %s (%d bytes)" % (src, len(data)))

    try:
        # The input's own CRCs are checked before anything is written. A caller
        # that hands us a corrupt image would otherwise get a *consistently
        # CRC'd* corrupt image back, which U-Boot would happily boot.
        shcrc, chcrc, sdcrc, cdcrc = uimage_crcs(data)
        if shcrc != chcrc or sdcrc != cdcrc:
            print(
                "patch_dtb: %s does not verify before patching "
                "(header=%08x/%08x data=%08x/%08x)" % (src, shcrc, chcrc, sdcrc, cdcrc),
                file=sys.stderr,
            )
            return 1

        dtb_off, pow_off = find_power_offset(data)
    except UImageError as e:
        print("patch_dtb: %s" % e, file=sys.stderr)
        return 1

    old = be32(data, pow_off)
    print("dtb:  0x%x" % dtb_off)
    print("power at 0x%x = 0x%02x (%d) -> %d mA" % (pow_off, old, old, old * 2))

    if old == POWER_WANTED:
        print(
            "patch_dtb: %s is already patched to 0x%02x — nothing to do"
            % (src, POWER_WANTED),
            file=sys.stderr,
        )
        return 1
    if old != POWER_VENDOR:
        # Refuse rather than overwrite. An unexpected value means this is not the
        # firmware the 9-byte diff was measured against, and the caller's md5
        # gate should already have stopped us getting here.
        print(
            "patch_dtb: expected 0x%02x or 0x%02x, found 0x%02x — refusing"
            % (POWER_VENDOR, POWER_WANTED, old),
            file=sys.stderr,
        )
        return 1

    struct.pack_into(">I", data, pow_off, POWER_WANTED)
    hcrc, dcrc = uimage_fix_crcs(data)
    print("power now 0x%02x (%d) -> %d mA" % (POWER_WANTED, POWER_WANTED, POWER_WANTED * 2))
    print("header CRC 0x%08x, data CRC 0x%08x" % (hcrc, dcrc))

    try:
        with open(dst, "wb") as f:
            f.write(data)
    except OSError as e:
        print("patch_dtb: cannot write %s: %s" % (dst, e), file=sys.stderr)
        return 1
    print("out:  %s (%d bytes)" % (dst, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
