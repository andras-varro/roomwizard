#!/bin/bash
#
# rw-identify.sh — recognise a RoomWizard SD card: its rootfs tree, and its disk.
#
# SOURCED, not executed:   . "$SCRIPT_DIR/rw-identify.sh"
#
# ── Why this is not a UUID ──────────────────────────────────────────────────
#
# A filesystem UUID is generated at mkfs time, so it identifies ONE CARD, not a
# model. Units are mkfs'd independently at the factory: two RoomWizards running
# the identical firmware build (/etc/version 20180309123456) share no UUID at
# all — not p6, not p2, p3 or p5. Matching a hardcoded UUID therefore recognises
# exactly the one unit whose card the constant was copied from, and rejects every
# other RoomWizard on earth. It also cannot be repaired by assigning the constant
# to the new card: two cards with one UUID is a worse bug than the one it hides.
#
# What is actually invariant, and what each function below uses:
#
#   rw_is_rootfs      CONTENT. The vendor firmware's own files.
#   rw_is_card_disk   LAYOUT. The partition table, which the vendor's own
#                     partitioning produces identically on every unit and which
#                     U-Boot depends on — `root=/dev/mmcblk0p6` is compiled into
#                     u-boot.bin with no saveenv, so p6 IS the rootfs by
#                     position and cannot be renumbered. See
#                     SYSTEM_ANALYSIS.md#42-partitions.
#
# Both are readable without mounting anything new and without writing anything.

# ── Content markers ─────────────────────────────────────────────────────────
#
# Split in two deliberately.
#
# RW_ROOTFS_REQUIRED are the files a caller EDITS. Their absence means the
# caller would fail partway through, so it is not merely an identity question.
#
# RW_ROOTFS_VENDOR is identity, and it is an OR because our own tooling deletes
# some of these. `setup-device.sh --remove` does `rm -rf /opt/pv02`, and
# --deep-clean removes more — yet pulling the card of a unit already in service
# to reset its password or rename it is a normal thing to do, and must still be
# recognised. /opt/sbin/watchdog and /etc/issue are never touched by anything in
# this repo (setup-device.sh keeps /opt/sbin/* deliberately), and
# /opt/roomwizard/ exists only once our own Phase 2 has run — so at least one of
# the four survives every state a card can be in.
RW_ROOTFS_REQUIRED="etc/shadow etc/hosts etc/ssh/sshd_config etc/network/interfaces"
RW_ROOTFS_VENDOR="opt/sbin/watchdog/watchdog.sh opt/pv02 opt/roomwizard"
RW_ISSUE_RE='RW20 Embedded Platform'

# ── Layout fingerprint ──────────────────────────────────────────────────────
#
# start,size in 512-byte sectors, from `sfdisk -d`. p1 p2 p3 p5 p6 are
# byte-identical on every unit measured. p4 (the extended container) and p7
# (swap) are NOT pinned: they absorb the difference in physical card size, and
# two cards of the same nominal 4 GB differ there (6586650 vs 6891885 sectors of
# p4). Pinning them would reject a genuine RoomWizard for being a slightly
# different card.
RW_LAYOUT="1:63,144522 2:144585,514080 3:658665,498015 5:1156743,2939832 6:4096638,2008062"

# ---------------------------------------------------------------------------
# rw_is_rootfs DIR
#
# 0 if DIR is the root of a RoomWizard rootfs. Silent; sets nothing.
# DIR may be "" or "/" for the live root — both mean the same tree.
# ---------------------------------------------------------------------------
rw_is_rootfs() {
    local d="${1:-}" m
    case "$d" in
        /) d="" ;;
        */) d="${d%/}" ;;
    esac

    for m in $RW_ROOTFS_REQUIRED; do
        [ -f "$d/$m" ] || return 1
    done

    for m in $RW_ROOTFS_VENDOR; do
        [ -e "$d/$m" ] && return 0
    done

    # Last resort, and the only marker that survives an arbitrarily aggressive
    # clean: the vendor's login banner. grep -q on a file that may not exist is
    # guarded rather than swallowed, so a caller running under `set -e` is safe.
    [ -f "$d/etc/issue" ] && grep -q "$RW_ISSUE_RE" "$d/etc/issue" 2>/dev/null && return 0

    return 1
}

# ---------------------------------------------------------------------------
# rw_rootfs_firmware DIR
#
# Echo a one-line human description of the firmware on DIR, for the operator to
# eyeball. Never used as a gate — a build string is not identity, and gating on
# it would reject a unit with different vendor firmware for no reason.
# ---------------------------------------------------------------------------
rw_rootfs_firmware() {
    local d="${1:-}" issue="" version=""
    case "$d" in
        /) d="" ;;
        */) d="${d%/}" ;;
    esac
    [ -f "$d/etc/issue" ]   && issue=$(head -1 "$d/etc/issue" | sed 's/[ \t]*\\[nl].*$//')
    [ -f "$d/etc/version" ] && version=$(head -1 "$d/etc/version" | tr -d ' \011\015\012')
    echo "${issue:-unknown firmware}${version:+ (build $version)}"
}

