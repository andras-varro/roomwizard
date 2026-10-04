#!/bin/bash
#
# smoke_first_screen_test.sh — host regression for native_apps/smoke-first-screen.sh
#
# Host-only, no device, no root. The runner needs a device to RUN but not to be
# proven: every outcome branch is driven here, the classifier directly over
# fixture captures, the whole script through an `ssh` stub on PATH that plays a
# scripted device per scenario.
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && ./tests/smoke_first_screen_test.sh"
#
#   A  smoke_argmode / smoke_targets — the argv each binary gets matches what
#      app_launcher.c's parse_args() gives it, and the scope is what the header says
#   B  smoke_classify — each of the five outcomes, from fixture captures
#   C  depth and geometry come from fbset, never assumed: the SAME bytes grade
#      differently at 16 and 32bpp, a short capture and an unparsable fbset are
#      could-not-tell, the visible page follows the pan offset
#   D  another omapdss overlay enabled is could-not-tell, never black-screen
#   E  the threshold is a floor on DISTINCT values, at its exact boundary
#   F  the whole runner through the stub: verdict lines, exit codes, and the init
#      script's `start` on every path that issued `stop` — including failure and TERM
#
# The directive below resolves the sourced runner from this script's directory.
# shellcheck source-path=SCRIPTDIR
set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
# $SMOKE_RUNNER points the suite at a staged copy — the hook a sabotage run uses.
RUNNER="${SMOKE_RUNNER:-$REPO_DIR/native_apps/smoke-first-screen.sh}"

# shellcheck source=../native_apps/smoke-first-screen.sh
. "$RUNNER"

RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
PASS=0; FAIL=0
ok()  { PASS=$((PASS + 1)); echo -e "  ${GREEN}pass${NC}  $1"; }
bad() { FAIL=$((FAIL + 1)); echo -e "  ${RED}FAIL${NC}  $1"; }
assert_eq() { if [ "$1" = "$2" ]; then ok "$3"; else bad "$3 (want '$1', got '$2')"; fi; }
# assert_verdict <want-first-word> <verdict line> <label>
assert_verdict() { assert_eq "$1" "${2%% *}" "$3 → $2"; }
has()   { if printf '%s\n' "$1" | grep -q -- "$2"; then ok "$3"; else bad "$3 (no '$2' in: $(printf '%s' "$1" | tr '\n' '|'))"; fi; }
hasnt() { if printf '%s\n' "$1" | grep -q -- "$2"; then bad "$3 (found '$2')"; else ok "$3"; fi; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT INT TERM

P32=$((800 * 480 * 4)); P16=$((800 * 480 * 2))

# mkfix <file> <total-bytes> <k> <bpp> [x]  — k distinct NON-ZERO pixels, then zero
# padding: k+1 distinct values in total. With "x" the 32bpp pixels vary ONLY in the
# undisplayed X byte, which must count as one value.
mkfix() {
    local f="$1" total="$2" k="$3" bpp="$4" x="${5:-}" i h
    : > "$f"
    for ((i = 1; i <= k; i++)); do
        h=$(printf '%02x' "$i")
        if [ "$bpp" = 16 ]; then printf '%b' "\\x$h\\x00" >> "$f"
        elif [ -n "$x" ];   then printf '%b' "\\x00\\x00\\x00\\x$h" >> "$f"
        else                    printf '%b' "\\x00\\x$h\\x10\\x00" >> "$f"; fi
    done
    head -c $((total - k * bpp / 8)) /dev/zero >> "$f"
}

# info <xres> <yres> <vx> <vy> <bpp> [stride] [pan] [fbovl] [ovl-lines…]
info() {
    local x="$1" y="$2" vx="$3" vy="$4" b="$5" st="${6-$(( $1 * $5 / 8 ))}" pan="${7-0,0}" fo="${8-0}"
    shift 8 2>/dev/null || shift $#
    printf 'mode "%sx%s-60"\n\tgeometry %s %s %s %s %s\n\ttimings 1 2 3\nendmode\n' "$x" "$y" "$x" "$y" "$vx" "$vy" "$b"
    [ -n "$st" ] && echo "STRIDE $st"
    [ -n "$pan" ] && echo "PAN $pan"
    [ -n "$fo" ] && echo "FBOVL $fo"
    if [ $# -gt 0 ]; then printf '%s\n' "$@"; else printf 'OVL 0 1\nOVL 1 0\nOVL 2 0\n'; fi
}
I32=$(info 800 480 800 480 32)
I16=$(info 800 480 800 480 16)
ALIVE=$'SMOKE_PID=123\nSMOKE_ALIVE'

mkfix "$TMP/c32.raw"   "$P32" 40 32         # colourful 32bpp page: 41 distinct
mkfix "$TMP/blk32.raw" "$P32" 0 32          # black
mkfix "$TMP/x32.raw"   "$P32" 40 32 x       # varies only in the X byte
mkfix "$TMP/c16.raw"   "$P16" 40 16         # colourful 16bpp page
# 16bpp trap: first 16bpp page black, the colour only in the second half — a full
# 32bpp-sized dump. Graded at 16bpp it is black; at 32bpp the same bytes pass.
{ head -c "$P16" /dev/zero; mkfix "$TMP/c16b.raw" "$P16" 40 16; cat "$TMP/c16b.raw"; } > "$TMP/trap.raw"
head -c "$P16" "$TMP/c32.raw" > "$TMP/short32.raw"
cat "$TMP/blk32.raw" "$TMP/c32.raw" > "$TMP/two32.raw"   # page 0 black, page 1 colourful

# ═══════════════════════════════════════════════════════════════════════════
echo ""; echo "A. argv and scope"
assert_eq "fb touch" "$(smoke_argmode '')"         "A1 empty args= is fb,touch (parse_args default)"
assert_eq ""         "$(smoke_argmode none)"       "A2 args=none is no arguments"
assert_eq "fb"       "$(smoke_argmode fb)"         "A3 args=fb"
assert_eq "touch"    "$(smoke_argmode touch)"      "A4 args=touch"
assert_eq "fb touch" "$(smoke_argmode fb,touch)"   "A5 args=fb,touch"
assert_eq "fb touch" "$(smoke_argmode bogus)"      "A6 an unrecognised value falls back to fb,touch"
T=$(smoke_targets)
has "$T" '^app_launcher|/opt/roomwizard/app_launcher|$'  "A7 app_launcher runs bare, as the init script starts it"
has "$T" '^control_panel|/opt/games/control_panel|/dev/fb0 /dev/input/touchscreen0$' "A8 control_panel's empty args= becomes fb,touch"
has "$T" '^snake|/opt/games/snake|/dev/fb0 /dev/input/touchscreen0$' "A9 a game gets <fb> <touch>"
hasnt "$T" 'fb_plane_bench\|dss_scale_ab' "A10 the two excluded tools are not targets"
assert_eq 13 "$(printf '%s\n' "$T" | grep -c .)" "A11 13 targets: launcher + 9 manifests + 3 tools"

# ═══════════════════════════════════════════════════════════════════════════
echo ""; echo "B. the five outcomes"
assert_verdict did-not-start  "$(smoke_classify SMOKE_NOEXEC '' /dev/null)"                 "B1 not executable"
assert_verdict did-not-start  "$(smoke_classify $'SMOKE_PID=9\nSMOKE_EXIT=127' '' /dev/null)" "B2 exec failed 127"
assert_verdict did-not-start  "$(smoke_classify $'SMOKE_PID=9\nSMOKE_EXIT=126' '' /dev/null)" "B3 exec failed 126"
assert_verdict started-died   "$(smoke_classify $'SMOKE_PID=9\nSMOKE_EXIT=1' '' /dev/null)"   "B4 exited 1"
V=$(smoke_classify $'SMOKE_PID=9\nSMOKE_EXIT=132' '' /dev/null)
assert_verdict started-died "$V" "B5 SIGILL"
has "$V" SIGILL "B6 exit 132 is named as SIGILL"
assert_verdict started-died   "$(smoke_classify $'SMOKE_PID=9\nSMOKE_EXIT=0' '' /dev/null)"   "B7 a clean exit before the settle time is still a death"
assert_verdict could-not-tell "$(smoke_classify '' "$I32" "$TMP/c32.raw")"                   "B8 silence from the launch is not success"
assert_verdict could-not-tell "$(smoke_classify 'garbage' "$I32" "$TMP/c32.raw")"            "B9 an unrecognised launch reply"
V=$(smoke_classify "$ALIVE" "$I32" "$TMP/c32.raw")
assert_verdict pass "$V" "B10 alive + varied 32bpp page"
has "$V" '41 distinct' "B11 the count is reported (40 colours + black)"
assert_verdict black-screen   "$(smoke_classify "$ALIVE" "$I32" "$TMP/blk32.raw")"           "B12 alive + all black"
assert_verdict black-screen   "$(smoke_classify "$ALIVE" "$I32" "$TMP/x32.raw")"             "B13 variation only in the undisplayed X byte"

# ═══════════════════════════════════════════════════════════════════════════
echo ""; echo "C. depth and geometry from fbset"
assert_eq "800 480 800 480 32" "$(smoke_parse_fbset "$I32")" "C1 the geometry line parses"
assert_verdict black-screen   "$(smoke_classify "$ALIVE" "$I16" "$TMP/trap.raw")" "C2 16bpp: one page only — the second half of a 32bpp-sized dump is not graded"
assert_verdict pass           "$(smoke_classify "$ALIVE" "$I32" "$TMP/trap.raw")" "C3 the SAME bytes at 32bpp pass — the depth decided it"
assert_verdict pass           "$(smoke_classify "$ALIVE" "$I16" "$TMP/c16.raw")"  "C4 16bpp varied page"
has "$(smoke_classify "$ALIVE" "$I16" "$TMP/c16.raw")" '16bpp' "C5 the verdict names the depth it used"
assert_verdict could-not-tell "$(smoke_classify "$ALIVE" $'fbset: error\nSTRIDE 3200' "$TMP/c32.raw")" "C6 unparsable fbset"
assert_verdict could-not-tell "$(smoke_classify "$ALIVE" '' "$TMP/c32.raw")"      "C7 empty fbset (info ssh failed)"
assert_verdict could-not-tell "$(smoke_classify "$ALIVE" "$(info 800 480 800 480 24)" "$TMP/c32.raw")" "C8 a depth it does not model"
assert_verdict could-not-tell "$(smoke_classify "$ALIVE" "$I32" "$TMP/short32.raw")" "C9 a 32bpp geometry with a 16bpp-page-sized capture"
assert_verdict could-not-tell "$(smoke_classify "$ALIVE" "$I32" "$TMP/absent.raw")"  "C10 no capture at all"
assert_verdict could-not-tell "$(smoke_classify "$ALIVE" "$(info 800 480 800 480 32 3328)" "$TMP/c32.raw")" "C11 a padded stride"
assert_verdict pass           "$(smoke_classify "$ALIVE" "$(info 800 480 800 960 32 3200 0,480)" "$TMP/two32.raw")" "C12 pan y=480 grades page 1"
assert_verdict black-screen   "$(smoke_classify "$ALIVE" "$(info 800 480 800 960 32 3200 0,0)" "$TMP/two32.raw")"   "C13 pan y=0 grades page 0"
assert_verdict could-not-tell "$(smoke_classify "$ALIVE" "$(info 800 480 800 960 32 3200 '')" "$TMP/two32.raw")"    "C14 virtual height > visible with no pan"

# ═══════════════════════════════════════════════════════════════════════════
echo ""; echo "D. overlays"
V=$(smoke_classify "$ALIVE" "$(info 800 480 800 480 32 3200 0,0 0 'OVL 0 1' 'OVL 1 1' 'OVL 2 0')" "$TMP/blk32.raw")
assert_verdict could-not-tell "$V" "D1 overlay1 enabled: a black gfx buffer is NOT black-screen"
has "$V" overlay1 "D2 the verdict names the overlay"
assert_verdict could-not-tell "$(smoke_classify "$ALIVE" "$(info 800 480 800 480 32 3200 0,0 0 'OVL 0 1' 'OVL 1 1')" "$TMP/c32.raw")" "D3 nor is a varied one a pass"
assert_verdict could-not-tell "$(smoke_classify "$ALIVE" "$(info 800 480 800 480 32 3200 0,0 '' 'OVL 0 1')" "$TMP/c32.raw")" "D4 fbN/overlays unreadable"
assert_verdict could-not-tell "$(smoke_classify "$ALIVE" "$(info 800 480 800 480 32 3200 0,0 0 'nothing')" "$TMP/c32.raw")" "D5 no overlayN/enabled lines"
assert_verdict pass "$(smoke_classify "$ALIVE" "$(info 800 480 800 480 32 3200 0,0 0,1 'OVL 0 1' 'OVL 1 1')" "$TMP/c32.raw")" "D6 an enabled overlay the captured fb itself feeds is fine"

# ═══════════════════════════════════════════════════════════════════════════
echo ""; echo "E. the threshold is a floor on distinct values"
mkfix "$TMP/lo.raw" "$P32" $((SMOKE_MIN_DISTINCT - 2)) 32
mkfix "$TMP/hi.raw" "$P32" $((SMOKE_MIN_DISTINCT - 1)) 32
assert_verdict black-screen "$(smoke_classify "$ALIVE" "$I32" "$TMP/lo.raw")" "E1 SMOKE_MIN_DISTINCT-1 values ($((SMOKE_MIN_DISTINCT - 1)))"
assert_verdict pass         "$(smoke_classify "$ALIVE" "$I32" "$TMP/hi.raw")" "E2 exactly SMOKE_MIN_DISTINCT values ($SMOKE_MIN_DISTINCT)"
if [ "$SMOKE_MIN_DISTINCT" -ge 3 ]; then ok "E3 the floor is above a one-rectangle-on-a-fill screen (>= 3)"
else bad "E3 SMOKE_MIN_DISTINCT=$SMOKE_MIN_DISTINCT is vacuous"; fi

# ═══════════════════════════════════════════════════════════════════════════
echo ""; echo "F. the runner end to end, through an ssh stub"
SC="$TMP/sc"; mkdir -p "$TMP/bin"
# ⚠️ The stub MUST slurp its stdin unless given -n, as a real ssh does with a non-tty
# stdin. The runner calls ssh inside a `while read` loop; a stub that never reads would
# let a missing -n through and F31 (all 13 graded) would pass vacuously.
cat > "$TMP/bin/ssh" <<'STUB'
#!/bin/bash
n=0
while :; do case "${1:-}" in -o) shift 2 ;; -n) n=1; shift ;; *) break ;; esac; done
shift                                   # the target
cmd="$*"
[ "$n" = 1 ] || cat > /dev/null
printf '%s\n' "$cmd" | tr '\n' ' ' >> "$SC/calls"; echo >> "$SC/calls"
case "$cmd" in
    true)  [ -f "$SC/down" ] && { echo "ssh: connect to host x port 22: Connection refused" >&2; exit 255; }
           exit 0 ;;
    *"roomwizard-app stop"*)  exit 0 ;;
    *"roomwizard-app start"*) exit 0 ;;
    *SMOKE_NOEXEC*)
        if [ -f "$SC/term" ]; then
            for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do [ -s "$SC/mainpid" ] && break; sleep 0.1; done
            kill -TERM "$(cat "$SC/mainpid")"
        fi
        cat "$SC/launch.out"; exit 0 ;;
    *"fbset -fb"*) cat "$SC/info.out"; exit 0 ;;
    "cat /dev/fb"*) [ -f "$SC/fb.raw" ] || exit 255; cat "$SC/fb.raw"; exit 0 ;;
    kill*) exit 0 ;;
