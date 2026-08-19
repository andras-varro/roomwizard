/*
 * audio_tone_test — audio_tone()'s CHAINING RULE, on the host
 *
 * F1 phase 3d.  The mix bus made "two sounds at once" possible, and then one line
 * in `audio_tone()` quietly took it back for a whole class of sound: a new tone
 * defaults its start delay to the TAIL OF THE PRECEDING TONE, with no test of how
 * long ago that tone was issued.  So a tap 400 ms after `440 3s` was scheduled 2.6 s
 * out, and the operator heard exactly that — *"the audio is still serialized"*.
 * Canned sounds (`audio_success()` and friends) go through `play_sequence()`, which
 * calls `audio_mix_add()` directly and never touches `last_tone_slot`, so they
 * overlapped a drone correctly the whole time.  That asymmetry — SUCCESS mixes, a
 * plain tone queues — is the fingerprint of this defect and nothing else.
 *
 * The fix is a RECENCY GUARD, not the removal of chaining: chaining is load-bearing.
 * `tetris/tetris.c:620-621`, `tetris/tetris.c:714-715` and `snake/snake.c:317-318`
 * each play a two-note motif as two back-to-back `audio_tone()` statements with
 * nothing between them, so at delay 0 all three collapse into DYADS.  Those pairs
 * are microseconds apart; an independent tap is at least one frame away
 * (`FRAME_DELAY_ACTIVE_US` = 33333, `common/common.h:19`).  Group A is the motif and
 * group B is the tap, and a fix that satisfies one while breaking the other is
 * caught here rather than at the panel.
 *
 * ⚠️ **This file deliberately does NOT name `AUDIO_TONE_CHAIN_MS`.** It spells its
 * gaps as plain numbers with wide margins, because the only way to know a test can
 * fail is to compile it against the PRE-FIX source — and pre-fix that constant does
 * not exist.  There is no CI here; a test that has only ever been seen passing is
 * not evidence.  Keep it compilable against both sides.
 *
 * ⚠️ **`common/audio.c` is the device half, and this is the first host test to link
 * it.**  It has no `__has_include` split the way `common/audio_out.c` does
 * (`audio_out.c:465-478`) and it must not grow one — it has nothing to degrade TO.
 * `tests/hostshim/sys/soundcard.h` supplies the header this host spells
 * `<linux/soundcard.h>` instead, so `audio.c` compiles unmodified.  Nothing here
 * opens `/dev/dsp`: mk_audio() builds an `Audio` by hand, which is legitimate
 * because `struct Audio` is public in `audio.h` and the mixing branch of
 * `audio_tone()` touches no fd (`audio.c:708-712` says so in as many words).
 *
 * Build and run (host gcc, from native_apps/):
 *   gcc -Wall -Wextra -Wno-unused-parameter -I. -Itests/hostshim \
 *       -o build/audio_tone_test tests/audio_tone_test.c \
 *       common/audio.c common/audio_gen.c common/audio_out.c common/config.c -lm && \
 *   ./build/audio_tone_test
 *
 * It also runs ON THE DEVICE, and there the shim is not wanted — the cross
 * toolchain has the real `<sys/soundcard.h>`, so leaving `-Itests/hostshim` off is
 * what makes the ARM binary compile the same header the shipped build does.  It
 * needs no framebuffer and no touch, only /dev/null, so it is one of the few
 * on-device checks that needs no human at the panel:
 *   arm-linux-gnueabihf-gcc -Wall -Wextra -Wno-unused-parameter -O2 -static -I. \
 *       -o build/audio_tone_test_arm tests/audio_tone_test.c \
 *       common/audio.c common/audio_gen.c common/audio_out.c common/config.c -lm
 *   scp build/audio_tone_test_arm root@<ip>:/tmp/ && \
 *   ssh root@<ip> "chmod +x /tmp/audio_tone_test_arm && /tmp/audio_tone_test_arm"
 * Measured 2026-08-19 on RW .188: byte-for-byte the same 9 ok lines as the host,
 * worst tap tail 200 ms on both.
 *
 * ⚠️ **Group A is group B's negative control, which is why it must not be deleted
 * as redundant.**  A guard that is accidentally always-false passes B and C for the
 * wrong reason — it would look like a fix while having abolished chaining outright.
 * A only passes if the guard can be TRUE and the clock behind it works, and B only
 * passes if it can be FALSE, so the pair is self-controlling on a device where
 * nothing else can be injected.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>

#include "common/audio.h"
#include "common/audio_gen.h"

static int failures = 0;
static int checks   = 0;

static void check(bool cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("  FAIL: %s\n", what); }
    else       { printf("  ok:   %s\n", what); }
}

#define TEST_RATE 44100

/* A pumping bus with no device behind it.
 *
 * `cont` stays FALSE and `dsp_fd` is a real descriptor on /dev/null, because
 * audio_live() (`audio.c:71-75`) reads audio_out_is_open() when `cont` is set and
 * there is no open stream here — audio_interrupt() would become a silent no-op and
 * group E would pass for the wrong reason.  Neither path under test writes to the
 * fd; it exists so that if one ever does, it lands in /dev/null rather than on this
 * test's own stdout. */