# ---------------------------------------------------------------------------
# rw_find_rootfs
#
# Echo the mount point of every mounted RoomWizard rootfs, one per line.
# Callers decide what 0, 1 or many means.
#
# "/" is EXCLUDED unconditionally. Commissioning is an offline, card-in-reader
# operation, so the live root is never the intended target — and a content scan,
# unlike the UUID lookup it replaces, could otherwise select the dev host's own
# root and rewrite its /etc/shadow. Excluding it makes that impossible rather
# than unlikely. To act on a live root deliberately, set ROOTFS=/ by hand.
# ---------------------------------------------------------------------------
rw_find_rootfs() {
    local target fstype
    # Fed by a here-doc rather than a pipe so the loop body runs in THIS shell:
    # a `findmnt | while read` would put it in a subshell, which matters as soon
    # as anyone adds a variable assignment to the body.
    while read -r target fstype; do
        [ -n "$target" ] || continue
        [ "$target" = "/" ] && continue
        case "$fstype" in
            ext2|ext3|ext4) ;;
            *) continue ;;
        esac
        rw_is_rootfs "$target" && echo "$target"
    done <<EOF
$(findmnt -rno TARGET,FSTYPE 2>/dev/null)
EOF
    # Explicit, because the loop's status is that of the last rw_is_rootfs call:
    # a final non-match would otherwise return 1 and kill a caller running under
    # `set -e`. "No cards found" is a normal result, not an error.
    return 0
}

# ---------------------------------------------------------------------------
# rw_is_rootfs_writable DIR
#
# 0 if DIR is mounted read-write. A card auto-mounted read-only is the one way
# detection can succeed and every subsequent edit fail, so callers check this
# and say so, rather than emitting a pile of "Read-only file system" errors.
# ---------------------------------------------------------------------------
rw_is_rootfs_writable() {
    local opts
    opts=$(findmnt -rno OPTIONS --mountpoint "$1" 2>/dev/null | head -1)
    case ",$opts," in
        *,ro,*) return 1 ;;
        *) return 0 ;;
    esac
}

# ---------------------------------------------------------------------------
# rw_is_card_disk DEVICE
#
# 0 if DEVICE (a whole disk or an image file) carries the RoomWizard partition
# layout. Reads the partition table only — nothing is mounted and nothing is
# written, so this is safe to run against any disk on the host.
# ---------------------------------------------------------------------------
rw_is_card_disk() {
    local dev="$1" table entry num want got
    [ -n "$dev" ] || return 1
    table=$(sfdisk -d "$dev" 2>/dev/null) || return 1
    [ -n "$table" ] || return 1

    for entry in $RW_LAYOUT; do
        num="${entry%%:*}"
        want="${entry#*:}"
        # sfdisk -d prints, with the padding shown:
        #   /dev/sdf6 : start=  4096638, size=  2008062, type=83
        # so `start=` and its value are separate awk fields. Matched with a
        # regex over the whole line instead of by field index, because the
        # padding is not guaranteed and `type=` may or may not be followed by
        # `bootable`.
        #
        # The partition number is anchored on a non-digit: a bare /p?6$/ also
        # matches /dev/sdf16, and a substring test on "${dev}${num}" matches
        # nothing at all on /dev/mmcblk0, whose partitions are p-prefixed.
        got=$(echo "$table" | awk -v n="$num" '
            $1 ~ ("(^|[^0-9])p?" n "$") && /start=/ {
                s = ""; z = ""
                if (match($0, /start=[ \t]*[0-9]+/)) {
                    s = substr($0, RSTART, RLENGTH); gsub(/[^0-9]/, "", s)
                }
                if (match($0, /size=[ \t]*[0-9]+/)) {
                    z = substr($0, RSTART, RLENGTH); gsub(/[^0-9]/, "", z)
                }
                print s "," z
                exit
            }')
        [ "$got" = "$want" ] || return 1
    done
    return 0
}

# ---------------------------------------------------------------------------
# rw_find_card_disks
#
# Echo every whole disk on the host that carries the RoomWizard layout, one
# "DEVICE SIZE" pair per line. Used only to make a "nothing is mounted" error
# actionable, so it is best-effort and prints nothing if lsblk is unavailable.
#
# The root disk is skipped, and by resolution rather than by name: on this host
# every disk reports removable=0 including the root disk, and a wsl --mount'ed
# card lands on the same /dev/sd? namespace as / — so a "removable only" filter
# rejects everything and a name filter rejects nothing. (CLAUDE.md, "Working
# from this host".)
# ---------------------------------------------------------------------------
rw_find_card_disks() {
    local rootdisk="" name size
    command -v lsblk >/dev/null 2>&1 || return 0
    rootdisk=$(lsblk -rnso NAME "$(findmnt -no SOURCE --target / 2>/dev/null)" 2>/dev/null | tail -1)

    while read -r name size; do
        [ -n "$name" ] || continue
        [ "$name" = "$rootdisk" ] && continue
        rw_is_card_disk "/dev/$name" && echo "/dev/$name $size"
    done <<EOF
$(lsblk -drno NAME,SIZE --nodeps 2>/dev/null)
EOF
    return 0   # see rw_find_rootfs: finding nothing is not an error
}
