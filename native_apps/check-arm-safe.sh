#!/bin/bash
# Pre-deploy gate: reject ARM binaries containing hardware integer-divide
# instructions.
#
# The Cortex-A8 in the OMAP3503 has NO hardware integer divide.  A binary
# containing sdiv/udiv dies instantly with SIGILL (exit 132) — blank screen,
# no output, no log.  It is the worst failure mode in this project because it
# looks identical to "the app didn't start".
#
# Usage:
#   ./check-arm-safe.sh                 # check every executable in build/
#   ./check-arm-safe.sh path/to/binary  # check specific files
#
# Exit 0 = safe to deploy.  Exit 1 = do not deploy.
#
# ── On the "~45 known hits" ──────────────────────────────────────────────────
# Earlier documentation said a -static glibc binary always carries ~45
# unreachable sdiv/udiv that must be allowlisted.  That is not correct, and no
# allowlist is needed.  Those 45 are SUBSTRING matches on the *names* of the
# software divide helpers the compiler emits calls to:
#
#     bl  195c8 <__udivsi3>          <-- "udiv" appears inside "__udivsi3"
#         <__udivmoddi4>             <-- and inside "__udivmoddi4"
#
# i.e. they are positive evidence that division is being done in software.
# Grepping for the bare string "sdiv\|udiv" matches them; matching the
# instruction MNEMONIC field does not.  Measured across every build artifact,
# the true instruction count is 0.  So this check demands a hard zero: if it
# ever fires, it is real.
#
# ── This check needs the symbol table: it cannot judge a stripped binary ─────
# objdump needs the symbol table to tell Thumb-2 from ARM.  These binaries are
# Thumb-2; stripped, objdump falls back to 32-bit ARM and re-reads the same bytes
# as ARM words, manufacturing divides that are not in the file.  Measured on
# samegame 2026-08-08 — `objdump -s` prints 4846ebf7 1bfe3de7 at 0x42618 for the
# unstripped file and the stripped copy alike, because strip cannot alter .text:
#
#     unstripped (Thumb):  4648  mov r0, r9 / f7eb fe1b  bl … / e78e  b.n …
#     stripped   (ARM):    e73dfe1b   udiv sp, fp, lr       <-- not in the file
#
# So a stripped target is reported as UNVERIFIABLE (exit 2) and never as a hit.
# ⚠️ Do not "just eyeball the operands" instead: the phantoms are not reliably
# invalid.  `udiv pc, fp, sl` is dismissible, but `udiv r7, r1, lr` is a legal
# encoding indistinguishable from compiler output.  And do not silently skip a
# stripped binary either — that is the same bug from the false-negative side.
# The sound check is at BUILD time, on the unstripped artifact, which is where all
# three component build scripts call it from.  Detail: IMPROVEMENT_PLAN.md C9.
#
# Exit 0 = safe to deploy.  Exit 1 = do not deploy.  Exit 2 = something could not
# be judged; the last line always carries the three counts as ARM-SUMMARY.

set -u

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

OBJDUMP="${OBJDUMP:-arm-linux-gnueabihf-objdump}"
command -v "$OBJDUMP" >/dev/null 2>&1 || {
    echo -e "${RED}✗ $OBJDUMP not found${NC}" >&2
    echo "  Install with: sudo apt-get install gcc-arm-linux-gnueabihf" >&2
    exit 1
}

# Default to every executable regular file in build/
if [ "$#" -gt 0 ]; then
    TARGETS=("$@")