static int mk_audio(Audio *a)
{
    int fd = open("/dev/null", O_WRONLY);
    if (fd < 0) { printf("  FAIL: cannot open /dev/null\n"); failures++; return -1; }

    memset(a, 0, sizeof(*a));
    a->dsp_fd       = fd;
    a->sample_rate  = TEST_RATE;
    a->channels     = 2;
    a->available    = true;
    a->cont         = false;
    a->vol          = AUDIO_VOICE_VOL;
    a->master_shift = AUDIO_MASTER_SHIFT;
    audio_mix_init(&a->mix, TEST_RATE);
    audio_mix_set_knee(&a->mix, audio_voice_peak(a->vol));
    a->pumping        = true;
    a->last_tone_slot = -1;
    a->last_tone_gen  = 0;
    return fd;
}

/* What the LAST tone audio_tone() added still owes, delay included, in ms.
 * The bus only advances when something renders it, and nothing in this test
 * renders — so these numbers are pure scheduling, with no timing slop. */
static long last_tone_pending_ms(const Audio *a)
{
    return audio_ms_for_frames(a->sample_rate,
                               audio_mix_voice_pending(&a->mix, a->last_tone_slot,
                                                       a->last_tone_gen));
}

/* One frame is 33 ms; sleeping 4x that leaves no argument about which side of a
 * half-frame threshold the gap fell on, and costs the suite 0.13 s per use. */
static void gap_one_tap(void) { usleep(133000); }

int main(void)
{
    Audio a;
    int fd;

    printf("audio_tone_test — the chaining rule\n\n");

    printf("A. a two-note motif still chains (tetris.c:620-621, snake.c:317-318)\n");
    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        audio_tone(&a, 500, 60);
        long first = last_tone_pending_ms(&a);
        audio_tone(&a, 250, 70);
        long second = last_tone_pending_ms(&a);
        check(first >= 50 && first <= 75, "note 1 owes only its own 60 ms");
        check(second >= 110,
              "note 2 issued in the SAME frame queues behind note 1, not on top of it");
        check(second <= 160, "and behind note 1 alone, not behind the whole bus");
        close(fd);
    }

    printf("\nB. THE DEFECT: an independent tap does not inherit a drone's tail\n");
    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        audio_tone(&a, 220, 3000);          /* the DRONE pad */
        gap_one_tap();
        audio_tone(&a, 880, 200);           /* an unrelated tap, frames later */
        long tap = last_tone_pending_ms(&a);
        check(tap <= 400,
              "a tap 133 ms after a 3 s drone starts NOW, not 3 s out");
        check(tap >= 150, "and it is really scheduled — not silently dropped");
        close(fd);
    }

    printf("\nC. and taps do not ACCUMULATE behind each other (the panel's six taps)\n");
    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        audio_tone(&a, 220, 3000);
        long worst = 0;
        for (int i = 0; i < 4; i++) {
            gap_one_tap();
            audio_tone(&a, 880, 200);
            long p = last_tone_pending_ms(&a);
            if (p > worst) worst = p;
        }
        check(worst <= 400,
              "four spaced taps over a drone each start now (worst tail stays small)");
        printf("        (worst tap tail: %ld ms)\n", worst);
        close(fd);
    }

    printf("\nD. canned sounds were never part of this — they bypass last_tone_slot\n");
    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        audio_tone(&a, 220, 3000);
        int      slot = a.last_tone_slot;
        uint32_t gen  = a.last_tone_gen;
        audio_success(&a);                  /* play_sequence(), audio.c:895-906 */
        check(a.last_tone_slot == slot && a.last_tone_gen == gen,
              "audio_success() leaves last_tone_slot alone (so it overlapped all along)");
        check(audio_mix_active(&a.mix) >= 4,
              "and its notes really did land on the bus beside the drone");
        close(fd);
    }

    printf("\nE. audio_interrupt() still clears the tail (the ~23 interrupt+tone sites)\n");
    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        audio_tone(&a, 220, 3000);
        audio_interrupt(&a);
        audio_tone(&a, 880, 200);
        long p = last_tone_pending_ms(&a);
        check(p >= 150 && p <= 260,
              "a tone straight after an interrupt owes only its own 200 ms");
        close(fd);
    }

    printf("\n%s  %d checks, %d failure(s)\n",
           failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