esac
exit 0
STUB
chmod +x "$TMP/bin/ssh"
export SC SMOKE_NO_PNG=1
# scenario <launch-out> <info-out> <fb-fixture|-> [touch-files…]
scenario() {
    rm -rf "$SC"; mkdir -p "$SC" "$SC/out"
    printf '%s\n' "$1" > "$SC/launch.out"; printf '%s\n' "$2" > "$SC/info.out"
    [ "$3" != - ] && cp "$3" "$SC/fb.raw"
    shift 3; for f in "$@"; do : > "$SC/$f"; done
}
# run_runner <args…>  → sets OUT, RC
run_runner() {
    PATH="$TMP/bin:$PATH" bash "$RUNNER" "$@" --out "$SC/out" > "$SC/stdout" 2>&1 &
    local rp=$!; echo "$rp" > "$SC/mainpid"; RC=0; wait "$rp" || RC=$?
    OUT=$(cat "$SC/stdout"); CALLS=$(cat "$SC/calls" 2>/dev/null)
}

scenario "$ALIVE" "$I32" "$TMP/c32.raw"
run_runner 1.2.3.4 snake
assert_eq 0 "$RC" "F1 one passing binary exits 0"
has "$OUT" 'snake *pass 41 distinct' "F2 the verdict line names binary, outcome, count"
has "$OUT" '1 of 1 passed' "F3 the summary"
assert_eq 1 "$(printf '%s\n' "$CALLS" | grep -n 'roomwizard-app stop' | cut -d: -f1 | head -n1 | grep -c '^2$')" "F4 stop is the first call after the gate probe"
has "$(printf '%s\n' "$CALLS" | tail -n1)" 'roomwizard-app start' "F5 start is the LAST call"
has "$CALLS" 'b=/opt/games/snake; .* /dev/fb0 /dev/input/touchscreen0 </dev/null' "F6 the production argv reaches the launch"
has "$CALLS" '^kill 123' "F7 the launched PID — and only it — is killed"
hasnt "$CALLS" 'killall' "F8 no killall, ever"
has "$CALLS" 'fbset -fb /dev/fb0' "F9 geometry from fbset -fb on the capture device"

