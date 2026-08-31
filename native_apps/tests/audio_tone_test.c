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
 *       common/audio.c common/audio_gen.c common/audio_out.c common/audio_wav.c common/config.c -lm && \
 *   ./build/audio_tone_test
 *
 * ⚠️ **This host passes all 58, and `/dev/dsp` is irrelevant to that.**  It used to
 * report `FAILED 58 checks, 34 failure(s)`, and this comment used to explain those
 * 34 as the ENVIRONMENT — a host with no sound device.  That was WRONG, measured
 * 2026-08-31: the cause was two missing lines in mk_audio() below.  The EFFECTS and
 * MUSIC toggles landed in `common/audio.c` after this file was written, and every
 * entry point begins by reading one of them — `audio_tone()` (:852),
 * `audio_fx_play()` (:1265), `audio_music_start()` (:1679), `audio_sfx_play()`
 * (:1708) — so a hand-built `Audio` that had been memset to zero was refused at the
 * door by code doing exactly what it says it does.  Setting both fields took the
 * count to 0.  audio_tone()'s own comment says it must work with `dsp_fd` at -1, and
 * it does.
 *
 * ⚠️ **The lesson is about the INSTRUMENT, not the toggles: 34 checks agreed with
 * each other for a whole week, and the agreement was the tell.**  A group that
 * always fails is unfalsifiable in both directions — it can neither catch a
 * regression nor be seen to catch one, and its sabotage harness reports "caught"
 * for the wrong reason (`tests/measure_audio_clip_sabotage.sh` prints a FAIL count,
 * and 34 unrelated failures read the same as a detection).  The old paragraph made
 * that permanent by instructing the reader to expect the 34 and compare against
 * HEAD — and HEAD was silenced identically, so the comparison agreed.  ⚠️ **If a
 * failure count here is not ZERO, read the failing lines; never restore a paragraph
 * that explains a number away.**
 * It also runs ON THE DEVICE, and there the shim is not wanted — the cross
 * toolchain has the real `<sys/soundcard.h>`, so leaving `-Itests/hostshim` off is
 * what makes the ARM binary compile the same header the shipped build does.  It
 * needs no framebuffer and no touch, only /dev/null, so it is one of the few
 * on-device checks that needs no human at the panel:
 *   arm-linux-gnueabihf-gcc -Wall -Wextra -Wno-unused-parameter -O2 -static -I. \
 *       -o build/audio_tone_test_arm tests/audio_tone_test.c \
 *       common/audio.c common/audio_gen.c common/audio_out.c common/audio_wav.c common/config.c -lm
 *   scp build/audio_tone_test_arm root@<ip>:/tmp/ && \
 *   ssh root@<ip> "chmod +x /tmp/audio_tone_test_arm && /tmp/audio_tone_test_arm"
 * Measured 2026-08-19 on RW .188: byte-for-byte the same ok lines as the host,
 * worst tap tail 200 ms on both.
 *
 * ⚠️ **Group F is a SECOND subject in this file, and it is here rather than in a new
 * one because this is the only host test that links `common/audio.c`.**  It asserts
 * which limiter a first bus session runs — `bus_reset()` carries `mix.limit` across a
 * session on purpose, and `AUDIO_MIX_SOFT` being 0 meant a never-armed bus read that
 * zero as an operator's choice.  Its second check is the negative control: a fix that
 * hardwired HARD inside `bus_reset()` would satisfy the first and destroy the
 * carry-across the panel's `LIM` pad depends on.
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
    /* ⚠️ BOTH TOGGLES, and they are load-bearing rather than tidy: every entry
     * point this file tests begins by reading one of them, so a zeroed `Audio`
     * is refused at the door and 34 of 58 checks fail for a harness reason.
     * See the header — that count was misread as a missing /dev/dsp for a week.
     * audio_open() sets them from config; a hand-built Audio must say so itself. */
    a->music_on       = true;
    a->effects_on     = true;
    a->pumping        = true;
    a->last_tone_slot = -1;
    a->last_tone_gen  = 0;
    /* Mirrors what level_defaults() does for the clip voices on the shipped path,
     * because this test builds an `Audio` by hand and never runs audio_open().
     * ⚠️ It is NOT what protects the read — a zeroed voice reads (slot 0, gen 0)
     * and gen 0 is never issued, so the pair already answers "free".  It is here
     * so `slot >= 0` keeps meaning "has been armed", which group I asserts.
     * `fx_path` stays empty: a test that inherited /opt/sound would pass or fail
     * according to what happens to be installed on the host. */
    for (int i = 0; i < AUDIO_CLIP_VOICES; i++) a->fxv[i].slot = -1;
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

