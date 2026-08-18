#!/usr/bin/env bash
# doc_check.sh — documentation invariants that a host can check.
#
# Host-only: no device, no card, no root, no cross-compiler. Run it from WSL:
#
#   wsl.exe -e bash -lc "cd /mnt/c/work/roomwizard && ./tests/doc_check.sh > out.txt 2>&1"
#
# Groups, each with its own negative control (`--self-test` runs them all):
#
#   A  Every markdown anchor in the repo resolves: a cross-file `<doc>.md#<frag>` and a
#      same-file `(#<frag>)` alike must name a slugified heading that exists. Moving or
#      retitling a section is otherwise silent — ~120 of these point into
#      SYSTEM_ANALYSIS.md from .sh, .c, .py and .conf files, which no markdown linter reads.
#
#   B  No file outside IMPROVEMENT_PLAN.md cites a plan ID that resolves to nothing there.
#      Two citation shapes are scanned: qualified (`IMPROVEMENT_PLAN.md B19`) and the bare
#      parenthetical tag (`(B19)`, `see B19`) that a qualified one decays into.
#
#   C  Extraction receipts. One row per fact moved OUT of IMPROVEMENT_PLAN.md by the
#      documentation cleanup: a distinctive token, the file that must now hold it, and
#      optionally the file it must have LEFT. Run it before a deletion to confirm the
#      destination has the fact, and after to confirm it survived.
#
#   D  Per-document size ceilings. Every addition that grew these documents to 891, 2287
#      and 2214 lines was individually justified, so judging each addition on its merits
#      does not bound the total — only a ceiling does, because it is arithmetic rather
#      than judgement and therefore survives a session that has forgotten the rule.
#
# ── Why every scan here skips this file ───────────────────────────────────────
# A gate that documents the pattern it searches for matches its own documentation.
# Measured four times on this script and its sibling doc, each time inflating the number
# a run reported. So: all three scans skip `doc_check.sh`, coverage of it comes from the
# `--self-test` fixture, and anywhere a *doc* has to describe one of these shapes it
# writes `<placeholder>` brackets, which no scan here can match.
#
# ── Why group B is "resolves to nothing" and not "cites an ID at all" ──────────
# An ID beside a comment is allowed as a *bonus*; what is forbidden is a comment that
# DEPENDS on one, because closed items are deleted outright from IMPROVEMENT_PLAN.md and
# `git log --grep` does not rescue the ID either (B3k and B13c return zero commits). So the
# rule this gate enforces is the checkable half: every ID still written down must resolve.
# The unenforceable half — the comment stands on its own without the ID — is a review rule.
#
# This makes the gate self-maintaining in the direction that matters: deleting a closed item
# from IMPROVEMENT_PLAN.md fails this check until every citation of it has been rewritten.
#
# ⚠️ The bare form is why the qualified scan alone was not enough. Measured 2026-08-15: with
# every qualified dangling citation fixed and the gate reporting a clean zero, 41 bare ones
# were still in the tree — `(B13g)`, `(B3k)`, `see B28`. A gate that passes on a repo which
# still violates its invariant is worse than no gate.
#
# ⚠️ Both scans SKIP this file. Its header discusses IDs by writing them out, and a scan over
# its own source counted those as findings — three times, in three different shapes, each one
# inflating the number the run reported. Coverage of this file comes from the self-test
# fixture instead, which is a throwaway tree that cannot be mistaken for a real result.
#
# ⚠️ Known blind spots, both of which under-count rather than over-count:
#   - the scanner is line-based, so a qualified citation list broken across two comment
#     lines (`… C11,` / `# C12)`) has only its first ID seen;
#   - the bare scan matches only the parenthetical/`see`-prefixed shapes, because a bare
#     `B2` in running prose is indistinguishable from a register name — which is exactly
#     what `vnc_client/deps/`'s vendored libjpeg-turbo is full of (pruned below).
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLAN_FILE="IMPROVEMENT_PLAN.md"

fail_total=0

# ── the scanner ───────────────────────────────────────────────────────────────
# Files worth scanning, relative to $1. Prunes the two vendor trees that make a
# recursive walk from the repo root exceed a two-minute budget (scummvm/,
# usb_host/linux-4.14.52/) and the card captures, which are not our source.
scan_files() {
    local root="$1"
    ( cd "$root" && find . \
        \( -name .git \
        -o -name scummvm \
        -o -name linux-4.14.52 \
        -o -name partitions \
        -o -name partitions.new \
        -o -name HardwarePhotos \
        -o -name arm-deps \
        -o -name deps \
        \) -prune -o -type f \
        \( -name '*.c' -o -name '*.h' -o -name '*.cpp' -o -name '*.sh' \
        -o -name '*.py' -o -name '*.md' -o -name '*.conf' \) \
        -print | sed 's|^\./||' | sort )
}

