#!/bin/bash
#
# lib/rw-usbpower.sh — raise the USB power budget from 100 mA to 500 mA by
#                      patching uImage-system on p1, safely, from either transport.
#
# SOURCED, not executed:   . "$REPO_ROOT/lib/rw-usbpower.sh"
#                          (needs lib/rw-identify.sh for the offline mount half)
#
# IMPROVEMENT_PLAN.md F15.
#
# ── What this is for, and what it is NOT ────────────────────────────────────
#
# usb_host is three independent mechanisms and only ONE of them is here:
#
#   the /dev/mem patch of omap2430_ops   -> device-files/enable-usb-host.sh, p6
#   xpad/joydev/ff-memless               -> the bundle, p6
#   the 500 mA power budget              -> THIS FILE, p1
#
# The first two are ordinary provision records and bundle artifacts. Do not treat
# the p1 rule as a blocker on USB as a whole; it blocks exactly the power budget,
# i.e. whether a controller works without a powered hub.
#
# ── Why it cannot be a provision rule ──────────────────────────────────────
#
# device-files/provision-rules.conf can express "write these bytes at this mode".
# This is not that: the bytes are DERIVED from the file already on the card
# (patch_dtb.py), gated on that file's md5, backed up, verified after the write and
# rolled back on failure. Six steps with a conditional and an undo.
#
# ⚠️ Which has a consequence worth naming: this is the one step
# tests/rw_provision_test.sh group E — "both executors' dry runs print the same
# resolved set" — cannot compare. That comparison is what catches C12-style drift,
# so the protection here is that BOTH callers run this ONE function and only the
# transport differs. Never write a second copy of the sequence into a caller.
#
# ── Never ship the vendor kernel ───────────────────────────────────────────
#
# uImage-system is a 5.2 MB Steelcase binary and this repo is meant to be
# published, so the patched image is DERIVED on the spot from the device's own
# copy rather than bundled. release.sh refuses any manifest entry naming it — the
# negative control for that rule. The md5 gate on the input plus the md5 assert on
# the output makes deriving it a complete check, not a weaker one.
#
# ── The measured constants ─────────────────────────────────────────────────
#
# Measured 2026-08-08 across FIVE sources on THREE units — p1 of both full-card
# captures, both units' p5 factory-restore payloads, and RW09's own copy: the
# vendor uImage-system is byte-identical everywhere. Nothing generates it per
# unit, unlike the filesystem UUIDs. That is what makes an md5 gate sound rather
# than a fingerprint of one card.
RW_UIMAGE_NAME="uImage-system"
RW_UIMAGE_BACKUP="uImage-system.vendor"
RW_UIMAGE_VENDOR_MD5="edc637ac14f90e0187b1ed65ffedf6d7"
RW_UIMAGE_PATCHED_MD5="a1fd1af8da18c430a34b24762aa16dab"

# ---------------------------------------------------------------------------
# rw_usbpower_tool NAME
#
# Resolve usb_host/<NAME> from this file's own location, so a caller in a
# subdirectory or invoked through a symlink still finds it and nobody has to pass
# a repo root.
# ---------------------------------------------------------------------------
rw_usbpower_tool() {
    local d
    d=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." && pwd)
    echo "$d/usb_host/$1"
}

# ---------------------------------------------------------------------------
# rw_usbpower_prereqs
#
# Echo every missing prerequisite; return 1 if there were any. python3 is the
# only one — no mkimage, no dtc (neither is installed in this WSL, and neither is
# needed: verify_uimage.py does both jobs in pure Python).
# ---------------------------------------------------------------------------
rw_usbpower_prereqs() {
    local bad=0 t
    if ! command -v python3 >/dev/null 2>&1; then
        echo "  python3 is not installed — the uImage patch and its verifier are Python"
        bad=1
    fi
    for t in patch_dtb.py verify_uimage.py uimage.py; do
        if [ ! -f "$(rw_usbpower_tool "$t")" ]; then
            echo "  missing $(rw_usbpower_tool "$t")"
            bad=1
        fi
    done
    [ "$bad" = 0 ]
}

# ---------------------------------------------------------------------------
# rw_usbpower_classify MD5
#
# Echo "vendor", "patched" or "unknown". Return 0 for the first two, 1 for the
# third — so a caller can `case` on the word and still be safe under `set -e`.
#
# ⚠️ "unknown" is a REFUSAL, not a reason to patch anyway. A third md5 means this
# is not the firmware the 9-byte diff was measured against, and patch_dtb.py's own
# value check would be the only thing left standing.
# ---------------------------------------------------------------------------
rw_usbpower_classify() {
    case "$1" in
        "$RW_UIMAGE_VENDOR_MD5")  echo vendor;  return 0 ;;
        "$RW_UIMAGE_PATCHED_MD5") echo patched; return 0 ;;
        *)                        echo unknown; return 1 ;;
    esac
}

