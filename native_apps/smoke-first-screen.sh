#!/bin/bash
#
# smoke-first-screen.sh — launch each native app on a device, capture the first
# screen it draws before any input, and grade it.
#
#   ./native_apps/smoke-first-screen.sh <ip> [binary ...]
#   ./native_apps/smoke-first-screen.sh --list
#
# DEVICE ONLY to run, host-only to test: tests/smoke_first_screen_test.sh sources
# this file (the main-guard at the bottom keeps it from running) and drives every
# branch through an `ssh` stub on PATH. It is NOT a row in tests/run-all.sh,
# because it needs a device and that gate holds host-only tests.
#
# ── The five outcomes, kept apart because silence is not success ───────────
#
#   pass           alive after SMOKE_SETTLE_SECS and the visible page holds at
#                  least SMOKE_MIN_DISTINCT distinct pixel values
#   did-not-start  the binary is not executable, or exec failed (shell 126/127)
#   started-died   it ran and exited before the settle time — including SIGILL
#                  (132), the no-hardware-divide crash, which on the panel looks
#                  exactly like "did not start"
#   black-screen   alive, but the page has fewer than SMOKE_MIN_DISTINCT values
#   could-not-tell the harness could not measure: ssh failed, fbset unparsable,
#                  a depth/stride it does not model, a short capture, or another
#                  omapdss overlay enabled (see smoke_overlays_ok)
#
# ⚠️ "Not all black" alone is nearly vacuous — a screen cleared to one colour
# passes it — so the floor is a count of DISTINCT values. And the depth comes
# from `fbset` on the device every time, never assumed: a 32bpp frame is exactly
# the size of two 16bpp pages, so a capture's length can never reveal a wrong bpp.
#
# ── Stopping and restoring the running app ─────────────────────────────────
#
# `/etc/init.d/roomwizard-app stop` is the only implementation of "stop what is
# running" (it also stops the respawn loop); this script never killalls. The one
# process it kills is the PID it launched itself. `start` is run on every exit
# path once `stop` has been issued — normal end, failure, Ctrl-C, TERM.
#
# Exit: 0 every binary passed; 1 any binary did not; 2 usage or unreachable device.

# ── Scope: which binaries, with exactly the argv they get in production ─────
#
# The manifested apps are read from native_apps/app-manifests.sh (the one home of
# exec=/args=) and their args= mapped the way app_launcher.c's parse_args() maps
# it: empty means fb,touch (control_panel's empty args= is therefore fb,touch),
# "none" means no arguments, otherwise "fb" and/or "touch" by substring.
# app_launcher itself runs bare, as the init script starts it.
#
# The three interactive diagnostics (touch_raw, touch_trace, audio_mix_test) take
# the same <fb> <touch> pair and draw a UI before any input, so they are in scope.
#
# EXCLUDED, and refused if named:
#   fb_plane_bench — a benchmark: it renders a fixed number of frames, prints a
#                    timing and exits by design, so "alive after 2 s" is not its
#                    contract and a pass/fail here would mean nothing.
#   dss_scale_ab   — needs a mode argument and draws its subject on the DSS vid1
#                    overlay; `cat /dev/fb0` returns only the gfx plane, never the
#                    composited panel, so the capture cannot see what it shows.
#
# The directive below resolves the two sourced files from this script's directory.
# shellcheck source-path=SCRIPTDIR
SMOKE_EXTRA_TOOLS="touch_raw touch_trace audio_mix_test"
SMOKE_EXCLUDED="fb_plane_bench dss_scale_ab"
SMOKE_FB_DEV=/dev/fb0                     # what the apps are TOLD to draw on (production argv)
SMOKE_TOUCH_DEV=/dev/input/touchscreen0
# What the harness CAPTURES and reads geometry from — one variable, kept apart
# from the app argv so a later app that draws into a scaled video-plane fb can be
# graded by pointing this at that node (--capture-dev).
SMOKE_CAPTURE_DEV="${SMOKE_CAPTURE_DEV:-/dev/fb0}"
SMOKE_OVL_GLOB=/sys/devices/platform/omapdss/overlay

# ── The thresholds, in one place ───────────────────────────────────────────
# A black or single-colour screen is 1; a screen with one rectangle on a fill is 2-3.
# The real first screens measured far above this (see the device run in the commit
# that added this file), so the floor sits well clear of both.
SMOKE_MIN_DISTINCT="${SMOKE_MIN_DISTINCT:-16}"
SMOKE_SETTLE_SECS="${SMOKE_SETTLE_SECS:-2}"

