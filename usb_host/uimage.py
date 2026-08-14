#!/usr/bin/env python3
"""uimage.py — the one implementation of "read a uImage and find the MUSB power property".

Imported by patch_dtb.py (which writes the value) and verify_uimage.py (which
only reads it).  Two copies of an FDT walk is two things to keep in step, and the
one that drifts is the one that reports a patched image as clean.

── Why this is pure Python ─────────────────────────────────────────────────

Neither `mkimage` nor `dtc` is installed in this WSL, and neither needs to be:
a uImage header is 64 bytes with two CRC32s, and an FDT is a documented
structure.  So the whole gate/patch/verify sequence runs with nothing but
python3, which the offline installer already requires.  usb_host/verify_patch.sh
used to do this with `mkimage -l` plus `dtc -I dtb`, from a hardcoded
/mnt/c/work/roomwizard/usb_host — it could not run on this host at all.

── ⚠️ The DTB is FOUND, not asserted at a constant offset ──────────────────

0x4eb788 is where it sits in the vendor image and is tried FIRST, because that
makes the common case one header read rather than a scan.  But asserting it is
what made a small synthetic test fixture impossible — a 5.2 MB kernel was the
only input that could reach the walk.

⚠️ A scan for d00dfeed alone is not sound either: a compressed kernel payload can
contain those four bytes by coincidence.  So a candidate must ALSO carry a valid
FDT header (magic, version, and both blocks inside totalsize) AND the walk over
its struct block must actually find `power` inside a `usb_otg_hs` node.  Three
conditions, and the third is the one no accident satisfies.
"""

import binascii
import struct

FDT_MAGIC = 0xD00DFEED
UIMAGE_MAGIC = 0x27051956
UIMAGE_HEADER_SIZE = 64

# Where the vendor image keeps it. A HINT — see the module docstring.
DTB_OFFSET_HINT = 0x004EB788

# The MUSB power budget, in 2 mA units: musb_host.c:2797 does
# `hcd->power_budget = 2 * (power_budget ? : 250)`.
POWER_VENDOR = 0x32   # 50  -> 100 mA, the value Steelcase shipped
POWER_WANTED = 0xFA   # 250 -> 500 mA, which is also the kernel's own default

# MUSB's port mode, musb_core.h:82-84.  IMPROVEMENT_PLAN.md B32: the vendor
# declares DUAL_ROLE on a kernel with no gadget support compiled in, which buys
# nothing and costs the SESSION bit.
MODE_HOST = 1
MODE_PERIPHERAL = 2
MODE_DUAL_ROLE = 3
MODE_VENDOR = MODE_DUAL_ROLE   # what Steelcase shipped
MODE_WANTED = MODE_HOST        # what a gadget-less kernel should have

FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9


class UImageError(Exception):
    """Anything that means "this is not the file you think it is"."""


def be32(buf, off):
    return struct.unpack_from(">I", buf, off)[0]


# ---------------------------------------------------------------------------
# The uImage header
# ---------------------------------------------------------------------------
def uimage_crcs(data):
    """Return (stored_header_crc, computed_header_crc, stored_data_crc,
    computed_data_crc) for a uImage.

    The header CRC is computed over the 64-byte header with its own CRC field
    zeroed, which is why this works on a bytearray copy rather than in place.
    """
    if len(data) < UIMAGE_HEADER_SIZE:
        raise UImageError(
            "%d bytes is shorter than a %d-byte uImage header"
            % (len(data), UIMAGE_HEADER_SIZE)
        )
    if be32(data, 0) != UIMAGE_MAGIC:
        raise UImageError(
            "not a uImage: magic is %08x, want %08x" % (be32(data, 0), UIMAGE_MAGIC)
        )

    stored_hcrc = be32(data, 4)
    stored_dcrc = be32(data, 24)

    hdr = bytearray(data[:UIMAGE_HEADER_SIZE])
    struct.pack_into(">I", hdr, 4, 0)
    computed_hcrc = binascii.crc32(bytes(hdr)) & 0xFFFFFFFF
    computed_dcrc = binascii.crc32(bytes(data[UIMAGE_HEADER_SIZE:])) & 0xFFFFFFFF

    return stored_hcrc, computed_hcrc, stored_dcrc, computed_dcrc


def uimage_fix_crcs(data):
    """Recompute both CRCs in place.  `data` must be a bytearray.

    Data CRC first, then header CRC: the header carries the data CRC, so the
    other order signs a header that is already stale.  That ordering is the
    whole reason U-Boot's own check would fail on a patch that got it backwards
    — and there is no boot-time md5 or signature, so these two CRCs are the ONLY
    gate on the file (SYSTEM_ANALYSIS.md#4-boot-chain-and-recovery).
    """
    dcrc = binascii.crc32(bytes(data[UIMAGE_HEADER_SIZE:])) & 0xFFFFFFFF
    struct.pack_into(">I", data, 24, dcrc)
    struct.pack_into(">I", data, 4, 0)
    hcrc = binascii.crc32(bytes(data[:UIMAGE_HEADER_SIZE])) & 0xFFFFFFFF
    struct.pack_into(">I", data, 4, hcrc)
    return hcrc, dcrc