scenario $'SMOKE_PID=9\nSMOKE_EXIT=1' "$I32" -
run_runner 1.2.3.4 tetris
assert_eq 1 "$RC" "F10 a death exits 1"
has "$OUT" 'tetris *started-died' "F11 started-died reported"
has "$(printf '%s\n' "$CALLS" | tail -n1)" 'roomwizard-app start' "F12 start runs after a failure too"
hasnt "$CALLS" 'fbset' "F13 a dead app is not captured"

scenario "$ALIVE" "$I32" -
run_runner 1.2.3.4 pong
assert_eq 1 "$RC" "F14 a failed capture exits 1"
has "$OUT" 'pong *could-not-tell' "F15 capture ssh failure is could-not-tell, not black-screen"
has "$(printf '%s\n' "$CALLS" | tail -n1)" 'roomwizard-app start' "F16 start runs after a harness failure"

scenario "$ALIVE" "$I32" "$TMP/blk32.raw"
run_runner 1.2.3.4 samegame
assert_eq 1 "$RC" "F17 black-screen exits 1"
has "$OUT" 'samegame *black-screen' "F18 black-screen reported"

scenario "$ALIVE" "$I32" "$TMP/c32.raw" term
run_runner 1.2.3.4 frogger
assert_eq 143 "$RC" "F19 TERM mid-run exits 143"
has "$(printf '%s\n' "$CALLS" | tail -n1)" 'roomwizard-app start' "F20 start runs on TERM (the trap)"