SMOKE_SELF="${BASH_SOURCE[0]}"
SMOKE_NATIVE_DIR="$(cd "$(dirname "$SMOKE_SELF")" && pwd)"
SMOKE_REPO_DIR="$(cd "$SMOKE_NATIVE_DIR/.." && pwd)"

# ═══ Pure functions — no ssh, no device; the host test calls these directly ═══

# smoke_argmode <args-value>  →  "fb touch" | "fb" | "touch" | "" (app_launcher.c parse_args)
smoke_argmode() {
    local s="$1" m=""
    [ -z "$s" ] && { echo "fb touch"; return; }
    [ "$s" = none ] && { echo ""; return; }
    case "$s" in *fb*) m="fb" ;; esac
    case "$s" in *touch*) m="${m:+$m }touch" ;; esac
    [ -n "$m" ] && echo "$m" || echo "fb touch"
}

# smoke_targets  →  one "name|exec|argv" line per in-scope binary
smoke_targets() {
    local base exec args mode argv w t
    echo "app_launcher|/opt/roomwizard/app_launcher|"
    # shellcheck source=app-manifests.sh
    . "$SMOKE_NATIVE_DIR/app-manifests.sh"
    while IFS="|" read -r base _ exec _ args; do
        [ -n "$base" ] || continue
        mode=$(smoke_argmode "$args"); argv=""
        for w in $mode; do
            case "$w" in
                fb)    argv="${argv:+$argv }$SMOKE_FB_DEV" ;;
                touch) argv="${argv:+$argv }$SMOKE_TOUCH_DEV" ;;
            esac
        done
        echo "$base|$exec|$argv"
    done <<< "$RW_APP_MANIFESTS"
    for t in $SMOKE_EXTRA_TOOLS; do
        echo "$t|/opt/games/$t|$SMOKE_FB_DEV $SMOKE_TOUCH_DEV"
    done
}

# smoke_parse_fbset <fbset output>  →  "XRES YRES VXRES VYRES BPP", or return 1
smoke_parse_fbset() {
    local g
    g=$(printf '%s\n' "$1" | awk '$1 == "geometry" && NF >= 6 { print $2, $3, $4, $5, $6; exit }')
    printf '%s\n' "$g" | grep -Eq '^[0-9]+ [0-9]+ [0-9]+ [0-9]+ [0-9]+$' || return 1
    echo "$g"
}

# smoke_distinct <capture> <bpp> <xres> <yres> <yoffset>  →  count, or return 1
# when the capture is shorter than the page it must contain. Counts over exactly
# ONE page, the visible one. At 32bpp the X byte of XRGB8888 is masked off: it
# is not displayed, so it must not be able to make a black screen look varied.
smoke_distinct() {
    local f="$1" bpp="$2" xres="$3" yres="$4" yoff="$5"
    local bytes=$((bpp / 8)) page skip size
    page=$((xres * yres * bytes)); skip=$((yoff * xres * bytes))
    [ -f "$f" ] || return 1; size=$(wc -c < "$f") || return 1
    [ "$size" -ge $((skip + page)) ] || return 1
    case "$bpp" in
        32) tail -c +$((skip + 1)) "$f" | head -c "$page" | od -An -v -tx4 -w4 \
                | awk '{ print substr($1, 3) }' | sort -u | wc -l ;;
        16) tail -c +$((skip + 1)) "$f" | head -c "$page" | od -An -v -tx2 -w2 \
                | sort -u | wc -l ;;
        *)  return 1 ;;
    esac
}

# smoke_classify <launch-out> <fbinfo-out> <capture-file>
#   →  "<verdict> <detail>"; verdict is one of the five outcomes above.
# <launch-out> is what the remote launch command printed (SMOKE_* lines);
# <fbinfo-out> is what smoke_fbinfo_cmd printed: `fbset -fb <dev>`, then tagged
# lines STRIDE <n>, PAN <x,y>, FBOVL <ids> (fbN/overlays), OVL <id> <enabled>.
smoke_info_tag() { printf '%s\n' "$1" | sed -n "s/^$2 //p" | head -n1; }