# ═══════════════════════════════════════════════════════════════════════════
# The transport, which is the ONLY thing that differs between the two callers
# ═══════════════════════════════════════════════════════════════════════════
#
# RWUP_XPORT is "local" (p1 mounted on this host, paths are host paths) or "ssh"
# (p1 mounted on the device, paths are device paths and RWUP_TARGET is root@ip).
#
# $RW_SSH and $RW_SCP override the two commands, empty in production, which is how
# tests/rw_usbpower_test.sh drives the ssh half against a directory on this host —
# the same approach rw_bundle_install_ssh and rw_provision_online_script take, and
# for the same reason: an installer whose only test is "it worked on a device
# once" has no test.

_rwup_ssh() { ${RW_SSH:-ssh} "$RWUP_TARGET" "$@"; }

# _rwup_md5 PATH — echo the md5 of PATH on the far side, or nothing if absent.
_rwup_md5() {
    case "$RWUP_XPORT" in
        local) [ -f "$1" ] && md5sum "$1" 2>/dev/null | cut -d' ' -f1 ;;
        ssh)   _rwup_ssh "md5sum '$1' 2>/dev/null" | cut -d' ' -f1 ;;
    esac
    return 0
}

# _rwup_pull FAR LOCAL
_rwup_pull() {
    case "$RWUP_XPORT" in
        local) cp "$1" "$2" ;;
        ssh)   ${RW_SCP:-scp} -q "$RWUP_TARGET:$1" "$2" ;;
    esac
}

# _rwup_push LOCAL FAR
_rwup_push() {
    case "$RWUP_XPORT" in
        local) cp "$1" "$2" ;;
        ssh)   ${RW_SCP:-scp} -q "$1" "$RWUP_TARGET:$2" ;;
    esac
}

# _rwup_cp FAR FAR, _rwup_mv FAR FAR, _rwup_rm FAR — in place on the far side.
_rwup_cp() {
    case "$RWUP_XPORT" in
        local) cp "$1" "$2" ;;
        ssh)   _rwup_ssh "cp '$1' '$2'" ;;
    esac
}
_rwup_mv() {
    case "$RWUP_XPORT" in
        local) mv "$1" "$2" ;;
        ssh)   _rwup_ssh "mv '$1' '$2'" ;;
    esac
}
_rwup_rm() {
    case "$RWUP_XPORT" in
        local) rm -f "$1" ;;
        ssh)   _rwup_ssh "rm -f '$1'" ;;
    esac
    return 0
}
_rwup_sync() {
    case "$RWUP_XPORT" in
        local) sync ;;
        ssh)   _rwup_ssh "sync" ;;
    esac
    return 0
}

