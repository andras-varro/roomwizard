#!/bin/sh
# alsa_probe.sh - what will hw:0,0 actually grant us?
#
# Runs ON THE DEVICE, needs nothing cross-compiled and nothing shipped: the vendor
# image already carries alsa-lib 1.2.1.2 plus aplay/amixer/alsactl, and this aplay
# has --dump-hw-params.  That is the same SNDRV_PCM_IOCTL_HW_REFINE that tinyalsa
# would call, so the ranges below are the ranges our backend gets.
#
#   scp native_apps/tests/alsa_probe.sh root@<ip>:/tmp/ && ssh root@<ip> sh /tmp/alsa_probe.sh
#
# stdout-only, no touch, no framebuffer -> fully automatable.  Only step 7 needs an ear.
# Everything before it is silent; step 7 is the one thing a human has to confirm.

set -u
PCM=hw:0,0
CLICK=/opt/sound/asl_click.wav

say() { echo; echo "=== $* ==="; }

say "1. card and pcm inventory"
cat /proc/asound/cards
echo "--- /proc/asound/pcm"
cat /proc/asound/pcm
echo "--- device nodes"
ls -l /dev/snd/ 2>&1

say "2. is anything holding the PCM right now"
# A native app with /dev/dsp open occupies this PCM through the shim, and every
# measurement below would then report a busy device rather than the hardware.
for p in /proc/*/fd; do
	pid=${p%/fd}; pid=${pid#/proc/}
	case "$pid" in *[!0-9]*) continue ;; esac
	if ls -l "$p" 2>/dev/null | grep -q "dsp\|/dev/snd/"; then
		echo "PID $pid holds an audio fd: $(cat /proc/$pid/cmdline 2>/dev/null | tr '\0' ' ')"
	fi
done
echo "(no lines above = PCM is free)"

say "3. hw:0,0 RANGES  -- Phase 0 questions 1 and 3"
# PERIOD_SIZE min is the latency answer; CHANNELS min/max is the mono answer.
aplay -D "$PCM" --dump-hw-params "$CLICK" 2>&1

say "4. which rates does hw:0,0 accept DIRECTLY  -- Phase 0 question 2"
# tinyalsa has no plug/resampler, so a rate that needs alsa-lib's plug is a rate we
# cannot use.  Silent: 1 s of /dev/zero, raw, S16_LE.
#
# CHANNELS from step 3 is the count to use here.  The first version of this loop passed
# -c 1 and printed REFUSED for all six rates -- every one of them failing on the channel
# count, not on the rate.  Six identical wrong answers from one harness mistake; read
# step 3 before believing step 4.
for c in 1 2; do
	echo "  -- $c channel(s)"
	for r in 8000 11025 22050 32000 44100 48000 96000; do
		printf '     %6s Hz: ' "$r"
		if aplay -D "$PCM" -t raw -f S16_LE -c "$c" -r "$r" -d 1 /dev/zero >/dev/null 2>&1; then
			echo "accepted"
		else
			echo "REFUSED"
		fi
	done
done

say "5. what does it GRANT for the config we intend to use"
# -v prints the resolved hw_params (rate, period, buffer) after negotiation.  Ask for a
# SMALL period explicitly: the question is not what it defaults to, it is what it grants.
aplay -D "$PCM" -v -t raw -f S16_LE -c 2 -r 48000 --period-size=1024 --buffer-size=4096 \
	-d 1 /dev/zero 2>&1 | sed -n '1,40p'
echo "--- and at ScummVM's rate"
aplay -D "$PCM" -v -t raw -f S16_LE -c 2 -r 22050 --period-size=1024 --buffer-size=4096 \
	-d 1 /dev/zero 2>&1 | sed -n '1,40p'

say "6. amp and mixer state"
cat /sys/class/gpio/gpio12/direction 2>&1
cat /sys/class/gpio/gpio12/value 2>&1
for c in "DAC1 Digital Fine Playback Volume" "DAC1 Digital Coarse Playback Volume" \
	 "PreDriv Playback Volume" "HandsfreeL Switch" "HandsfreeR Switch" \
	 "HandsfreeL Mux" "HandsfreeR Mux"; do
	echo "--- $c"
	amixer -c 0 cget name="$c" 2>&1 | grep -v '^numid' | head -3
done

say "7. AUDIBLE: native ALSA end to end, independent of any code we write"
# The shipped WAVs are MONO and hw:0,0 is stereo-only, so a direct hw:0,0 aplay of one
# fails on channels and proves nothing -- that is not a broken audio path.  Two tests:
#   a) plughw:0,0 lets alsa-lib convert mono -> stereo.  Proves the kernel path.
#   b) speaker-test generates its own stereo tone, so it needs no plug at all -- this is
#      the one that matches what our tinyalsa backend will do.
echo "--- a) mono WAV through plughw (alsa-lib converts):  listen for a click x3"
for i in 1 2 3; do aplay -D plughw:0,0 "$CLICK" >/dev/null 2>&1; echo "     play $i -> exit $?"; done
echo "--- b) stereo sine straight at hw:0,0 (no plug, no conversion): listen for a tone"
speaker-test -D "$PCM" -c 2 -t sine -f 440 -l 1 2>&1 | tail -12
echo "     speaker-test -> exit $?"

say "done"