# smoke_overlays_ok <fbinfo-out>  →  return 0 when every ENABLED omapdss overlay is
# one that scans out the captured fb; else print why and return 1.
#
# Why: the harness grades the app's own buffer, and `cat /dev/fbN` reads exactly
# that buffer — never the composited panel, which no capture on this SoC can
# reach. An app that renders into another plane (a video-plane fb the DSS scales
# up) leaves the captured buffer black or stale while the panel looks fine, so
# with any other overlay enabled a low count would be a false black-screen and a
# high one a false pass. Either way the honest verdict is could-not-tell.
smoke_overlays_ok() {
    local info="$1" fbovl id en
    fbovl=$(smoke_info_tag "$info" FBOVL)
    if [ -z "$fbovl" ] || ! printf '%s\n' "$info" | grep -q '^OVL [0-9]'; then
        echo "omapdss overlay state unreadable"; return 1
    fi
    while read -r _ id en; do
        [ "$en" = 1 ] || continue
        case ",$fbovl," in *",$id,"*) ;; *)
            echo "overlay$id is enabled and does not scan out the captured fb (its overlays: $fbovl)"
            return 1 ;;
        esac
    done <<< "$(printf '%s\n' "$info" | grep '^OVL ')"
    return 0
}

smoke_classify() {
    local launch="$1" info="$2" cap="$3"
    local code geo xres yres vx vy bpp stride pan yoff n why

    if printf '%s\n' "$launch" | grep -qx 'SMOKE_NOEXEC'; then
        echo "did-not-start binary missing or not executable"; return
    fi
    code=$(printf '%s\n' "$launch" | sed -n 's/^SMOKE_EXIT=\([0-9][0-9]*\)$/\1/p' | head -n1)
    if [ -n "$code" ]; then
        case "$code" in
            126|127) echo "did-not-start exec failed (exit $code)" ;;
            132)     echo "started-died exit 132 = SIGILL (hardware divide?)" ;;
            *)       echo "started-died exit $code within ${SMOKE_SETTLE_SECS}s" ;;
        esac
        return
    fi
    if ! printf '%s\n' "$launch" | grep -qx 'SMOKE_ALIVE'; then
        echo "could-not-tell launch printed no verdict (ssh failed?)"; return
    fi

    if ! geo=$(smoke_parse_fbset "$info"); then
        echo "could-not-tell fbset output has no parsable geometry line"; return
    fi
    read -r xres yres vx vy bpp <<< "$geo"
    case "$bpp" in 16|32) ;; *) echo "could-not-tell unmodelled depth ${bpp}bpp"; return ;; esac
    if ! why=$(smoke_overlays_ok "$info"); then
        echo "could-not-tell $why"; return
    fi
    stride=$(smoke_info_tag "$info" STRIDE)
    pan=$(smoke_info_tag "$info" PAN)
    if [ -n "$stride" ] && [ "$stride" != $((xres * bpp / 8)) ]; then
        echo "could-not-tell stride $stride != ${xres}x${bpp}bpp (padded or vxres $vx)"; return
    fi
    yoff=0
    if printf '%s\n' "$pan" | grep -Eq '^[0-9]+,[0-9]+$'; then
        yoff=${pan#*,}
    elif [ "$vy" -ne "$yres" ]; then
        echo "could-not-tell virtual height $vy > $yres and pan unreadable"; return
    fi
    if ! n=$(smoke_distinct "$cap" "$bpp" "$xres" "$yres" "$yoff"); then
        echo "could-not-tell capture shorter than one ${xres}x${yres}x${bpp}bpp page at y=$yoff"; return
    fi
    n=$((n + 0))
    if [ "$n" -lt "$SMOKE_MIN_DISTINCT" ]; then
        echo "black-screen $n distinct < $SMOKE_MIN_DISTINCT (${bpp}bpp)"; return
    fi
    echo "pass $n distinct (${xres}x${yres} ${bpp}bpp)"
}

# ═══ The device side ═══════════════════════════════════════════════════════

SMOKE_TARGET_HOST=""
SMOKE_STOPPED=0
SMOKE_LIVE_PID=""

# rw_ssh_gate has already proved key auth; like every other caller behind that gate
# this sets no BatchMode (lib/rw-ssh.sh: only the probes do).
# ⚠️ -n is load-bearing: smoke_main calls this inside `while read … <<< "$lines"`, and
# an ssh reading its stdin would swallow every remaining target after the first.
smoke_ssh() { ssh -n -o ConnectTimeout=5 "$SMOKE_TARGET_HOST" "$@"; }

