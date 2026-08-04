#!/bin/sh
#
# set-hostname.sh — set a RoomWizard's host name, in /etc/hostname AND /etc/hosts
#
# ONE implementation, called from two places with different connection models:
#
#   commission-roomwizard.sh    offline, against a mounted card:
#                                   sudo ./set-hostname.sh NAME /mnt/rw
#   setup-device.sh --hostname  over SSH, against the live root:
#                                   ./set-hostname.sh NAME
#
# It lives in its own file rather than in either caller because the two paths
# would otherwise carry the same 30 lines of /etc/hosts rewriting and drift
# apart; and because a standalone script can be exercised on the host against a
# copy of a real vendor rootfs, with no SD card and no device.
#
# Why /etc/hosts is rewritten and not just /etc/hostname: the vendor image ships
#
#     127.0.0.1 localhost
##
# Usage: set-hostname.sh NAME [ROOTFS]
#   NAME    RFC-1123 single label. No dots — mDNS appends .local itself, and a
#           dotted /etc/hostname is what produced the mess above.
#   ROOTFS  prefix to edit under. Empty or absent means the live root, in which
#           case the running kernel's name is set too.
#
# POSIX sh only, no bashisms: this runs under BusyBox ash on the device.

set -e

NAME="$1"
ROOTFS="${2:-}"

# Strip a trailing slash so "$ROOTFS/etc/hosts" never becomes "//etc/hosts".
case "$ROOTFS" in
    */) ROOTFS="${ROOTFS%/}" ;;
esac

usage() {
    echo "Usage: set-hostname.sh NAME [ROOTFS]" >&2
    exit 1
}

if [ -z "$NAME" ]; then
    echo "set-hostname.sh: no NAME given" >&2
    usage
fi

# ── validate ────────────────────────────────────────────────────────────────
# RFC-1123, single label. Length is checked with ${#NAME} and the charset with a
# plain ERE that needs no {n,m} interval, so this does not depend on how the
# device's BusyBox grep was built.
if [ "${#NAME}" -gt 63 ]; then
    echo "set-hostname.sh: '$NAME' is ${#NAME} characters; the limit is 63." >&2
    exit 1
fi
if ! printf '%s' "$NAME" | grep -qE '^[a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?$'; then
    echo "set-hostname.sh: '$NAME' is not a valid host name." >&2
    echo "  Allowed: letters, digits and hyphens; must start and end alphanumeric." >&2
    echo "  No dots — set 'rw09', not 'rw09.local'; mDNS adds the .local itself." >&2
    exit 1
fi

HOSTNAME_FILE="$ROOTFS/etc/hostname"
HOSTS_FILE="$ROOTFS/etc/hosts"

if [ ! -f "$HOSTS_FILE" ]; then
    echo "set-hostname.sh: $HOSTS_FILE does not exist — is '$ROOTFS' a rootfs?" >&2
    exit 1
fi

# The name being replaced. Needed because the stale mapping is keyed by the OLD
# name, so it cannot be found without reading it first.
OLD=""
if [ -f "$HOSTNAME_FILE" ]; then
    OLD=$(head -1 "$HOSTNAME_FILE" | tr -d ' \011\015\012')
fi

TMP="$HOSTS_FILE.tmp.$$"
trap 'rm -f "$TMP"' EXIT INT TERM

# ── /etc/hosts ──────────────────────────────────────────────────────────────
# Back up ONCE. A second run must not overwrite the vendor original with this
# script's own previous output — that would destroy the only record of what the
# image shipped.
[ -f "$HOSTS_FILE.backup" ] || cp "$HOSTS_FILE" "$HOSTS_FILE.backup"

# Comments and blank lines are passed through untouched,
# and loopback lines are never dropped — the localhost entry is not ours to
# remove, and the assertion below depends on it surviving.
awk -v old="$OLD" -v new="$NAME" '
    function shortlower(h) { sub(/\..*$/, "", h); return tolower(h) }
    /^[ \t]*#/ || /^[ \t]*$/ { print; next }
    {
        keep = 1
        if ($1 != "127.0.0.1" && $1 != "::1") {
            for (i = 2; i <= NF; i++) {
                s = shortlower($i)
                if ((old != "" && s == tolower(old)) || s == tolower(new)) keep = 0
            }
        }
        if (keep) print
    }
' "$HOSTS_FILE" > "$TMP"

# Add the loopback mapping unless some 127.0.0.1 line already carries the name.
if ! awk -v new="$NAME" '
        $1 == "127.0.0.1" { for (i = 2; i <= NF; i++) if (tolower($i) == tolower(new)) found = 1 }
        END { exit(found ? 0 : 1) }
    ' "$TMP"; then
    echo "127.0.0.1 $NAME" >> "$TMP"
fi

# Negative control, same shape as the loopback-stanza guard in
# commission-roomwizard.sh: assert the thing the filter must never remove is
# still there, rather than trusting the filter. Losing the localhost entry is
# the one way this script could break a booting device.
LOCALHOST_RE='(^|[ \t])localhost([ \t]|$)'
if grep -qiE "$LOCALHOST_RE" "$HOSTS_FILE" && ! grep -qiE "$LOCALHOST_RE" "$TMP"; then
    echo "set-hostname.sh: refusing to write $HOSTS_FILE — the localhost entry" >&2
    echo "  would be lost. The file is unchanged (backup: $HOSTS_FILE.backup)." >&2
    exit 1
fi

# cp onto the existing file so its mode and owner are preserved.
cp "$TMP" "$HOSTS_FILE"
rm -f "$TMP"

# ── /etc/hostname ───────────────────────────────────────────────────────────
if [ -f "$HOSTNAME_FILE" ] && [ ! -f "$HOSTNAME_FILE.backup" ]; then
    cp "$HOSTNAME_FILE" "$HOSTNAME_FILE.backup"
fi
printf '%s\n' "$NAME" > "$HOSTNAME_FILE"

# ── running kernel ──────────────────────────────────────────────────────────
# Only meaningful for the live root; offline there is no kernel to tell.
if [ -z "$ROOTFS" ]; then
    hostname "$NAME"
fi

if [ -n "$OLD" ] && [ "$OLD" != "$NAME" ]; then
    echo "  host name: $OLD -> $NAME"
else
    echo "  host name: $NAME"
fi
echo "  $HOSTNAME_FILE and $HOSTS_FILE updated (backups: *.backup)"