else
    TARGETS=()
    for f in build/*; do
        [ -f "$f" ] && [ -x "$f" ] && TARGETS+=("$f")
    done
fi

[ "${#TARGETS[@]}" -gt 0 ] || { echo -e "${YELLOW}! nothing to check${NC}"; exit 0; }

checked=0
bad=0

skipped=0
unverified=0
UNVERIFIED_NAMES=""

for bin in "${TARGETS[@]}"; do
    [ -f "$bin" ] || { echo -e "${RED}✗ $bin: no such file${NC}"; bad=$((bad + 1)); continue; }

    # build/ also collects HOST binaries — the tests/ regressions are compiled
    # with native gcc into the same directory.  arm-objdump cannot disassemble
    # an x86 ELF, so those would count as "checked" while proving nothing, which
    # makes the headline number a lie.  Skip anything that is not an ARM ELF;
    # every real deploy artifact still goes through the check below.
    if ! "$OBJDUMP" -f "$bin" 2>/dev/null | grep -qi 'architecture: *arm'; then
        skipped=$((skipped + 1))
        continue
    fi

    # No symbol table means objdump reads Thumb-2 as ARM and invents divides that
    # are not in the file — see the header. There is no verdict to be had here, in
    # either direction, so do not disassemble it and do not count it as checked.
    if ! "$OBJDUMP" -t "$bin" 2>/dev/null | grep -q '\.text'; then
        unverified=$((unverified + 1))
        UNVERIFIED_NAMES="$UNVERIFIED_NAMES$bin
"
        continue
    fi

    # Track the enclosing symbol, print it for every real sdiv/udiv instruction.
    # objdump format:  "   121d8:\t<encoding>\tmnemonic\toperands"
    # so the mnemonic is a tab-delimited field, which "<__udivsi3>" never is.
    hits=$("$OBJDUMP" -d "$bin" 2>/dev/null | awk '
        /^[0-9a-f]+ </      { fn = $2; gsub(/[<>:]/, "", fn) }
        /\t(sdiv|udiv)(\.w)?\t/ { print fn ": " $0 }
    ')

    checked=$((checked + 1))

    if [ -n "$hits" ]; then
        echo -e "${RED}✗ $bin — hardware divide instruction(s):${NC}"
        printf '%s\n' "$hits" | sed 's/^/      /'
        bad=$((bad + 1))
    fi
done

echo ""

# One line, always, in every exit path: a caller that has to act on the numbers
# should not have to parse prose. commission-offline.sh reads this.
summary_line() {
    printf 'ARM-SUMMARY checked=%d unverified=%d bad=%d skipped=%d\n' \
           "$checked" "$unverified" "$bad" "$skipped"
}

if [ "$unverified" -gt 0 ]; then
    echo -e "${YELLOW}  ! $unverified binary(ies) NOT verified — stripped, no symbol table:${NC}"
    printf '%s' "$UNVERIFIED_NAMES" | sed 's/^/        /'
    echo    "    objdump needs the symbol table to tell Thumb-2 from ARM. Without it it"
    echo    "    reads ordinary Thumb code as ARM words and invents sdiv/udiv that are"
    echo    "    not in the file, so a verdict here would be wrong in both directions"
    echo    "    and none is given. The sound check is at BUILD time, on the unstripped"
    echo    "    artifact (IMPROVEMENT_PLAN.md C9)."
fi

if [ "$bad" -gt 0 ]; then
    echo -e "${RED}✗ $bad of $checked binaries would SIGILL on Cortex-A8 — NOT deploying${NC}"
    echo "  Recompile with -mcpu=cortex-a8 (or check for an -march that implies idiv)."
    summary_line
    exit 1
fi

# Only claim a clean bill for what was actually disassembled. "no hardware divide
# in 0 binaries" is a pass over nothing, and printing it next to an unverified
# count would read as an all-clear.
if [ "$checked" -gt 0 ]; then
    echo -e "${GREEN}  ✓ ARM-safe: no hardware divide in $checked binaries${NC}"
fi
if [ "$skipped" -gt 0 ]; then
    echo -e "${YELLOW}  ! skipped $skipped non-ARM file(s) in build/ (host-compiled tests)${NC}"
fi
summary_line
[ "$unverified" -eq 0 ] || exit 2
exit 0