# The remote command whose output smoke_classify reads as <fbinfo-out>. Attribute
# names are drivers/video/fbdev/omap2/omapfb/: overlayN/enabled (dss/overlay-sysfs.c,
# under the omapdss platform device) and fbN/overlays, a comma list of the overlay
# ids that fb feeds (omapfb-sysfs.c show_overlays).
smoke_fbinfo_cmd() {
    local s="/sys/class/graphics/${SMOKE_CAPTURE_DEV##*/}"
    printf '%s' "fbset -fb $SMOKE_CAPTURE_DEV; echo STRIDE \$(cat $s/stride); echo PAN \$(cat $s/pan);"
    printf '%s' " echo FBOVL \$(cat $s/overlays);"
    printf '%s' " for o in $SMOKE_OVL_GLOB*; do [ -r \$o/enabled ] && echo OVL \${o##*overlay} \$(cat \$o/enabled); done; true"
}

# Runs on every exit path once stop has been issued. Kills only the PID we started.
smoke_restore() {
    trap - EXIT INT TERM
    if [ -n "$SMOKE_LIVE_PID" ]; then
        smoke_ssh "kill $SMOKE_LIVE_PID 2>/dev/null" >/dev/null 2>&1
        SMOKE_LIVE_PID=""
    fi
    if [ "$SMOKE_STOPPED" = 1 ]; then
        echo "  restoring: /etc/init.d/roomwizard-app start"
        smoke_ssh "/etc/init.d/roomwizard-app start" >/dev/null 2>&1 \
            || echo "  ⚠️  restore failed — run: ssh $SMOKE_TARGET_HOST /etc/init.d/roomwizard-app start" >&2
        SMOKE_STOPPED=0
    fi
}

# smoke_one <name> <exec> <argv> <outdir>  →  prints the verdict line
smoke_one() {
    local name="$1" exe="$2" argv="$3" out="$4" launch info cap="$4/$1.raw" v pid
    # One remote shell: launch detached (all fds redirected so ssh returns), wait
    # the settle time, then report alive or the exit status `wait` recovers.
    launch=$(smoke_ssh "b=$exe; [ -x \"\$b\" ] || { echo SMOKE_NOEXEC; exit 0; }
\"\$b\" $argv </dev/null >/tmp/smoke-$name.log 2>&1 &
p=\$!; echo SMOKE_PID=\$p; sleep $SMOKE_SETTLE_SECS
if kill -0 \$p 2>/dev/null; then echo SMOKE_ALIVE; else wait \$p; echo SMOKE_EXIT=\$?; fi" 2>/dev/null)
    pid=$(printf '%s\n' "$launch" | sed -n 's/^SMOKE_PID=\([0-9][0-9]*\)$/\1/p' | head -n1)
    if printf '%s\n' "$launch" | grep -qx SMOKE_ALIVE; then
        SMOKE_LIVE_PID="$pid"
        info=$(smoke_ssh "$(smoke_fbinfo_cmd)" 2>/dev/null)
        smoke_ssh "cat $SMOKE_CAPTURE_DEV" > "$cap" 2>/dev/null || : > "$cap"
    else
        info=""; : > "$cap"
    fi
    v=$(smoke_classify "$launch" "$info" "$cap")
    if [ -n "$SMOKE_LIVE_PID" ]; then
        smoke_ssh "kill $SMOKE_LIVE_PID 2>/dev/null; sleep 1; kill -9 $SMOKE_LIVE_PID 2>/dev/null; true" >/dev/null 2>&1
        SMOKE_LIVE_PID=""
    fi
    smoke_png "$name" "$cap" "$out" "$v" "$info"
    printf '  %-16s %s\n' "$name" "$v"
}

# Optional PNG — only when python3 with PIL really runs (Git Bash's python3 is an
# App Execution Alias that does not). The verdict never depends on it.
smoke_png() {
    local name="$1" cap="$2" out="$3" v="$4" geo bpp
    case "$v" in pass*|black-screen*) ;; *) return ;; esac
    [ -z "${SMOKE_NO_PNG:-}" ] || return
    python3 -c 'import PIL' >/dev/null 2>&1 || return
    geo=$(smoke_parse_fbset "$5") || return
    bpp=${geo##* }
    # fb565_to_png.py assumes 800x480; skip any other geometry rather than mis-decode.
    case "$geo" in "800 480 "*) ;; *) return ;; esac
    python3 "$SMOKE_REPO_DIR/fb565_to_png.py" "$cap" "$out/$name.png" --bpp "$bpp" >/dev/null 2>&1
}

