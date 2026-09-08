#!/bin/bash
# Sabotage sweep for ppm_test.c: prove that EVERY assertion group can go red.
#
# One targeted defect per case, applied to a scratch COPY of ppm.c, rebuilt and
# re-run.  Nothing writes inside the repo and nothing here runs git: restoring a
# sabotage with git would destroy the uncommitted fix being measured, so every
# case restores by copying the baseline file back.
#
# The baseline is the FIXED ppm.c — the copy carrying ppm_scale()'s
# source-dimension guard — because that is what the repo will hold once the
# patch is applied.  Sweeping against the pristine file instead would leave
# group H red in all sixteen cases and the sweep would measure nothing.
#
# A sed program that no longer matches prints "NO-OP EDIT" rather than
# "0 failed": a rotted pattern is otherwise indistinguishable from a suite with
# a hole in it.  Read the output; do not just count it.
#
# The group array is NOT called GROUPS: that name is a read-only special array
# in bash holding the user's group ids, so the assignment is ignored in silence
# and the "red:" column then reports numeric gids instead of group letters.
# The verdict column was unaffected, which is what made it survive one run.

set -u

ROOT="/mnt/c/work/rw-scratch/c6"
BASE="$ROOT/fixed"                     # the FIXED ppm.c, this sweep's pristine copy
W="$ROOT/build/sabotage"
CASE_GROUPS=(A B C D E F G H I)

rm -rf "$W"
mkdir -p "$W/c" "$W/run/build" || exit 1
cd "$W/run" || exit 1                  # ppm_test.c writes its fixtures into ./build

caught=0
missed=0

# run <label> <group that must go red> <sed command…>
run() {
    local name="$1" want="$2"; shift 2
    local out rc n red g verdict note

    rm -f "$W/c/ppm.c" "$W/c/ppm.h"
    cp "$BASE/ppm.c" "$BASE/ppm.h" "$W/c/" || { echo "  $name: CANNOT STAGE"; return; }

    if ! ( cd "$W/c" && "$@" ); then
        echo "  SABOTAGE DID NOT APPLY   want=$want  $name"
        missed=$((missed + 1)); return
    fi
    if diff -q "$W/c/ppm.c" "$BASE/ppm.c" >/dev/null 2>&1; then
        echo "  NO-OP EDIT               want=$want  $name  (pattern rotted)"
        missed=$((missed + 1)); return
    fi
    if ! gcc -Wall -Wextra -Wno-unused-parameter -I "$W/c" -o "$W/t" \
            "$ROOT/ppm_test.c" "$W/c/ppm.c" -lm 2>"$W/cc.log"; then
        echo "  DID NOT COMPILE          want=$want  $name  ($(head -1 "$W/cc.log"))"
        missed=$((missed + 1)); return
    fi

    # stdbuf, because a sabotage that CRASHES loses whatever libc had buffered
    # into the pipe, and a caught sabotage then reads as an undetected one.
    out=$(stdbuf -oL timeout 60 "$W/t" 2>&1); rc=$?

    red=""
    for g in "${CASE_GROUPS[@]}"; do
        n=$(printf '%s\n' "$out" | grep -c "FAIL $g:")
        if [ "$n" -gt 0 ]; then red="$red $g($n)"; fi
    done

    note=""
    if [ "$rc" -ge 124 ]; then note="  ⚠ TIMED OUT/SIGNALLED rc=$rc"; fi

    if printf '%s\n' "$out" | grep -q "FAIL $want:"; then
        verdict="CAUGHT"; caught=$((caught + 1))
    else
        verdict="NOT CAUGHT"; missed=$((missed + 1))
    fi
    printf '  %-24s want=%s  %-44s red:%s%s\n' \
           "$verdict" "$want" "$name" "${red:- NONE}" "$note"
}

echo "measure_ppm_sabotage.sh — one targeted defect per assertion group"
echo "baseline: $BASE/ppm.c (the FIXED ppm.c)"
echo

run "1  pixel channels swapped R<->B"            A \
    sed -i 's|((uint32_t)rgb\[0\] << 16)|((uint32_t)rgb[2] << 16)|' ppm.c
run "2  #-comments are not recognised"           B \
    sed -i "s|if (c == '#')|if (c == '@')|" ppm.c
run "3  only a space counts as whitespace"       B \
    sed -i 's|} else if (!isspace(c)) {|} else if (!(c == 0x20)) {|' ppm.c
run "4  maxval is no longer checked"             C \
    sed -i 's| \|\| maxval != 255||' ppm.c
run "5  the 4096 dimension cap is raised"        C \
    sed -i 's|w > 4096 \|\| h > 4096|w > 8192 \|\| h > 8192|' ppm.c
run "6  a zero dimension is accepted"            C \
    sed -i 's|if (w <= 0 \|\| h <= 0|if (w < 0 \|\| h < 0|' ppm.c
run "7  a short pixel read is accepted"          D \
    sed -i 's|if (fread(rgb, 1, 3, f) != 3) {|if (fread(rgb, 1, 3, f) > 3) {|' ppm.c
run "8  any Pn magic is accepted"                D \
    sed -i "s| \|\| magic\[1\] != '6'||" ppm.c
run "9  THE CRLF DEFECT FIXED (the pin must fire)" E \
    sed -i 's|^    fgetc(f);$|    { int sep = fgetc(f); if (sep == 0x0D) { int c2 = fgetc(f); if (c2 != 0x0A) ungetc(c2, f); } }|' ppm.c
run "10 the sample is transposed"                F \
    sed -i 's|src\[sy \* src_w + sx\]|src[sx * src_h + sy]|' ppm.c
run "11 x rounds instead of flooring"            G \
    sed -i 's|int sx = x \* src_w / dst_w;|int sx = (x * src_w + dst_w / 2) / dst_w;|' ppm.c
run "12 y is divided by the wrong extent"        G \
    sed -i 's|int sy = y \* src_h / dst_h;|int sy = y * src_h / dst_w;|' ppm.c
run "13 THE OOB DEFECT RESTORED (no src guard)"  H \
    sed -i 's|src_w <= 0 \|\| src_h <= 0 \|\| ||' ppm.c
run "14 only src_w is guarded, not src_h"        H \
    sed -i 's| \|\| src_h <= 0||' ppm.c
run "15 only src_h is guarded, not src_w"        H \
    sed -i 's|src_w <= 0 \|\| ||' ppm.c
run "16 a zero destination is accepted"          I \
    sed -i 's|dst_w <= 0 \|\| dst_h <= 0|dst_w < 0 \|\| dst_h < 0|' ppm.c

echo
printf '  %d caught, %d not caught\n' "$caught" "$missed"
rm -rf "$W"
if [ "$missed" -gt 0 ]; then exit 1; fi
exit 0
