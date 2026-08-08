#!/usr/bin/env python3
"""make-fake-uimage.py — build a small SYNTHETIC uImage for tests/rw_usbpower_test.sh.

    make-fake-uimage.py <out> [--power 0x32] [<sabotage> ...]

⚠️ Synthetic on purpose, and it is the only kind of fixture this suite may use.
The vendor `uImage-system` is a 5.2 MB copyrighted Steelcase binary; `usb_host/.gitignore`
excludes it and this repo is meant to be published, so it can never be committed as a
fixture. What makes a synthetic one usable is that `uimage.py` FINDS the device tree by
magic instead of asserting the vendor offset 0x4eb788 — an image of a few hundred bytes
reaches exactly the same walk as the real one.

What is faithful here, and what is not:

  faithful     the 64-byte uImage header and both CRC32s; the FDT header's ten
               big-endian fields; the struct/strings block encoding; a `power`
               property of length 4 inside a node whose name contains `usb_otg_hs`.
               Those are what uimage.py reads, so they are what a fixture must get
               right.
  NOT faithful the payload is filler, not a compressed kernel, and the tree has one
               node instead of hundreds. So this fixture cannot tell you anything
               about the REAL image's md5 — which is why the suite overrides
               RW_UIMAGE_*_MD5 for the sequence tests and asserts the shipped
               constants separately.

Every sabotage is a declared flag rather than a byte offset in the caller, so a
test reads as the defect it is describing. CRCs are recomputed AFTER a structural
sabotage and BEFORE a CRC one, so each flag makes exactly one check fail — a
fixture that trips two checks cannot serve as the negative control for either.
"""

import binascii
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "usb_host"))

from uimage import (  # noqa: E402
    FDT_BEGIN_NODE,
    FDT_END,
    FDT_END_NODE,
    FDT_MAGIC,
    FDT_PROP,
    UIMAGE_HEADER_SIZE,
    UIMAGE_MAGIC,
    uimage_fix_crcs,
)

SABOTAGES = ("--break-dcrc", "--break-hcrc", "--bad-magic", "--bad-size", "--no-usb-node")


def align4(b):
    while len(b) % 4:
        b += b"\0"
    return b


def build_fdt(power=0x32, node=b"usb_otg_hs@480ab000"):
    """A minimal but valid FDT: / { <node> { power = <N>; }; }."""
    strings = b"power\0"

    struct_block = b""
    struct_block += struct.pack(">I", FDT_BEGIN_NODE) + align4(b"\0")          # root, empty name
    struct_block += struct.pack(">I", FDT_BEGIN_NODE) + align4(node + b"\0")
    #                          token           len  nameoff-into-strings
    struct_block += struct.pack(">III", FDT_PROP, 4, 0) + struct.pack(">I", power)
    struct_block += struct.pack(">I", FDT_END_NODE)
    struct_block += struct.pack(">I", FDT_END_NODE)
    struct_block += struct.pack(">I", FDT_END)

    off_struct = 64                      # past the 40-byte header, comfortably aligned
    off_strings = off_struct + len(struct_block)
    totalsize = off_strings + len(strings)

    hdr = struct.pack(
        ">IIIIIIIIII",
        FDT_MAGIC,
        totalsize,
        off_struct,
        off_strings,
        0,              # off_mem_rsvmap — unread by uimage.py, so 0 is honest filler
        17,             # version: what dtc has emitted since 2007
        16,             # last_comp_version
        0,              # boot_cpuid_phys
        len(strings),
        len(struct_block),
    )
    return hdr + b"\0" * (off_struct - len(hdr)) + struct_block + strings


def build(power=0x32, sabotage=()):
    fdt = build_fdt(power=power, node=b"nothing_here@0" if "--no-usb-node" in sabotage
                    else b"usb_otg_hs@480ab000")

    # Filler before and after, so the FDT is genuinely *found* rather than sitting at
    # a predictable place. 0x11 rather than 0x00 so a stray d00dfeed cannot appear.
    payload = b"\x11" * 128 + fdt + b"\x11" * 64

    data = bytearray(UIMAGE_HEADER_SIZE + len(payload))
    struct.pack_into(">I", data, 0, UIMAGE_MAGIC)
    struct.pack_into(">I", data, 8, 0x5B000000)          # ih_time
    struct.pack_into(">I", data, 12, len(payload))       # ih_size
    struct.pack_into(">I", data, 16, 0x80008000)         # ih_load
    struct.pack_into(">I", data, 20, 0x80008000)         # ih_ep
    data[28] = 5                                         # ih_os   = Linux
    data[29] = 2                                         # ih_arch = ARM
    data[30] = 2                                         # ih_type = Kernel
    data[31] = 0                                         # ih_comp = none
    data[32:32 + 10] = b"rw-fake\0\0\0"                  # ih_name
    data[UIMAGE_HEADER_SIZE:] = payload

    # A structural sabotage happens BEFORE the CRCs are computed, so the CRCs stay
    # correct and only the structural check fires.
    if "--bad-size" in sabotage:
        struct.pack_into(">I", data, 12, len(payload) + 4096)

    uimage_fix_crcs(data)

    # A CRC sabotage happens AFTER, because that is what a corrupted file is: bytes
    # that no longer match a CRC computed over the originals.
    if "--break-dcrc" in sabotage:
        data[UIMAGE_HEADER_SIZE + 3] ^= 0xFF
    if "--break-hcrc" in sabotage:
        # ih_load, not the CRC field itself — a header byte the checker does not
        # otherwise read, so the ONLY consequence is the header CRC mismatching.
        struct.pack_into(">I", data, 16, 0xDEADBEEF)
    if "--bad-magic" in sabotage:
        struct.pack_into(">I", data, 0, 0x12345678)

    return bytes(data)


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__.strip())
        return 0 if len(argv) > 1 else 1

    out = argv[1]
    power = 0x32
    sabotage = []
    i = 2
    while i < len(argv):
        a = argv[i]
        if a == "--power":
            i += 1
            if i >= len(argv):
                print("--power needs a value", file=sys.stderr)
                return 2
            power = int(argv[i], 0)
        elif a in SABOTAGES:
            sabotage.append(a)
        else:
            print("unknown option: %s" % a, file=sys.stderr)
            return 2
        i += 1

    data = build(power=power, sabotage=tuple(sabotage))
    with open(out, "wb") as fh:
        fh.write(data)
    print("%s  %d bytes  power=0x%02x  %s"
          % (out, len(data), power, " ".join(sabotage) or "clean"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