# Plan IDs that have a heading in IMPROVEMENT_PLAN.md, one per line.
defined_ids() {
    grep -oE '^#{2,4} +[A-F][0-9]+[a-z]?\.' "$1/$PLAN_FILE" \
        | grep -oE '[A-F][0-9]+[a-z]?' | sort -u
}

# Every `IMPROVEMENT_PLAN.md <ID>` citation in the given files, as file:line:ID.
# Matches the two forms in the tree — a bare path in a code comment and a
# backticked path in markdown — plus comma- and slash-separated continuations
# (`F9, F10`, `B20/B25`). A `#fragment` after the path is an anchor, group A's job.
# The `.md` is optional because one site in the tree writes `IMPROVEMENT_PLAN B13g`.
cite_sites() {
    local root="$1"; shift
    ( cd "$root" && awk '
    {
        s = $0
        while (match(s, /IMPROVEMENT_PLAN(\.md)?`?[ \t]+`?[A-F][0-9]+[a-z]?/)) {
            st = RSTART; ln = RLENGTH
            hit = substr(s, st, ln)
            s   = substr(s, st + ln)
            if (match(hit, /[A-F][0-9]+[a-z]?$/))
                print FILENAME ":" FNR ":" substr(hit, RSTART, RLENGTH)
            while (match(s, /^(,[ ]*(and[ ]+)?|\/)`?[A-F][0-9]+[a-z]?/)) {
                st2 = RSTART; ln2 = RLENGTH
                seg = substr(s, st2, ln2)
                s   = substr(s, st2 + ln2)
                if (match(seg, /[A-F][0-9]+[a-z]?$/))
                    print FILENAME ":" FNR ":" substr(seg, RSTART, RLENGTH)
            }
        }
    }' "$@" )
}

# Bare plan-ID tags: the shape a qualified citation decays into once the filename is
# dropped. Only `(B19)`-style parentheticals and `see B19` / `is B19` prose are matched —
# a bare ID with no such marker cannot be told apart from a register or bitfield name.
#
# ⚠️ This scan skips THIS file. The header above discusses bare IDs by writing them out,
# and the self-test fixture is prose too, so a bare scan over its own source reports nine
# phantom findings — the second time this gate inflated its own number. The qualified scan
# still covers this file, via the interpolated fixture.
bare_sites() {
    local root="$1"; shift
    ( cd "$root" && awk '
    FILENAME ~ /doc_check\.sh$/ { next }
    {
        s = $0
        while (match(s, /(\(|see |is |was )[A-F][0-9]+[a-z]?([),.;: ]|$)/)) {
            st = RSTART; ln = RLENGTH
            hit = substr(s, st, ln)
            s   = substr(s, st + ln)
            if (match(hit, /[A-F][0-9]+[a-z]?/))
                print FILENAME ":" FNR ":" substr(hit, RSTART, RLENGTH)
        }
    }' "$@" )
}

# ── group A: every markdown anchor resolves ───────────────────────────────────
# GitHub's slug, measured against anchors already in this tree rather than assumed:
# heading text, lowercased, every character outside [a-z0-9 _-] deleted, then spaces
# turned into '-'. `### 5.2 As we run it — game mode` gives `52-as-we-run-it--game-mode`
# — the em-dash is deleted and leaves BOTH its spaces behind, so the double hyphen is
# correct. Backticks go and underscores stay: `### 4.5 Control block and \`boot_tracker\``
# gives `45-control-block-and-boot_tracker`.
#
# ⚠️ An ATX heading inside a fenced code block is not a heading. SYSTEM_ANALYSIS.md's
# recovery recipes are full of `# comment` lines inside ```bash fences — treating those as
# headings would let a dangling anchor resolve, which is a false PASS.
slugs_of() {
    awk '
    /^[ \t]*```/ { fence = !fence; next }
    fence        { next }
    /^#{1,6}[ \t]+/ {
        sub(/^#+[ \t]+/, ""); sub(/[ \t]+$/, "")
        s = tolower($0)
        gsub(/[^a-z0-9 _-]/, "", s)
        gsub(/ /, "-", s)
        print s
    }' "$1"
}

# Every anchor as file<TAB>line<TAB>link. Two shapes, both in the tree:
#   `path/to/doc.md#frag`  — bare in a code comment, or inside a markdown ]( ) target
#   `(#frag)`              — same-file, markdown only
# A fragment cannot contain `.`, because the slug rule deletes it — so excluding the dot is
# what stops the sentence-ending period of `(… doc.md#42-partitions).` being read as part of
# the fragment, which it was on the first run.
anchor_sites() {
    local root="$1"; shift
    ( cd "$root" && awk '
    FILENAME ~ /doc_check\.sh$/ { next }
    {
        s = $0
        while (match(s, /[A-Za-z0-9_.\/-]*\.md#[A-Za-z0-9_-]+/)) {
            print FILENAME "\t" FNR "\t" substr(s, RSTART, RLENGTH)
            s = substr(s, RSTART + RLENGTH)
        }
        s = $0
        while (match(s, /\(#[A-Za-z0-9_-]+\)/)) {
            print FILENAME "\t" FNR "\t" substr(s, RSTART + 1, RLENGTH - 2)
            s = substr(s, RSTART + RLENGTH)
        }
    }' "$@" )
}

# Resolve a link path against the citing file's directory, collapsing . and ..
# ($1 = dirname of the citing file, "." at the repo root; $2 = the link's path part.)
norm_path() {
    local p="$2"
    [ "$1" = "." ] || p="$1/$2"
    printf '%s\n' "$p" | awk -F/ '{
        n = 0
        for (i = 1; i <= NF; i++) {
            if ($i == "." || $i == "") continue
            else if ($i == "..") { if (n > 0) n-- }
            else a[++n] = $i
        }
        out = ""
        for (i = 1; i <= n; i++) out = out (i > 1 ? "/" : "") a[i]
        print out
    }'
}

# ⚠️ The path resolves relative to the citing file and then, failing that, from the repo
# root — because most of these anchors are not links. A comment in `lib/rw-clean.sh` or a
# backticked pointer in `lib/CLAUDE.md` writes the doc's plain filename and means the one at
# the root; requiring `../` there would put path arithmetic into 25 human-readable comments
# to satisfy a checker. The invariant worth having is that the **fragment** names a real
# heading, and the root fallback keeps a filename typo caught (nothing of that name exists
# anywhere) while dropping the noise.
#
# ⚠️ One blind spot, taken deliberately: a fragment that is 3 or 6 hex digits is skipped as a
# CSS colour. `browser_games/README.md` documents its palette as `(#0a0e27)`, which is
# `(#<frag>)` to a scanner. A heading whose slug is six hex digits would go unchecked.
group_a() {
    local root="$1" quiet="${2:-}"
    local bad=0 total=0 f ln link tgt frag key cache
    local -a files=()
    mapfile -t files < <(scan_files "$root" | grep -v 'doc_check\.sh$')
    [ ${#files[@]} -eq 0 ] && { echo 0 > "$RESULT"; return 0; }

    cache="$(mktemp -d)"
    while IFS=$'\t' read -r f ln link; do
        frag="${link#*#}"
        [[ "$frag" =~ ^[0-9a-fA-F]{3}$|^[0-9a-fA-F]{6}$ ]] && continue
        total=$((total + 1))

        if [ "${link:0:1}" = "#" ]; then
            tgt="$f"
        else
            tgt="$(norm_path "$(dirname "$f")" "${link%%#*}")"
            [ -f "$root/$tgt" ] || tgt="$(norm_path "." "${link%%#*}")"
        fi

        key="$cache/$(printf '%s' "$tgt" | tr '/' '_')"
        if [ ! -e "$key" ]; then
            if [ -f "$root/$tgt" ]; then slugs_of "$root/$tgt" > "$key"
            else : > "$key"; : > "$key.missing"; fi
        fi

        if [ -e "$key.missing" ]; then
            bad=$((bad + 1))
            [ -n "$quiet" ] || printf '  NO SUCH FILE  %s:%s  -> %s\n' "$f" "$ln" "$tgt"
        elif ! grep -qxF -- "$frag" "$key"; then
            bad=$((bad + 1))
            [ -n "$quiet" ] || printf '  DANGLING      %s:%s  #%s is no heading in %s\n' "$f" "$ln" "$frag" "$tgt"
        fi
    done < <(anchor_sites "$root" "${files[@]}")

    rm -rf "$cache"
    [ -n "$quiet" ] || printf 'group A: %d anchors, %d resolve to nothing\n' "$total" "$bad"
    echo "$bad" > "$RESULT"
    [ "$bad" -eq 0 ]
}

# ── group B ───────────────────────────────────────────────────────────────────
group_b() {
    local root="$1" quiet="${2:-}"
    local ids_file hits_file dangling=0 total=0

    ids_file="$(mktemp)"; hits_file="$(mktemp)"
    defined_ids "$root" > "$ids_file"

    local -a files=()
    mapfile -t files < <(scan_files "$root" | grep -v "^$PLAN_FILE\$" | grep -v 'doc_check\.sh$')
    [ ${#files[@]} -eq 0 ] && { rm -f "$ids_file" "$hits_file"; return 0; }

    cite_sites "$root" "${files[@]}" > "$hits_file"
    bare_sites "$root" "${files[@]}" >> "$hits_file"
    total=$(wc -l < "$hits_file")

    while IFS= read -r hit; do
        local id="${hit##*:}"
        grep -qx "$id" "$ids_file" && continue
        dangling=$((dangling + 1))
        [ -n "$quiet" ] || printf '  %s\n' "$hit"
    done < "$hits_file"

    rm -f "$ids_file" "$hits_file"
    [ -n "$quiet" ] || printf 'group B: %d citations, %d resolve to nothing\n' "$total" "$dangling"
    echo "$dangling" > "$RESULT"
    [ "$dangling" -eq 0 ]
}

# ── group C: extraction receipts ──────────────────────────────────────────────
# Fields: TOKEN <TAB> file-that-must-contain-it <TAB> file-it-must-have-left (or "-")
#
# A row exists because the fact was unique to IMPROVEMENT_PLAN.md and that entry was cut.
# The `must have left` column is what makes this a *move* check rather than a copy check —
# "one fact, one home" is the rule, and a fact present in both files is the drift the whole
# cleanup exists to remove.
#
# ⚠️ What this group CANNOT see, all of which under-report:
#   - **A token can be present and the claim around it wrong.** This greps for a string; it
#     cannot read the sentence. Three of phase 2's 83 comments were stale about the tree
#     while their ID matched perfectly, which is the same failure one level down.
#   - **A fact extracted without a row here is invisible.** The table is hand-maintained, so
#     a clean run says "every receipt I was told about holds", never "nothing was lost".
#     That is why the ⚠️-warning survival census stays a per-phase manual control.
#   - **It cannot tell prose from a code fence or a see-also line.** A token parked in a
#     stray reference passes exactly like a token in the paragraph that explains it.
#   - **A token that is a substring of something else passes for free.** Prefer md5s, hex
#     addresses and `file.c:line` forms over words.
receipts() {
    # `RECEIPTS_FILE` exists only so --self-test can drive this group over a fixture table.
    if [ -n "${RECEIPTS_FILE:-}" ]; then cat "$RECEIPTS_FILE"; return; fi
    # shellcheck disable=SC2016
    cat <<'EOF'
0x3e4	usb_host/README.md	IMPROVEMENT_PLAN.md
9021923205825a2ec36edeaa1fe3ccc3	SYSTEM_ANALYSIS.md	IMPROVEMENT_PLAN.md
0x480AB060	usb_host/README.md	IMPROVEMENT_PLAN.md
SNDRV_PCM_VERSION	native_apps/CLAUDE.md	IMPROVEMENT_PLAN.md
15 of 59	SYSTEM_ANALYSIS.md	IMPROVEMENT_PLAN.md
pcm.c:978	native_apps/CLAUDE.md	IMPROVEMENT_PLAN.md
never a thread	native_apps/CLAUDE.md	IMPROVEMENT_PLAN.md
targets a LEAD	native_apps/CLAUDE.md	IMPROVEMENT_PLAN.md
no longer resets the ring	native_apps/CLAUDE.md	IMPROVEMENT_PLAN.md
cannot both own the ring	native_apps/CLAUDE.md	IMPROVEMENT_PLAN.md
RFC-1918	COMMISSIONING.md	IMPROVEMENT_PLAN.md
1486	SYSTEM_ANALYSIS.md	IMPROVEMENT_PLAN.md
22,317	SYSTEM_ANALYSIS.md	IMPROVEMENT_PLAN.md
button_check_tap	native_apps/CLAUDE.md	-
RESCAN_INTERVAL_MS	native_apps/CLAUDE.md	-
rw_provision_push_installs	lib/CLAUDE.md	-
rw_provision_check_keeps	device-files/CLAUDE.md	-
rw_clean_validate	device-files/CLAUDE.md	SYSTEM_ANALYSIS.md
TouchCalibSweep	native_apps/CLAUDE.md	SYSTEM_ANALYSIS.md
clamp_to_hw	native_apps/CLAUDE.md	SYSTEM_ANALYSIS.md
0..60000	native_apps/CLAUDE.md	SYSTEM_ANALYSIS.md
594, 614, 817	native_apps/CLAUDE.md	SYSTEM_ANALYSIS.md
FB_TOUCH_INSET_MAX	native_apps/CLAUDE.md	SYSTEM_ANALYSIS.md
publish_safe_area	native_apps/CLAUDE.md	SYSTEM_ANALYSIS.md
flat (saturated)	SYSTEM_ANALYSIS.md	native_apps/CLAUDE.md
1,536,000	SYSTEM_ANALYSIS.md	native_apps/CLAUDE.md
4-number	CLAUDE.md	native_apps/CLAUDE.md
1000000L	CLAUDE.md	native_apps/CLAUDE.md
injected successfully	CLAUDE.md	native_apps/CLAUDE.md
15/15/0/0	SYSTEM_ANALYSIS.md	native_apps/CLAUDE.md
red rect = visible	SYSTEM_ANALYSIS.md	native_apps/CLAUDE.md
+19 px	SYSTEM_ANALYSIS.md	native_apps/CLAUDE.md
0 1020 3074 4095	SYSTEM_ANALYSIS.md	native_apps/CLAUDE.md
bands every gradient	SYSTEM_ANALYSIS.md	native_apps/CLAUDE.md
XRGB8888	SYSTEM_ANALYSIS.md	native_apps/CLAUDE.md
SPKR1	SYSTEM_ANALYSIS.md	native_apps/CLAUDE.md
35280	native_apps/CLAUDE.md	IMPROVEMENT_PLAN.md
RW29 1G-093	HARDWARE.md	SYSTEM_ANALYSIS.md
W180322	HARDWARE.md	SYSTEM_ANALYSIS.md
D9RMJ	HARDWARE.md	SYSTEM_ANALYSIS.md
push-push	HARDWARE.md	SYSTEM_ANALYSIS.md
22.86	HARDWARE.md	SYSTEM_ANALYSIS.md
SLEEP_RQ	HARDWARE.md	SYSTEM_ANALYSIS.md
T1OUT	HARDWARE.md	SYSTEM_ANALYSIS.md
TP39	HARDWARE.md	SYSTEM_ANALYSIS.md
560-0540-0x	HARDWARE.md	SYSTEM_ANALYSIS.md
Top-Overwiev	HARDWARE.md	SYSTEM_ANALYSIS.md
no light aperture	HARDWARE.md	SYSTEM_ANALYSIS.md
EOF
}

group_c() {
    local root="$1" quiet="${2:-}"
    local bad=0 total=0 tok dest gone

    while IFS=$'\t' read -r tok dest gone; do
        [ -z "$tok" ] && continue
        total=$((total + 1))
        if [ ! -f "$root/$dest" ]; then
            bad=$((bad + 1))
            [ -n "$quiet" ] || printf '  MISSING FILE  %s (receipt for %s)\n' "$dest" "$tok"
            continue
        fi
        if ! grep -qF -- "$tok" "$root/$dest"; then
            bad=$((bad + 1))
            [ -n "$quiet" ] || printf '  NOT EXTRACTED %s  is absent from %s\n' "$tok" "$dest"
        fi
        [ "$gone" = "-" ] && continue
        if [ -f "$root/$gone" ] && grep -qF -- "$tok" "$root/$gone"; then
            bad=$((bad + 1))
            [ -n "$quiet" ] || printf '  NOT MOVED     %s  still in %s as well\n' "$tok" "$gone"
        fi
    done < <(receipts)

    [ -n "$quiet" ] || printf 'group C: %d receipts, %d unsatisfied\n' "$total" "$bad"
    echo "$bad" > "$RESULT"
    [ "$bad" -eq 0 ]
}

# ── group D: per-document size ceilings ───────────────────────────────────────
# Fields: ceiling <TAB> path
#
# This group is not about tidiness. Every addition that grew these documents to 891,
# 2287 and 2214 lines was individually justified, and a week of cleanup was the price.
# Judging each addition on its merits does not bound the total; a ceiling does, because
# it is arithmetic rather than judgement and so it survives a session that has forgotten
# the rule. `.claude/skills/doc-update/SKILL.md` is the authoring half of the same rule.
#
# The ceilings are where the 2026-08-16 cleanup landed, plus room for ONE substantial
# addition (~20 lines, ~16 for the always-loaded root CLAUDE.md). So a genuinely new
# measured fact does not fail the gate and a second one without a deletion does. Two
# legitimate responses when it fires, and only two:
#   1. PAY for the addition — delete what the document no longer needs, or move a block
#      to the document whose job it is, which is usually where it should have been.
#   2. RAISE the ceiling, in a commit that argues for it. Deliberate growth is fine;
#      what this catches is growth nobody decided on.
#
# ⚠️ A ceiling naming a file that is not there is a FAILURE, not a skip. A renamed or
# split document would otherwise be unguarded forever while it grew — and phase 5 split
# HARDWARE.md out of SYSTEM_ANALYSIS.md, so that is not hypothetical here.
# ⚠️ The authoring skill is on this list too. A rulebook that grows without limit is the
# thing it exists to prevent, and it has no other check on it.
# ⚠️ tests/CLAUDE.md is 250 rather than 235 because the commit that INTRODUCED these
# ceilings spent that file's whole headroom documenting this group — so the headroom was
# never actually granted. Raised deliberately, in that same commit, which is the escape
# hatch working as designed rather than an exception to it. Every other row still holds
# its original grant; check the margin before assuming a row is generous.
# ⚠️ IMPROVEMENT_PLAN.md is 1515 rather than 1500 because F1 defect 3's recorded cause was
# REFUTED and replacing it cost more lines than the wrong version had: a 4-row evidence
# table plus the five suspects that measurement killed, which is what stops the next
# session re-raising them. Raised to cover exactly that, and the ~20 lines of headroom are
# deliberately NOT restored. ⚠️ **That identified payment has now been SPENT** (2026-08-17): F1's
# Phase-3 design-rule bullets were verified at native_apps/CLAUDE.md → *Mixing*, receipted in group C
# below, and deleted in favour of a pointer — which is where this file's present margin came from.
# **The next addition needs a NEW way to pay; there is no queued one.**
# ⚠️ 2026-08-17, second raise, deliberate and argued: SYSTEM_ANALYSIS.md 1900 → 1930 and
# IMPROVEMENT_PLAN.md 1515 → 1545 (the figure is the measured
# post-edit line count, not an estimate -- the first attempt at this comment guessed 1535 and the
# gate caught it, which is the group working). F1 defect 3's "the OSS path corrupts it" cause was REFUTED by a
# controlled A/B, and replacing a wrong cause with a measured refutation costs more lines than the
# wrong version had — the same arithmetic as the 1500 → 1515 raise above, one fault later. The
# SYSTEM_ANALYSIS.md half is four new device facts, one of which (there is no microphone and the
# codec's loopbacks run capture → playback, so audio CANNOT be measured acoustically here) closes off
# a whole class of future investigation and is worth its lines several times over. ⚠️ **Neither raise
# leaves queued headroom: the next addition to either file pays by deleting.**
# ⚠️ 2026-08-17, THIRD raise, deliberate and argued: SYSTEM_ANALYSIS.md 1930 → 1940 and
# IMPROVEMENT_PLAN.md 1545 → 1580 (measured post-edit counts, not estimates). Two new measured
# device facts, both found in one evening at the panel. (1) B33 — a USB babble error leaves a printk
# loop that outlives unplugging and hard-resets the unit ~46 min later. It is worth its lines twice:
# it is a defect, and it is a MEASUREMENT CONTAMINANT, so every future on-device reading needs its
# one-command check. (2) The audible click at every sound is now a named mechanism with a runtime
# knob (pmdown_time = 5000) plus a stream-stop teardown, replacing a section that said only "the
# candidates that remain". F1's half also shrank where it could: the four refuted-mechanism bullets
# were compressed to three, keeping every measurement.
# ⚠️ **Identified future payment, not yet spendable:** when the continuous-stream fix lands, F1
# defect 3 loses BOTH its refuted-mechanism inventory and its onset question — that is where the
# next ~25 lines come from. Until then there is no queued headroom in either file.
# ⚠️ It counts LINES, which is a proxy for size and can be gamed in both directions: a
# paragraph rewrapped to 200 characters passes while getting longer, and a table split
# across more rows fails while saying the same thing. The ~110-char wrap is a review
# rule this cannot see. Read the diff; do not let a green D stand in for that.
ceilings() {
    # `CEILINGS_FILE` exists only so --self-test can drive this group over a fixture table.
    if [ -n "${CEILINGS_FILE:-}" ]; then cat "$CEILINGS_FILE"; return; fi
    cat <<'EOF'
1940	SYSTEM_ANALYSIS.md
1580	IMPROVEMENT_PLAN.md
260	HARDWARE.md
390	CLAUDE.md
740	native_apps/CLAUDE.md
240	lib/CLAUDE.md
150	commissioning/CLAUDE.md
125	device-files/CLAUDE.md
250	tests/CLAUDE.md
270	scummvm-roomwizard/CLAUDE.md
205	vnc_client/CLAUDE.md
170	.claude/skills/doc-update/SKILL.md
EOF
}

group_d() {
    local root="$1" quiet="${2:-}"
    local bad=0 total=0 cap path n margin tight=999999 tightest=

    while IFS=$'\t' read -r cap path; do
        [ -z "$cap" ] && continue
        total=$((total + 1))
        if [ ! -f "$root/$path" ]; then
            bad=$((bad + 1))
            [ -n "$quiet" ] || printf '  MISSING FILE  %s (ceiling %s) — renamed or split? the ceiling must follow it\n' \
                "$path" "$cap"
            continue
        fi
        n="$(wc -l < "$root/$path")"
        n=$((n))
        if [ "$n" -gt "$cap" ]; then
            bad=$((bad + 1))
            [ -n "$quiet" ] || printf '  OVER CEILING  %s  %d lines, ceiling %d (+%d — delete as much, or raise it deliberately)\n' \
                "$path" "$n" "$cap" "$((n - cap))"
            continue
        fi
        margin=$((cap - n))
        if [ "$margin" -lt "$tight" ]; then tight="$margin"; tightest="$path"; fi
    done < <(ceilings)

    if [ -z "$quiet" ]; then
        printf 'group D: %d ceilings, %d over' "$total" "$bad"
        [ -n "$tightest" ] && printf ' (tightest margin: %s, %d lines left)' "$tightest" "$tight"
        printf '\n'
    fi
    echo "$bad" > "$RESULT"
    [ "$bad" -eq 0 ]
}

# ── negative controls ─────────────────────────────────────────────────────────
# A gate that reports a number can be wrong in both directions, so both directions
# are controlled: a fabricated dangling citation MUST be caught, and a citation of a
# real ID in the same file MUST NOT be. The fixture is a throwaway tree, not this
# repo, so the control cannot be confused with a real finding.
self_test() {
    local t rc=0 seen
    t="$(mktemp -d)"
    mkdir -p "$t/sub"
    cat > "$t/$PLAN_FILE" <<'EOF'
# fixture
### C99. A defined item — open
### F1. Another defined item — open
EOF
    # One dangling, one defined, one anchor (group A's, must be ignored), and a
    # continuation list mixing both kinds.
    # ⚠️ Interpolated, NOT a quoted heredoc: a literal citation here would be found by
    # the scanner in this script's own source and counted as a real finding. That is the
    # harness inflating its own number, and it did — three phantom hits on first run.
    local p="$PLAN_FILE"
    cat > "$t/sub/probe.c" <<EOF
/* $p B99 — fabricated, must be caught. */
/* $p C99 — defined, must not be caught. */
/* $p#c99-a-defined-item--open — an anchor, not a citation. */
/* (../$p F1, D98). */
/* a bare dangling tag (B97), and a bare defined one (C99). */
/* prose form: see D96.  And a register name, B0, which must NOT be caught. */
EOF
    seen="$(group_b "$t" quiet; cat "$RESULT")"
    if [ "$seen" = "4" ]; then
        echo "self-test: PASS — 4 dangling (B99, D98, B97, D96); C99, F1, the anchor and the bare B0 ignored"
    else
        echo "self-test: FAIL — expected 4 dangling, scanner reported $seen"
        rc=1
    fi

    # Group A, controlled in both directions: a heading that exists must not fire, a
    # fragment that names no heading must, a link to a file that does not exist must, and
    # — the case that would otherwise be a false PASS — a `#` line inside a fenced code
    # block must NOT count as a heading.
    cat > "$t/DOC.md" <<'EOF'
# Fixture Doc
## 2.4 Unpopulated and expansion
Resolving same-file link: [a](#24-unpopulated-and-expansion)
Dangling same-file link: [b](#no-such-section)
```bash
# 9.9 Fenced, not a heading
```
EOF
    cat > "$t/sub/anchors.md" <<'EOF'
[ok](../DOC.md#24-unpopulated-and-expansion)
[fenced](../DOC.md#99-fenced-not-a-heading)
[gone](../NOPE.md#anything)
Background: dark blue (#0a0e27) — a colour, not an anchor.
EOF
    seen="$(group_a "$t" quiet; cat "$RESULT")"
    if [ "$seen" = "3" ]; then
        echo "self-test: PASS — group A caught 3 (#no-such-section, the fenced heading, the missing file);" \
             "both good anchors, the hex colour and the root-fallback path from sub/ ignored"
    else
        echo "self-test: FAIL — group A expected 3 dangling, reported $seen"
        rc=1
    fi

    # Group C, controlled in BOTH directions: a satisfied receipt must not fire, an
    # unextracted one must, and a token still present at the source must — the last is
    # the copy-instead-of-move case, which a "destination has it" check alone passes.
    printf 'kept-token\n' > "$t/DEST.md"
    printf 'both-token\n' >> "$t/DEST.md"
    printf 'both-token\n' > "$t/SOURCE.md"
    local rf="$t/receipts.tsv"
    printf 'kept-token\tDEST.md\tSOURCE.md\n'   > "$rf"   # satisfied: at dest, not at source
    printf 'absent-token\tDEST.md\t-\n'        >> "$rf"   # must fire: never extracted
    printf 'both-token\tDEST.md\tSOURCE.md\n'  >> "$rf"   # must fire: copied, not moved
    seen="$(RECEIPTS_FILE="$rf" group_c "$t" quiet; cat "$RESULT")"
    if [ "$seen" = "2" ]; then
        echo "self-test: PASS — group C caught 2 (absent-token, both-token); the satisfied receipt ignored"
    else
        echo "self-test: FAIL — group C expected 2 unsatisfied, reported $seen"
        rc=1
    fi

    # Group D, controlled in THREE directions: over ceiling must fire, under must not, and
    # a ceiling naming a file that is not there must fire. The third is the one a
    # "count the lines and compare" check passes by accident, and it is the case that
    # matters after a split — the guard must follow the document, not the old filename.
    printf 'a\nb\nc\nd\ne\n' > "$t/BIG.md"       # 5 lines against a ceiling of 3 — must fire
    printf 'a\nb\n'          > "$t/SMALL.md"     # 2 lines against a ceiling of 9 — must not
    local cf="$t/ceilings.tsv"
    printf '3\tBIG.md\n'    > "$cf"
    printf '9\tSMALL.md\n' >> "$cf"
    printf '9\tGONE.md\n'  >> "$cf"
    seen="$(CEILINGS_FILE="$cf" group_d "$t" quiet; cat "$RESULT")"
    if [ "$seen" = "2" ]; then
        echo "self-test: PASS — group D caught 2 (BIG.md over, GONE.md missing); SMALL.md under ceiling ignored"
    else
        echo "self-test: FAIL — group D expected 2, reported $seen"
        rc=1
    fi

    rm -rf "$t"
    return $rc
}

# ── main ──────────────────────────────────────────────────────────────────────
RESULT="$(mktemp)"
trap 'rm -f "$RESULT"' EXIT

case "${1:-}" in
    --self-test)
        self_test || fail_total=1
        ;;
    --help|-h)
        sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
        exit 0
        ;;
    *)
        self_test || fail_total=$((fail_total + 1))
        echo
        echo "Markdown anchors that resolve to no heading:"
        group_a "$REPO" || fail_total=$((fail_total + 1))
        echo
        echo "Plan-ID citations outside $PLAN_FILE that resolve to no heading:"
        group_b "$REPO" || fail_total=$((fail_total + 1))
        echo
        echo "Extraction receipts — each fact must be at its destination and gone from the source:"
        group_c "$REPO" || fail_total=$((fail_total + 1))
        echo
        echo "Document size ceilings — growth is paid for or decided on, never absorbed:"
        group_d "$REPO" || fail_total=$((fail_total + 1))
        ;;
esac

echo
if [ "$fail_total" -eq 0 ]; then
    echo "doc_check: PASS"
else
    echo "doc_check: FAIL ($fail_total group(s))"
fi
exit "$fail_total"