/* ── the bed's fixture, for groups G and H ───────────────────────────────────
 * One second of mono PCM with the canonical 44-byte header.  ⚠️ Deliberately
 * NOT a chunk-walk fixture: WHERE `data` starts is `tests/audio_sample_test.c`
 * group A's subject and belongs in exactly one suite.  What is under test here
 * is `audio.c`'s side — start, pause, resume, and who is asked whether a voice
 * is still live.
 */
#define BED_FIXTURE "/tmp/at_bed.wav"

/* Group I's fixtures.  Short, because a clip is an EFFECT: 4410 frames is 100 ms
 * at TEST_RATE, which is the length the stock set actually renders (27–200 ms).
 * CLIP_TOO_BIG is written once and removed — it is 353 KB and exists only to
 * prove the ceiling that stops a 3.9 MB bed being RAM-loaded inside a tap. */
#define CLIP_FIXTURE  "/tmp/at_clip.wav"
#define CLIP_FIXTURE2 "/tmp/at_clip2.wav"
#define CLIP_ABSENT   "/tmp/at_clip_absent.wav"
#define CLIP_TOO_BIG  "/tmp/at_clip_big.wav"
#define CLIP_TAIL     "/tmp/at_clip_tail.wav"
#define CLIP_FRAMES   4410L

static void put32(FILE *f, unsigned long v)
{
    fputc((int)(v & 0xff), f);         fputc((int)((v >> 8) & 0xff), f);
    fputc((int)((v >> 16) & 0xff), f); fputc((int)((v >> 24) & 0xff), f);
}

static void put16(FILE *f, unsigned int v)
{
    fputc((int)(v & 0xff), f); fputc((int)((v >> 8) & 0xff), f);
}

static void write_bed_fixture(const char *path, long frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  FAIL: cannot write %s\n", path); failures++; return; }

    long data_bytes = frames * 2;
    fwrite("RIFF", 1, 4, f);  put32(f, (unsigned long)(36 + data_bytes));
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);  put32(f, 16);
    put16(f, 1);  put16(f, 1);                     /* PCM, mono              */
    put32(f, TEST_RATE);  put32(f, TEST_RATE * 2); /* rate, byte rate        */
    put16(f, 2);  put16(f, 16);                    /* block align, bits      */
    fwrite("data", 1, 4, f);  put32(f, (unsigned long)data_bytes);
    for (long i = 0; i < frames; i++)
        put16(f, (unsigned int)(uint16_t)(int16_t)(8000 * ((i / 50) % 2 ? 1 : -1)));
    fclose(f);
}

/* The loudest sample the bus produced over `frames`.  ⚠️ Group I needs this
 * because every other assertion there reads a POSITION, and a fill that replays
 * the clip's head while advancing the cursor moves every position correctly.
 * Content is the only thing that separates those two. */
static int render_peak(Audio *a, long frames)
{
    static int16_t mono[2048];
    int peak = 0;
    while (frames > 0) {
        long n = frames > 2048 ? 2048 : frames;
        audio_mix_render(&a->mix, mono, n);
        for (long i = 0; i < n; i++) {
            int v = mono[i] < 0 ? -mono[i] : mono[i];
            if (v > peak) peak = v;
        }
        frames -= n;
    }
    return peak;
}

/* A clip whose first three quarters are SILENT and whose last quarter is loud.
 * Playing it proves the cursor reaches the tail: a fill that ignores `pos` keeps
 * returning the silent head, and no position check can tell. */
static void write_tail_fixture(const char *path, long frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  FAIL: cannot write %s\n", path); failures++; return; }

    long data_bytes = frames * 2;
    fwrite("RIFF", 1, 4, f);  put32(f, (unsigned long)(36 + data_bytes));
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);  put32(f, 16);
    put16(f, 1);  put16(f, 1);
    put32(f, TEST_RATE);  put32(f, TEST_RATE * 2);
    put16(f, 2);  put16(f, 16);
    fwrite("data", 1, 4, f);  put32(f, (unsigned long)data_bytes);
    for (long i = 0; i < frames; i++) {
        int16_t s = (i < (frames * 3) / 4) ? 0
                  : (int16_t)(8000 * ((i / 50) % 2 ? 1 : -1));
        put16(f, (unsigned int)(uint16_t)s);
    }
    fclose(f);
}

/* Render the bus for real, which is the only thing that advances a voice — and
 * the only thing that finishes a release.  A game does this through
 * audio_pump(); here there is no device, so drive the mixer directly. */
