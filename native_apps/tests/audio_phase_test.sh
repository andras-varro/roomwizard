#!/bin/sh
# Play the three phase-test tones, each preceded by N marker clicks, with long gaps.
#
#   1 click  -> LEFT  (L = s, R = 0)      reference, known audible
#   2 clicks -> DUP   (L = s, R = s)      what the F1 mono-to-stereo design would write
#   3 clicks -> ANTI  (L = s, R = -s)
#
# The markers exist so a SILENT tone still identifies itself: "two clicks then
# nothing" is a result, "I only heard two beeps" is not.
# The clicks go through plughw (the vendor WAVs are mono); the tones go straight
# at hw:0,0 with no conversion.

set -u
CLICK=/opt/sound/asl_click.wav
i=1
for m in left dup anti; do
	n=1
	while [ "$n" -le "$i" ]; do
		aplay -D plughw:0,0 "$CLICK" >/dev/null 2>&1
		sleep 1
		n=$((n + 1))
	done
	sleep 1
	echo "  playing $i: $m"
	aplay -D hw:0,0 "/tmp/ph_$m.wav" >/dev/null 2>&1
	echo "    rc=$?"
	sleep 3
	i=$((i + 1))
done
echo "done"