smoke_usage() {
    cat <<'EOF'
Usage: native_apps/smoke-first-screen.sh <ip> [binary ...]
       native_apps/smoke-first-screen.sh --list

Stops the running app (/etc/init.d/roomwizard-app stop), launches each binary
with its production argv, checks it is alive after SMOKE_SETTLE_SECS (default 2),
captures /dev/fb0, grades the visible page by its count of distinct pixel values
(SMOKE_MIN_DISTINCT, default 16), and restarts the app on every exit path.

Verdicts: pass | did-not-start | started-died | black-screen | could-not-tell
Exit: 0 all passed, 1 any did not, 2 usage error or device unreachable.

  --out DIR   keep captures (and PNGs, if python3+PIL exists) in DIR
              (default: a fresh mktemp -d, printed at the end)
  --capture-dev DEV
              the fb node to capture and read geometry from (default /dev/fb0).
              Only the app's own buffer is graded: if any omapdss overlay other
              than the ones DEV feeds is enabled, the verdict is could-not-tell.
EOF
}

smoke_main() {
    local out="" names=() t name exe argv sel v pass=0 total=0 lines
    case "${1:-}" in
        -h|--help) smoke_usage; return 0 ;;
        --list) smoke_targets | while IFS='|' read -r name exe argv; do
                    printf '  %-16s %s %s\n' "$name" "$exe" "$argv"; done
                echo "  excluded: $SMOKE_EXCLUDED"; return 0 ;;
        ""|-*) smoke_usage >&2; return 2 ;;
    esac
    SMOKE_TARGET_HOST="$1"; shift
    case "$SMOKE_TARGET_HOST" in *@*) ;; *) SMOKE_TARGET_HOST="root@$SMOKE_TARGET_HOST" ;; esac
    while [ $# -gt 0 ]; do
        case "$1" in
            --out) out="${2:-}"; shift 2 || { smoke_usage >&2; return 2; } ;;
            --capture-dev) SMOKE_CAPTURE_DEV="${2:-}"; shift 2 || { smoke_usage >&2; return 2; } ;;
            *) names+=("$1"); shift ;;
        esac
    done

    lines=$(smoke_targets)
    for name in "${names[@]}"; do
        case " $SMOKE_EXCLUDED " in *" $name "*)
            echo "smoke: $name is excluded from this harness (see the header for why)" >&2; return 2 ;; esac
        printf '%s\n' "$lines" | grep -q "^$name|" \
            || { echo "smoke: unknown binary '$name' — see --list" >&2; return 2; }
    done
    [ -n "$out" ] || out=$(mktemp -d)
    mkdir -p "$out" || return 2

    # shellcheck source=../lib/rw-ssh.sh
    . "$SMOKE_REPO_DIR/lib/rw-ssh.sh"
    rw_ssh_gate "$SMOKE_TARGET_HOST" || return 2

    trap 'smoke_restore' EXIT
    trap 'smoke_restore; exit 130' INT
    trap 'smoke_restore; exit 143' TERM
    SMOKE_STOPPED=1
    echo "  stopping: /etc/init.d/roomwizard-app stop"
    smoke_ssh "/etc/init.d/roomwizard-app stop" >/dev/null 2>&1 \
        || echo "  ⚠️  stop reported failure; continuing — verdicts may be could-not-tell" >&2

    while IFS='|' read -r name exe argv; do
        if [ ${#names[@]} -gt 0 ]; then
            sel=0; for t in "${names[@]}"; do [ "$t" = "$name" ] && sel=1; done
            [ "$sel" = 1 ] || continue
        fi
        v=$(smoke_one "$name" "$exe" "$argv" "$out")
        echo "$v"
        total=$((total + 1))
        case "$v" in *" pass "*) pass=$((pass + 1)) ;; esac
    done <<< "$lines"

    smoke_restore
    echo ""
    echo "  $pass of $total passed  (captures: $out)"
    [ "$total" -gt 0 ] || { echo "  no binary was graded" >&2; return 2; }
    [ "$pass" -eq "$total" ]
}

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    smoke_main "$@"
    exit $?
fi
