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
# instruction MNEMONIC field does not.  Measured over all 30 build artifacts,
# the true instruction count is 0.  So this check demands a hard zero: if it
# ever fires, it is real.

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

for bin in "${TARGETS[@]}"; do
    [ -f "$bin" ] || { echo -e "${RED}✗ $bin: no such file${NC}"; bad=$((bad + 1)); continue; }

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
if [ "$bad" -gt 0 ]; then
    echo -e "${RED}✗ $bad of $checked binaries would SIGILL on Cortex-A8 — NOT deploying${NC}"
    echo "  Recompile with -mcpu=cortex-a8 (or check for an -march that implies idiv)."
    exit 1
fi

echo -e "${GREEN}  ✓ ARM-safe: no hardware divide in $checked binaries${NC}"
exit 0