scenario "$ALIVE" "$I32" "$TMP/c32.raw" down
run_runner 1.2.3.4 snake
assert_eq 2 "$RC" "F21 an unreachable device exits 2"
hasnt "$CALLS" 'roomwizard-app' "F22 nothing is stopped (or started) on a device never reached"

scenario "$ALIVE" "$I32" "$TMP/c32.raw"
run_runner 1.2.3.4 dss_scale_ab
assert_eq 2 "$RC" "F23 an excluded binary is refused"
assert_eq "" "$CALLS" "F24 ...before any ssh call"
run_runner 1.2.3.4 no_such_app
assert_eq 2 "$RC" "F25 an unknown binary is refused"

scenario "$ALIVE" "$(info 800 480 800 480 32 3200 0,0 1 'OVL 0 0' 'OVL 1 1')" "$TMP/c32.raw"
run_runner 1.2.3.4 snake --capture-dev /dev/fb1
has "$CALLS" 'fbset -fb /dev/fb1' "F26 --capture-dev moves fbset"
has "$CALLS" '^cat /dev/fb1' "F27 ...and the capture"
has "$CALLS" '/sys/class/graphics/fb1/overlays' "F28 ...and the sysfs it reads"
has "$OUT" 'snake *pass' "F29 fb1 feeding overlay1 is graded when overlay1 is the enabled one"
assert_eq 0 "$RC" "F30 ...and exits 0"

scenario "$ALIVE" "$I32" "$TMP/c32.raw"
run_runner 1.2.3.4
assert_eq 13 "$(printf '%s\n' "$OUT" | grep -cE '^  [a-z_]+ +pass ')" "F31 no subset: every one of the 13 targets is graded"
run_runner
assert_eq 2 "$RC" "F32 no <ip> is a usage error"

# ═══════════════════════════════════════════════════════════════════════════
echo ""
echo "════════════════════════════════════════"
TOTAL=$((PASS + FAIL))
echo "  $PASS passed, $FAIL failed, $TOTAL total"
if [ "$TOTAL" -lt 75 ]; then
    echo -e "  ${RED}✗ only $TOTAL cases ran — the harness itself is broken${NC}"
    exit 1
fi
if [ "$FAIL" -gt 0 ]; then
    echo -e "  ${RED}✗ $FAIL failure(s)${NC}"
    exit 1
fi
echo -e "  ${GREEN}✓ all $TOTAL cases passed${NC}"
echo ""
exit 0