# ---------------------------------------------------------------------------
# The FDT inside it
# ---------------------------------------------------------------------------
def _fdt_header(data, base):
    """Parse and VALIDATE an FDT header at `base`.  None if it is not one.

    Every field is range-checked against totalsize, because this function is
    what stands between "a real device tree" and "four bytes that happened to
    read d00dfeed inside a compressed kernel".
    """
    if base < 0 or base + 40 > len(data):
        return None
    if be32(data, base) != FDT_MAGIC:
        return None

    h = {
        "totalsize": be32(data, base + 4),
        "off_struct": be32(data, base + 8),
        "off_strings": be32(data, base + 12),
        "version": be32(data, base + 20),
        "size_strings": be32(data, base + 32),
        "size_struct": be32(data, base + 36),
    }

    # dtc has emitted 17 since 2007 and the kernel still accepts 16.
    if h["version"] not in (16, 17):
        return None
    if not (64 <= h["totalsize"] <= len(data) - base):
        return None
    for off, size in (
        (h["off_struct"], h["size_struct"]),
        (h["off_strings"], h["size_strings"]),
    ):
        if off < 40 or size == 0 or off + size > h["totalsize"]:
            return None
    return h


def _prop_offset_in(data, base, want):
    """Absolute offset of the `want` property VALUE inside `base`'s usb_otg_hs
    node, or None.

    Node identity is tracked by DEPTH rather than by a boolean, so that a child
    node's FDT_END_NODE cannot be mistaken for the end of usb_otg_hs itself and
    send the rest of the search looking at a sibling's properties.  Measured
    2026-08-08 on the vendor image: `usb_otg_hs@480ab000` has NO children and
    `power` is its LAST of 17 properties, so a boolean would also be correct
    *here* — the depth counter is what makes it correct on a tree nobody has
    inspected, which is the only kind this will meet on a unit that is not RW09.

    ⚠️ The property name is resolved by READING the string at `nameoff` and
    comparing exact bytes.  Never search the strings block for the name instead:
    dtc deduplicates that block by SUFFIX, so a short name may have no entry of
    its own at all.  Measured on `.188` 2026-08-14 — the live blob has
    `usb_mode` at nameoff 0x3e0 and `mode` at 0x3e4, i.e. `mode` IS the tail of
    `usb_mode` and `strings.find(b"mode\\0")` would be answering a different
    question than the one asked.  The node scope is the other half: `usb_mode`
    is a 4-byte property in `twl4030-usb`, so the `prop_len == 4` gate does not
    discriminate here and the name alone would not either if it were searched.
    """
    h = _fdt_header(data, base)
    if h is None:
        return None

    strings = data[
        base + h["off_strings"] : base + h["off_strings"] + h["size_strings"]
    ]
    pos = base + h["off_struct"]
    end = pos + h["size_struct"]
    depth = 0
    usb_depth = None

    try:
        while pos + 4 <= end:
            token = be32(data, pos)
            if token == FDT_BEGIN_NODE:
                pos += 4
                nul = data.index(b"\0", pos, end)
                name = bytes(data[pos:nul])
                depth += 1
                if usb_depth is None and b"usb_otg_hs" in name:
                    usb_depth = depth
                pos = (nul + 1 + 3) & ~3
            elif token == FDT_END_NODE:
                if usb_depth is not None and depth == usb_depth:
                    usb_depth = None
                depth -= 1
                pos += 4
            elif token == FDT_PROP:
                prop_len = be32(data, pos + 4)
                nameoff = be32(data, pos + 8)
                pos += 12
                if usb_depth is not None and prop_len == 4:
                    nul = strings.index(b"\0", nameoff)
                    if bytes(strings[nameoff:nul]) == want:
                        return pos
                pos += (prop_len + 3) & ~3
            elif token == FDT_NOP:
                pos += 4
            elif token == FDT_END:
                break
            else:
                # An unknown token means this is not a struct block, so the
                # candidate was a false d00dfeed after all.
                return None
    except (ValueError, struct.error, IndexError):
        return None
    return None


def _power_offset_in(data, base):
    """Kept as the name the module has always exported for the power property."""
    return _prop_offset_in(data, base, b"power")


def find_prop_offset(data, want, hint=DTB_OFFSET_HINT):
    """Return (dtb_offset, value_offset) for a 4-byte property of the MUSB node.

    Tries `hint` first, then every d00dfeed in the file.  Raises UImageError if
    no candidate satisfies all three conditions in the module docstring —
    refusing rather than guessing, because the caller is about to write p1.

    ⚠️ The candidate is accepted on finding the REQUESTED property, so two calls
    for two different properties could in principle settle on two different
    d00dfeeds.  A caller patching both must assert the two dtb offsets are equal;
    patch_dtb.py does.
    """
    tried = []
    if hint is not None:
        tried.append(hint)
    start = 0
    magic = struct.pack(">I", FDT_MAGIC)
    while True:
        pos = data.find(magic, start)
        if pos < 0:
            break
        if pos not in tried:
            tried.append(pos)
        start = pos + 1

    for base in tried:
        off = _prop_offset_in(data, base, want)
        if off is not None:
            return base, off

    raise UImageError(
        "no device tree in this image carries a `%s` property inside a "
        "usb_otg_hs node (%d d00dfeed candidate(s) examined)"
        % (want.decode("ascii", "replace"), len(tried))
    )


def find_power_offset(data, hint=DTB_OFFSET_HINT):
    """Return (dtb_offset, power_value_offset) for the MUSB power property."""
    return find_prop_offset(data, b"power", hint=hint)


def find_mode_offset(data, hint=DTB_OFFSET_HINT):
    """Return (dtb_offset, mode_value_offset) for the MUSB `mode` property.

    `mode` selects MUSB's port mode: 1 = MUSB_PORT_MODE_HOST,
    2 = PERIPHERAL, 3 = DUAL_ROLE (musb_core.h:82-84).  The vendor DTB declares
    3 on a kernel built `# CONFIG_USB_GADGET is not set` with musb_gadget.c not
    compiled, so dual-role buys nothing this kernel can use and costs the
    SESSION bit in musb_start() (musb_core.c:1074-1080) plus the host claim in
    musb_host_setup() (musb_host.c:2789-2793).  IMPROVEMENT_PLAN.md B32.
    """
    return find_prop_offset(data, b"mode", hint=hint)