# ---------------------------------------------------------------------------
# rw_usbpower_apply BOOTDIR WORKDIR
#
# THE sequence. BOOTDIR is the directory holding uImage-system on the far side;
# WORKDIR is a scratch directory on THIS host. RWUP_XPORT (and RWUP_TARGET for
# ssh) must be set. RW_USBPOWER_DRY=1 reports the verdict and writes nothing.
#
# Returns 0 when p1 ends up carrying the 500 mA image (including the idempotent
# "it already did" case), 1 otherwise — and on 1 it has either written nothing or
# restored the vendor image, and says which.
#
# Every step and why it is there:
#
#   1  the file exists                  — a BOOTDIR without it is not p1
#   2  md5 gate, three outcomes         — patched: skip. unknown: refuse. vendor: go
#   3  pull, and re-check the md5       — a truncated transfer is exactly what a
#                                         600 MHz device over scp can produce
#   4  patch_dtb.py                     — derives, never ships, the kernel
#   5  verify_uimage.py + md5 assert    — both CRCs and the power value, on the
#                                         candidate, BEFORE it is anywhere near p1
#   6  back up, then verify the BACKUP  — ⚠️ before the original is touched. A
#                                         backup nobody checked is not a backup
#   7  push to a .new, verify it there  — so a bad transfer never lands on the
#                                         name bootcmd is hardcoded to
#   8  mv into place, sync
#   9  re-read from the card            — the post-commissioning validation. vfat
#                                         write-back means step 7's verdict was
#                                         about a cache, not about the card
#   10 on any failure past step 6       — restore, verify the restore, and refuse
#                                         LOUDLY. p1 has no recovery over SSH
# ---------------------------------------------------------------------------
rw_usbpower_apply() {
    local bootdir="$1" work="$2"
    local img bak new cur state got

    [ -n "$bootdir" ] || { echo "  rw_usbpower_apply: no boot directory"; return 1; }
    [ -n "$work" ] && [ -d "$work" ] || { echo "  rw_usbpower_apply: no work directory"; return 1; }
    case "$RWUP_XPORT" in
        local|ssh) ;;
        *) echo "  rw_usbpower_apply: RWUP_XPORT must be 'local' or 'ssh', not '${RWUP_XPORT:-}'"; return 1 ;;
    esac
    if [ "$RWUP_XPORT" = "ssh" ] && [ -z "${RWUP_TARGET:-}" ]; then
        echo "  rw_usbpower_apply: the ssh transport needs RWUP_TARGET"
        return 1
    fi

    bootdir="${bootdir%/}"
    img="$bootdir/$RW_UIMAGE_NAME"
    bak="$bootdir/$RW_UIMAGE_BACKUP"
    new="$bootdir/$RW_UIMAGE_NAME.new"

    # ── 1 + 2: the gate ──
    cur=$(_rwup_md5 "$img")
    if [ -z "$cur" ]; then
        echo "  no $RW_UIMAGE_NAME at $bootdir — that is not a mounted p1"
        return 1
    fi
    state=$(rw_usbpower_classify "$cur") || true
    case "$state" in
        patched)
            echo "  $RW_UIMAGE_NAME is already the 500 mA image ($cur) — nothing to do"
            return 0
            ;;
        vendor) ;;
        *)
            echo "  $RW_UIMAGE_NAME md5 is $cur, which is neither the vendor image"
            echo "      ($RW_UIMAGE_VENDOR_MD5) nor the patched one"
            echo "      ($RW_UIMAGE_PATCHED_MD5). REFUSING to write p1."
            echo "      Nothing was changed. This is not the firmware the patch was"
            echo "      measured against — say what this unit is before overriding."
            return 1
            ;;
    esac

    if [ -n "${RW_USBPOWER_DRY:-}" ]; then
        echo "  would patch    $img  (vendor $cur -> $RW_UIMAGE_PATCHED_MD5)"
        echo "  would back up  $bak"
        return 0
    fi

    if ! rw_usbpower_prereqs; then
        echo "  cannot patch p1 without the above"
        return 1
    fi

    # ── 3: pull, and re-check ──
    if ! _rwup_pull "$img" "$work/vendor"; then
        echo "  could not read $img"
        return 1
    fi
    got=$(md5sum "$work/vendor" | cut -d' ' -f1)
    if [ "$got" != "$RW_UIMAGE_VENDOR_MD5" ]; then
        echo "  the copy of $RW_UIMAGE_NAME that arrived here is $got, not $cur —"
        echo "      the transfer was truncated or altered. Nothing was changed."
        return 1
    fi

    # ── 4 + 5: derive and check the candidate, before p1 is involved ──
    if ! python3 "$(rw_usbpower_tool patch_dtb.py)" "$work/vendor" "$work/patched" \
            | sed 's/^/      /'; then
        echo "  patch_dtb.py failed — nothing was changed"
        return 1
    fi
    if ! python3 "$(rw_usbpower_tool verify_uimage.py)" "$work/patched" \
            --expect-power 0xfa | sed 's/^/      /'; then
        echo "  the patched image does not verify — nothing was changed"
        return 1
    fi
    got=$(md5sum "$work/patched" | cut -d' ' -f1)
    if [ "$got" != "$RW_UIMAGE_PATCHED_MD5" ]; then
        echo "  the patched image is $got, expected $RW_UIMAGE_PATCHED_MD5 —"
        echo "      refusing. Nothing was changed."
        return 1
    fi
    echo "  patched image built and verified ($got)"

    # ── 6: the backup, verified BEFORE the original is touched ──
    got=$(_rwup_md5 "$bak")
    if [ -z "$got" ]; then
        _rwup_cp "$img" "$bak" || { echo "  could not create $bak — nothing was changed"; return 1; }
        got=$(_rwup_md5 "$bak")
    fi
    if [ "$got" != "$RW_UIMAGE_VENDOR_MD5" ]; then
        echo "  $RW_UIMAGE_BACKUP is $got, not the vendor image — refusing to write p1"
        echo "      with no working undo. Nothing was changed. Move that file aside"
        echo "      and re-run, or restore the vendor kernel by hand first."
        return 1
    fi
    echo "  vendor kernel backed up to $RW_UIMAGE_BACKUP and verified"

    # ── 7: land it under a temporary name and check it THERE ──
    if ! _rwup_push "$work/patched" "$new"; then
        _rwup_rm "$new"
        echo "  could not write $new — $RW_UIMAGE_NAME is untouched"
        return 1
    fi
    got=$(_rwup_md5 "$new")
    if [ "$got" != "$RW_UIMAGE_PATCHED_MD5" ]; then
        _rwup_rm "$new"
        echo "  $new arrived as $got — $RW_UIMAGE_NAME is untouched"
        return 1
    fi

    # ── 8 + 9: into place, then re-read from the card ──
    if ! _rwup_mv "$new" "$img"; then
        _rwup_rm "$new"
        echo "  could not move $new into place — $RW_UIMAGE_NAME is untouched"
        return 1
    fi
    _rwup_sync
    got=$(_rwup_md5 "$img")
    if [ "$got" = "$RW_UIMAGE_PATCHED_MD5" ]; then
        echo "  $RW_UIMAGE_NAME is now the 500 mA image ($got), verified by re-reading it"
        echo "  a reboot is required before the device tree change is live"
        return 0
    fi

    # ── 10: rollback ──
    echo ""
    echo "  ✗ $RW_UIMAGE_NAME reads back as $got, not $RW_UIMAGE_PATCHED_MD5."
    echo "    Restoring the vendor kernel from $RW_UIMAGE_BACKUP."
    if _rwup_cp "$bak" "$img"; then
        _rwup_sync
        got=$(_rwup_md5 "$img")
        if [ "$got" = "$RW_UIMAGE_VENDOR_MD5" ]; then
            echo "    Restored: $RW_UIMAGE_NAME is the vendor image again ($got)."
            echo "    The unit will boot as before, at 100 mA. Do not re-run this"
            echo "    against the same card until the write failure is understood."
            return 1
        fi
    fi
    echo ""
    echo "  ✗✗ THE RESTORE ALSO FAILED. p1 IS IN AN UNKNOWN STATE ($got)."
    echo "     DO NOT BOOT THIS UNIT. There is no serial console and no SSH before"
    echo "     the kernel loads, so a bad uImage-system is only fixable with a card"
    echo "     reader. Put the card in one and either copy $RW_UIMAGE_BACKUP over"
    echo "     $RW_UIMAGE_NAME, or dd the full-card backup back."
    return 1
}