static void render_frames(Audio *a, long frames)
{
    static int16_t mono[2048];
    while (frames > 0) {
        long n = frames > 2048 ? 2048 : frames;
        audio_mix_render(&a->mix, mono, n);
        frames -= n;
    }
}

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

    printf("\nF. a first bus session runs the DOCUMENTED limiter, not a zeroed field\n");
    {
        Audio a2;
        /* 0xA5 rather than 0: if the defaults came from the caller's memset
         * instead of from the init path, every check below reads garbage and
         * says so.  audio_open() memsets internally, so this only proves the
         * assertion is about init and not about how this test arrived. */
        memset(&a2, 0xA5, sizeof(a2));
        (void)audio_init_unchecked(&a2);   /* no /dev/dsp here; defaults still land */
        audio_pump_enable(&a2, true);
        check(audio_mix_get_limit(&a2.mix) == AUDIO_MIX_HARD,
              "the first pump session runs AUDIO_MIX_HARD");

        /* The negative control, and it is the whole reason this is two checks:
         * bus_reset() deliberately carries the limiter across a re-arm so a
         * panel A/B is not undone mid-comparison.  A "fix" that hardwired HARD
         * in bus_reset() would pass the check above and break this one. */
        audio_mix_set_limit(&a2.mix, AUDIO_MIX_SOFT);
        audio_pump_enable(&a2, false);
        audio_pump_enable(&a2, true);
        check(audio_mix_get_limit(&a2.mix) == AUDIO_MIX_SOFT,
              "and an operator's SOFT still survives a re-arm (carry-across intact)");
    }

    printf("\nG. the BED: a pause RESUMES where it stopped (platformer's music)\n");
    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        write_bed_fixture(BED_FIXTURE, TEST_RATE);   /* 1 s of mono tone */

        check(audio_music_start(&a, BED_FIXTURE, true),
              "a bed starts on a pumping bus");
        check(audio_music_active(&a), "and the mixer reports it live");
        check(!audio_music_start(&a, BED_FIXTURE, true),
              "a second start is refused while the first still sounds");
        long declared = audio_mix_voice_pending(&a.mix, a.music.slot, a.music.gen);

        render_frames(&a, TEST_RATE / 4);            /* 250 ms of real rendering */
        long pos = a.music.wav.pos;
        check(pos >= TEST_RATE / 4,
              "rendering advanced the FILE, not just the voice");

        check(audio_music_pause(&a), "pause arms the release");
        render_frames(&a, TEST_RATE / 2);            /* and the release finishes */
        check(!audio_music_active(&a), "which the mixer then reports as gone");
        check(a.music.wav.f != NULL,
              "but the file is still OPEN — that is the whole difference from stop");

        check(audio_music_resume(&a), "resume re-arms a voice over the held file");
        check(a.music.wav.pos >= pos,
              "and it continues from the held position rather than from 0");
        /* ⚠️ The arithmetic sample_arm() exists for: a resumed voice must declare
         * what is LEFT of the file.  Declaring the whole file again would outlive
         * its data and the mixer would pad the tail with silence. */
        check(audio_mix_voice_pending(&a.mix, a.music.slot, a.music.gen) < declared,
              "declaring the REMAINDER, not the whole file again");
        check(!audio_music_resume(&a),
              "and a second resume is refused — nothing is held any more");
        close(fd);
    }

    printf("\nH. PUMP: OFF clears the bed, and audio_music_active() must SEE that\n");
    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        write_bed_fixture(BED_FIXTURE, TEST_RATE);
        check(audio_music_start(&a, BED_FIXTURE, true), "a bed is on the bus");

        /* The SILENT one.  bus_reset() clears every voice without telling
         * audio.c, so a local `playing` flag would read true forever and refuse
         * every restart — a game whose music never comes back, with nothing in
         * any counter to say why.  sample_live() asks the mixer by (slot, gen). */
        audio_pump_enable(&a, false);
        check(!audio_music_active(&a),
              "a bed cleared by PUMP: OFF reads false, not stuck live");
        audio_pump_enable(&a, true);
        check(audio_music_start(&a, BED_FIXTURE, true),
              "so a fresh bed is accepted afterwards");
        close(fd);
    }

    printf("\nI. the CLIP behind a canned sound — the cursor, the fallback, the guards\n");
    {
        /* ⚠️ Group I is the third subject in this file, here for the same reason
         * as F and G: this is the only host test that links `common/audio.c`.
         * What it asserts is F1 Phase 5 ③ — that `audio_beep()` and friends play
         * a recorded clip when one is configured, and their note table when one is
         * not.  The AUDIBILITY the clip buys is acoustic and ear-only; what is
         * checkable here is which voice kind ran, and that a clip can RETRIGGER
         * where the one streaming sfx voice must refuse. */
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        write_bed_fixture(CLIP_FIXTURE, CLIP_FRAMES);

        /* The control FIRST, and it is the shipped default on a device with no
         * sound files: nothing configured, so the notes must run. */
        check(!audio_fx_play(&a, AUDIO_FX_FAIL),
              "no clip configured — audio_fx_play() says no, quietly");
        audio_beep(&a);
        check(a.mix.v[0].active && a.mix.v[0].kind == AUDIO_VOICE_TONE,
              "so audio_beep() put a TONE on the bus, not silence");

        audio_fx_set_path(&a, AUDIO_FX_FAIL, CLIP_FIXTURE);
        check(audio_fx_play(&a, AUDIO_FX_FAIL), "a configured clip plays");
        check(a.fx[AUDIO_FX_FAIL].frames == CLIP_FRAMES,
              "and the whole file is in RAM, at its real length");

        /* THE point of RAM-resident clips.  The one streaming sfx voice refuses a
         * second tap because its AudioWav is the live voice's ctx; a clip has a
         * cursor per trigger, so a game firing hits in bursts gets all of them. */
        check(audio_fx_play(&a, AUDIO_FX_FAIL),
              "a SECOND trigger is accepted while the first still sounds");
        check(audio_pump_voices(&a) == 3, "so three voices are on the bus");

        /* ⚠️ Two triggers in the SAME frame sit at the same offset, which is worth
         * pinning rather than leaving to be discovered: identical PCM at identical
         * offsets sums COHERENTLY, so the same effect fired twice in one frame is
         * 2x the amplitude of one — unlike two tones, which partially cancel
         * because AudioVoice.delay guarantees they start at different moments. */
        render_frames(&a, 1000);
        check(a.fxv[0].pos == a.fxv[1].pos,
              "two triggers in one frame advance in lockstep (a coherent 2x sum)");

        /* Independence is what the cursor buys, and it shows up the moment the
         * triggers are a frame apart — which is every real burst of brick hits. */
        check(audio_fx_play(&a, AUDIO_FX_FAIL), "a third trigger, one render later");
        render_frames(&a, 1000);
        check(a.fxv[0].pos > a.fxv[2].pos,
              "the triggers carry INDEPENDENT positions (the cursor)");
        check(a.fxv[2].pos > 0 && a.fxv[2].pos < CLIP_FRAMES,
              "and the later one started at 0 rather than where the first was");
        close(fd);
    }

    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        audio_fx_set_path(&a, AUDIO_FX_BEEP, CLIP_FIXTURE);
        audio_beep(&a);
        check(a.mix.v[0].active && a.mix.v[0].kind == AUDIO_VOICE_SAMPLE,
              "audio_beep() with a clip configured plays the CLIP, not the tone");

        /* A voice that finished must REWIND on reuse, not resume: the pool hands
         * the same AudioClipVoice back out and its cursor is at end-of-clip. */
        render_frames(&a, CLIP_FRAMES + 8000);
        check(audio_pump_voices(&a) == 0, "the clip ended and the voice freed itself");
        audio_beep(&a);
        render_frames(&a, 500);
        check(a.fxv[0].pos > 0 && a.fxv[0].pos <= 2048,
              "a reused voice REWOUND — it did not resume at end-of-clip");
        close(fd);
    }

    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        audio_fx_set_path(&a, AUDIO_FX_BLIP, CLIP_FIXTURE);
        for (int i = 0; i < AUDIO_CLIP_VOICES; i++)
            check(audio_fx_play(&a, AUDIO_FX_BLIP), "a clip voice was free");
        check(!audio_fx_play(&a, AUDIO_FX_BLIP),
              "and the pool is bounded — the AUDIO_CLIP_VOICES+1'th says no");
        audio_blip(&a);
        check(a.mix.v[AUDIO_CLIP_VOICES].kind == AUDIO_VOICE_TONE,
              "so audio_blip() fell back to its note table rather than going silent");
        close(fd);
    }

    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        /* A MISSING file is the normal case (the sound files are device-only,
         * ../IMPROVEMENT_PLAN.md F19) and the refusal is permanent for the
         * process: a per-trigger retry would fill app_stdout.log.  Creating the
         * file afterwards is the observable form of "it did not try again". */
        remove(CLIP_ABSENT);
        audio_fx_set_path(&a, AUDIO_FX_FAIL, CLIP_ABSENT);
        check(!audio_fx_play(&a, AUDIO_FX_FAIL), "a missing clip file is refused");
        write_bed_fixture(CLIP_ABSENT, CLIP_FRAMES);
        check(!audio_fx_play(&a, AUDIO_FX_FAIL),
              "and the refusal is PERMANENT — the file appearing changes nothing");
        remove(CLIP_ABSENT);

        /* Same refusal as the bed's, and for the same reason: no resampler. */
        audio_fx_set_path(&a, AUDIO_FX_BEEP, CLIP_FIXTURE);
        a.sample_rate = TEST_RATE / 2;
        check(!audio_fx_play(&a, AUDIO_FX_BEEP),
              "a clip at the wrong rate is refused, not pitch-shifted");
        a.sample_rate = TEST_RATE;

        /* The guard that stops a MUSIC path being RAM-loaded inside a tap. */
        write_bed_fixture(CLIP_TOO_BIG, AUDIO_CLIP_MAX_FRAMES + 1);
        audio_fx_set_path(&a, AUDIO_FX_SUCCESS, CLIP_TOO_BIG);
        check(!audio_fx_play(&a, AUDIO_FX_SUCCESS),
              "a file past the clip ceiling is refused — a bed STREAMS");
        remove(CLIP_TOO_BIG);
        close(fd);
    }

    {
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        write_bed_fixture(CLIP_FIXTURE, CLIP_FRAMES);
        write_bed_fixture(CLIP_FIXTURE2, CLIP_FRAMES / 2);
        audio_fx_set_path(&a, AUDIO_FX_FAIL, CLIP_FIXTURE);
        check(audio_fx_play(&a, AUDIO_FX_FAIL), "a clip is sounding");

        /* ④ swaps a path at runtime.  The old PCM cannot be freed while a voice
         * reads it, so the free waits for the next trigger — and if the old clip
         * is still sounding then, that one trigger takes the note table. */
        audio_fx_set_path(&a, AUDIO_FX_FAIL, CLIP_FIXTURE2);
        check(!audio_fx_play(&a, AUDIO_FX_FAIL),
              "a swap while the old clip sounds falls back for that one trigger");
        render_frames(&a, CLIP_FRAMES + 8000);
        check(audio_fx_play(&a, AUDIO_FX_FAIL), "and takes effect once it has ended");
        check(a.fx[AUDIO_FX_FAIL].frames == CLIP_FRAMES / 2,
              "with the NEW file's length, so the old PCM really was replaced");

        /* Setting the path already set must not invalidate anything — this is
         * called from a game's config read, which may run per frame. */
        long before = a.fx[AUDIO_FX_FAIL].frames;
        audio_fx_set_path(&a, AUDIO_FX_FAIL, CLIP_FIXTURE2);
        check(!a.fx[AUDIO_FX_FAIL].reload && a.fx[AUDIO_FX_FAIL].frames == before,
              "re-setting the same path is a no-op, so it is safe every frame");

        /* Off the bus a sample voice cannot exist, which is exactly when the
         * notes must run — and it must be a quiet no, not a refusal message. */
        a.pumping = false;
        check(!audio_fx_play(&a, AUDIO_FX_FAIL), "off the bus, the clip says no");
        a.pumping = true;
        close(fd);
    }

    {
        /* The CONTENT check.  Everything above reads a position, and a fill that
         * replays the clip's head while advancing the cursor satisfies every one
         * of them — so this is the only assertion that would catch it. */
        fd = mk_audio(&a);
        if (fd < 0) return 1;
        write_tail_fixture(CLIP_TAIL, CLIP_FRAMES);
        audio_fx_set_path(&a, AUDIO_FX_FAIL, CLIP_TAIL);
        check(audio_fx_play(&a, AUDIO_FX_FAIL), "a silent-headed clip plays");

        int head = render_peak(&a, (CLIP_FRAMES * 3) / 4 - 1024);
        int tail = render_peak(&a, CLIP_FRAMES);
        check(head < 400, "its silent head renders as silence (peak within rounding)");
        check(tail > 2000, "and its loud TAIL renders loud — the cursor really moved");
        check(tail > head + 1500, "which is a content difference, not a level one");
        close(fd);
    }

    printf("\n%s  %d checks, %d failure(s)\n",
           failures ? "FAILED" : "PASSED", checks, failures);
    return failures ? 1 : 0;
}