# ---------------------------------------------------------------------------
# rw_usbpower_apply_ssh TARGET WORKDIR
#
# The live half: mount p1 on the DEVICE, run the sequence over scp, unmount.
#
# The device does the mounting because it is the device's own card — /dev/mmcblk0p1
# by position, the same way root=/dev/mmcblk0p6 is compiled into u-boot.bin.
# ---------------------------------------------------------------------------
rw_usbpower_apply_ssh() {
    local target="$1" work="$2" rc=0 mnt="/tmp/rw-boot"

    [ -n "$target" ] || { echo "  rw_usbpower_apply_ssh: no target"; return 1; }

    RWUP_XPORT=ssh
    RWUP_TARGET="$target"

    if ! _rwup_ssh "mkdir -p $mnt && mount -t vfat /dev/mmcblk0p1 $mnt 2>/dev/null || mountpoint -q $mnt"; then
        echo "  could not mount /dev/mmcblk0p1 on the device — p1 not patched"
        unset RWUP_XPORT RWUP_TARGET
        return 1
    fi
    # Content check, so a mount that succeeded against the wrong thing is caught
    # before anything is written.
    if ! _rwup_ssh "[ -f $mnt/$RW_UIMAGE_NAME ] && [ -f $mnt/mlo ]"; then
        echo "  $mnt on the device does not carry $RW_UIMAGE_NAME and mlo — refusing"
        _rwup_ssh "umount $mnt 2>/dev/null" || true
        unset RWUP_XPORT RWUP_TARGET
        return 1
    fi

    rw_usbpower_apply "$mnt" "$work" || rc=1

    # Always unmounted, on both paths: a device left with p1 mounted rw is a
    # device that corrupts it on the next hard reset.
    _rwup_ssh "sync; umount $mnt 2>/dev/null; rmdir $mnt 2>/dev/null" || true
    unset RWUP_XPORT RWUP_TARGET
    return "$rc"
}

# ---------------------------------------------------------------------------
# rw_usbpower_apply_offline BOOTMNT WORKDIR
#
# The offline half: p1 is already mounted on THIS host at BOOTMNT (by
# rw_mount_boot, whose caller owns the unmount, including from its failure trap).
# ---------------------------------------------------------------------------
rw_usbpower_apply_offline() {
    local bootmnt="$1" work="$2" rc=0

    [ -n "$bootmnt" ] || { echo "  rw_usbpower_apply_offline: no boot mount"; return 1; }
    if ! rw_is_boot_tree "$bootmnt"; then
        echo "  $bootmnt does not carry $RW_BOOT_MARKERS — that is not a mounted p1"
        return 1
    fi

    RWUP_XPORT=local
    rw_usbpower_apply "$bootmnt" "$work" || rc=1
    unset RWUP_XPORT
    return "$rc"
}
