/*
 * audio_mix_test — the mix bus, driven by hand
 *
 * F1 Phase 3 adds real mixing to `common/audio.c` through an optional per-frame
 * `audio_pump()`.  Everything about it that is arithmetic is host-tested
 * (`tests/audio_gen_test.c`, groups I/J/K).  What no host can answer is whether
 * two sounds at once are AUDIBLE as two sounds on a 20 mm speaker that sums
 * L + R — and whether the ~60 ms minimum-tone rule survives a stream that is
 * never reset.  Both need an ear at the panel, so this is the tool for that trip
 * (../IMPROVEMENT_PLAN.md panel items 12 and 14).
 *
 * ⚠️ **CONT is the outer toggle, and it is the negative control for the CLICK.**
 * With it OFF every button takes the per-sound-reset path — `audio_flush()`,
 * SNDCTL_DSP_RESET before every sound, one sound at a time — which is exactly F1
 * defect 3's *"every time there is a sound, there is a click"*.  With it ON the
 * device is `common/audio_out.c`'s one never-reset stream instead.  So the control
 * is on the same panel, in the same session, one tap away: **a click that survives
 * CONT: ON is not the click this change removes.**
 *
 * Two things CONT does to the row beside it, both deliberate and both labelled:
 *
 *   - **PUMP reads LOCK, not ON.**  A continuous stream needs a writer every
 *     service, so CONT implies PUMP and the library REFUSES
 *     `audio_pump_enable(false)` while it is on — loudly, silencing the voices
 *     without giving the stream away.  Tapping PUMP under CONT triggers exactly
 *     that refusal, which is the only place it can be seen.  The pad also remembers
 *     the operator's own PUMP position and restores it when CONT goes off, or the
 *     A/B afterwards would compare PUMP: ON against PUMP: ON.
 *   - **KEEP reads n/a.**  Not refused — *unread*: the continuous stream writes
 *     every service, silence included, so `keepalive` has nothing left to decide.
 *     A dead toggle that still says OFF is worse than one that says why.
 *
 * ⚠️ **PUMP is a toggle too, and it is the control for MIXING** rather than for the
 * click.  With CONT and PUMP both off every button takes the one-sound-at-a-time
 * path: if DRONE + HIGH sounds like two tones with PUMP ON and like one with PUMP
 * OFF, mixing works and nothing else explains it.
 *
 * What each row is for:
 *
 *   toggles    CONT / PUMP / KEEP / LIM / STOP ALL.  STOP ALL is `audio_interrupt()`,
 *              which on either bus means "silence every voice" — note it cannot
 *              un-write what is already inside the device (≤80 ms on the pump, one
 *              lead, ~139 ms, on the continuous stream).
 *              ⚠️ **LIMIT is the second negative control, added after the first
 *              panel session.** With `clip` at 15402 the operator heard mixed
 *              sounds as *"a distorted square wave from an overdriven
 *              amplifier"* — three voices at `AUDIO_PEAK` sum to 54000 against
 *              int16's 32767.  `LIMIT: SOFT` is the fix (a knee at one voice's
 *              peak, asymptotic to `AUDIO_MIX_CEIL`); `LIMIT: HARD` restores the
 *              rejected clamp so the difference can be heard rather than argued.
 *   tones      DRONE is 3 s at 220 Hz, `440 3s` is 3 s at 440 Hz, and the other
 *              three are 200 ms at 440 / 880 / 1760 Hz.  Tap a long one, then tap
 *              the others while it runs.  The pitches are far apart on purpose:
 *              "one tone or two" must not depend on the listener keeping count.
 *              ⚠️ **The two 3 s pads exist for a TIMBRE question, which a 200 ms
 *              blip cannot answer.** Defect 3's decisive test is whether ONE voice
 *              at `AUDIO_PEAK` sounds like a clean sine — the operator compared the
 *              device against a phone signal generator and heard a square wave —
 *              and that comparison needs a tone that sustains.  `440 3s` alone is
 *              one voice through a limiter whose knee is `AUDIO_PEAK` exactly, so
 *              it is byte-identical to the unlimited path: if it still sounds
 *              square, **the limiter is exonerated** and the fault is upstream of
 *              the mix entirely.  `440 3s` + DRONE is the same question for a
 *              two-voice sum, sustained long enough to compare.
 *   canned     the four sounds every game uses, unchanged signatures.  SUCCESS
 *              and FAIL are three notes each, and on the pump they are three
 *              voices with start offsets — if either sounds like a CHORD rather
 *              than an arpeggio, the offsets are broken.  CHORD deliberately
 *              plays three notes together, so there is something to compare to.
 *   ms row     the ~60 ms rule.  Same 880 Hz tone at 5 / 10 / 20 / 40 / 60 /
 *              100 ms.  Walk up the row and note the shortest one you can hear,
 *              once per configuration: CONT OFF + PUMP OFF, then CONT OFF + PUMP ON,
 *              then CONT ON.  Three numbers, and the rule is whichever of them still
 *              holds.  ⚠️ The rule is attributed to DAC start-up under the
 *              per-sound reset, so **CONT ON is the configuration that should
 *              abolish it** — a continuous feed has no start-up to wait for, and F1
 *              expects the floor to drop to ~5 ms.  Each stimulus is chosen by the
 *              operator, so it is self-identifying by construction — no marker
 *              clicks needed.
 *   sample row `WAV 1` / `WAV 2` / `W STOP` / `SFX` / `INH 622` — F1 Phase 8, and
 *              **this row is the experiment the phase exists for.** Four causes of
 *              the two-voice harshness are refuted and the survivor is this speaker
 *              on sustained pure sine PAIRS (../IMPROVEMENT_PLAN.md F1). Every other
 *              pad here is a synthesised sine, so nothing above this row can test
 *              that. Tap `WAV 1`, let the bed settle, then tap `SFX` over it: same
 *              bus, same limiter, same delivery, different waveform. If that is
 *              clean while `440 3s` + `880` is not, the answer is "effects should be
 *              samples" and the question closes as a product decision.
 *              ⚠️ **`W STOP` is not the toggle row's `STOP`** — it releases only the
 *              bed, so an effect over it keeps sounding.
 *              ⚠️ **`INH 622` is here because every tone pad above is an OCTAVE of
 *              every other** (220/440/880/1760), so the tool could not make an
 *              inharmonic pair and `CHORD` was the only substitute. 622 against 440
 *              is within 0.03 % of √2 — the tritone, minimum harmonic coincidence.
 *              ⚠️ **The bed needs the bus**: `audio_music_start()` refuses loudly
 *              with PUMP off, because a sample voice only exists on the mix bus.
 *              And the two music files are hand-copied, not in the repo (F19), so
 *              "cannot open" is a deployment fact rather than a code fault.
 *
 * ⚠️ **Two of this tool's INSTRUMENTS were lying and both are fixed (2026-08-20).
 * The first invalidated a recorded refutation, which is the expensive kind:**
 *
 *   - **`CHORD` was an arpeggio.** Three back-to-back `audio_tone()` calls are
 *     consecutive statements, so `AUDIO_TONE_CHAIN_MS`'s recency gate read them as
 *     one motif and chained each behind the last. F1 carries "HARD vs SOFT was
 *     inaudible, therefore clipping is refuted" — and that A/B was judged with this
 *     pad, which never put two voices on the bus at once. It is now three
 *     `audio_mix_add()` calls at delay 0 when the bus is on, with the queueing
 *     `audio_tone()` path kept for OFF the bus, where the difference is the point.
 *   - **The limiter label printed the tool's own variable.** `audio_mix_init()` sets
 *     `AUDIO_MIX_HARD` and the tool's bool started false, so the pad read `LIM:
 *     SOFT` over a HARD bus, the log agreed with it, and the first tap "turned SOFT
 *     on" by setting HARD. There is no local copy any more: the pad, the log and
 *     the toggle all read `audio_mix_get_limit()`. **An A/B tool must read its own
 *     toggles back from the library.**
 *
 * ⚠️ **The five row offsets are DERIVED from `SCREEN_SAFE_HEIGHT`, not written
 * down** — `rows_layout()`, whose comment carries what a heavily-inset panel pays
 * the shortfall with and why. A fifth row is what made that necessary: the agreed
 * offsets end at 406 and at the 48/48 inset cap the safe height is 384.
 *
 * The readout shows live voices and five counters: `clip` (samples the int16
 * store could not hold — ⚠️ **must be 0 with LIMIT: SOFT**, that is the check
 * that the limiter is engaged), `lim` (samples the soft knee bent — expected to
 * be large, not a fault), `starve` (pumps that found the ring dry with audio
 * still owed — **each one is an audible gap, and it attributes crackle to PACING
 * rather than to mixing**), `lost` (frames the device refused after the voices had
 * already advanced past them) and `drop` (sounds refused by a full bus).
 *
 * ⚠️ **`clip == 0` is NOT evidence of a clean mix** — it proves int16 did not
 * overflow, and the soft limiter guarantees that by construction.  The limiter
 * waveshapes the sum instead, which IS harmonic distortion, and this tool once
 * read PASS on every counter while the operator heard every sum as "a big
 * distortion" (../IMPROVEMENT_PLAN.md F1 defect 3).  `lim` is the number to read
 * for that: large `lim` means the sum is being bent, whatever `clip` says.
 *
 * ⚠️ **Two of this tool's own numbers were WRONG until 2026-08-16, and both made
 * a panel session harder to trust than the code it was judging:**
 *
 *   - **`lead` was `AUDIO_PUMP_LEAD_MS`, a constant.**  The library floors the
 *     lead at whole device periods, so the panel displayed 80 ms while the pump
 *     held ~139 — and the worst-frame warning was coloured against the same wrong
 *     number.  It now reads `audio_pump_lead()`/`audio_pump_period()` and prints
 *     the arithmetic (`139 ms (3x46)`), or `lead ?` before the first pump has
 *     measured anything.  **An A/B tool must report what the library did.**
 *   - **Every tap went to `stderr` with no `fopen` anywhere**, so from the
 *     launcher tile the log went nowhere at all and the claim that taps were
 *     recorded was false.  `main()` now `freopen()`s `stderr` onto
 *     `MIX_LOG_PATH` — which captures `audio_pump()`'s own bounded ring trace in
 *     the same file, in order, with no library change.
 *
 * `worst frame` is also per-PUMP-session: toggling PUMP resets it alongside the
 * library counters, because a 2474 ms first frame at boot contention is not a
 * property of the mix bus and must not sit on the panel looking like one.
 *
 * CPU is the other open question (mixing on a 600 MHz core with no FPU-friendly
 * sin()).  Measure it from another shell while sound is playing:
 *   ssh root@<ip> "top -b -n 2 | grep audio_mix_test"
 *
 * Run on device (the log needs no redirect — the tool opens it itself):
 *   /opt/games/audio_mix_test /dev/fb0 /dev/input/touchscreen0
 *   ssh root@<ip> cat /tmp/mix.log
 *
 * Build (from native_apps/).  ⚠️ `common/audio_out.c` is not optional: `audio.h`
 * includes `audio_out.h` and every `Audio` embeds an `AudioOut`, so the link fails
 * without it — which is the right failure.  `build-and-deploy.sh` gets it from
 * `$COMMON_OBJ`; this line is for building the tool by hand.
 *   arm-linux-gnueabihf-gcc -O2 -static -I. tests/audio_mix_test.c \
 *     common/audio.c common/audio_gen.c common/audio_out.c common/audio_wav.c \
 *     common/touch_input.c \
 *     common/framebuffer.c common/hardware.c common/common.c \
 *     common/config.c common/highscore.c common/keyboard.c \
 *     -o build/audio_mix_test -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>

#include "../common/audio.h"
#include "../common/audio_gen.h"
#include "../common/touch_input.h"
#include "../common/framebuffer.h"
#include "../common/hardware.h"
#include "../common/common.h"

static volatile bool running = true;
static void sig_handler(int s) { (void)s; running = false; }

/** Where the tap log and `audio_pump()`'s ring trace both land.
 *
 * ⚠️ **`stderr` was not a log.**  This tool is normally started from the launcher
 * tile, whose child inherits an init script's stderr and therefore throws it away;
 * there was no `fopen` anywhere in the file, so "every tap is logged" was false for
 * every session that mattered.  Redirecting the STREAM rather than opening a
 * private `FILE *` is deliberate: `audio.c`'s bounded per-pump trace also writes
 * `stderr`, and this way the taps and the ring numbers interleave in one file, in
 * order, with no change to the library. */
#define MIX_LOG_PATH  "/tmp/mix.log"

/** ⚠️ freopen() CLOSES the stream before it opens the target, so a failure leaves
 *  `stderr` closed — writes then vanish silently rather than crashing.  Say so on
 *  stdout, which an SSH launch can still see, instead of assuming /tmp is
 *  writable. */
static void open_log(void)
{
    if (freopen(MIX_LOG_PATH, "a", stderr)) {
        setvbuf(stderr, NULL, _IONBF, 0);   /* a session that ends in SIGKILL must
                                             * still have its lines on disk */
        fprintf(stderr, "\n=== audio_mix_test session start ===\n");
    } else {
        printf("audio_mix_test: cannot open %s — taps will not be logged\n",
               MIX_LOG_PATH);
    }
}

/* ── pads ────────────────────────────────────────────────────────────────── */

typedef enum {
    ACT_CONT, ACT_PUMP, ACT_KEEPALIVE, ACT_LIMIT, ACT_LEVEL, ACT_STOP,
    ACT_TONE,                       /* uses freq/ms */
    ACT_BEEP, ACT_BLIP, ACT_SUCCESS, ACT_FAIL, ACT_CHORD,
    ACT_MUSIC,                      /* uses `path` — the looping bed          */
    ACT_MUSIC_STOP,
    ACT_SFX                         /* uses `path` — one recorded effect      */
} Action;

/* ── the sample material, and why these files ────────────────────────────────
 *
 * All five measured on `.188` 2026-08-20: **mono / 44100 / 16-bit**, which is
 * what `hw:0,0` grants, so nothing here is resampled or downmixed and a rate
 * refusal in the log means something changed rather than something is unsupported.
 *
 * ⚠️ **The two music files are hand-copied and are not in the repo** — they
 * survive `device-files/clean-rules.conf`'s wholesale keep of `/opt/sound` but not
 * a fresh card (../IMPROVEMENT_PLAN.md F19). If a pad refuses with "cannot open",
 * that is the first thing to check, not a code fault.
 *
 * ⚠️ **`asl_success.wav` is the pad that answers phase 8's actual question.**
 * Everything else on this panel is a synthesised sine, and the surviving
 * hypothesis for the two-voice harshness is this speaker on sustained pure sine
 * PAIRS (../IMPROVEMENT_PLAN.md F1 — four other causes are refuted). Sampled
 * material over sampled material shares the bus, the limiter and the delivery
 * with the sine case and differs only in the waveform, so if SFX-over-bed is
 * clean the answer is "effects should be samples" and the question closes.
 */
#define MUSIC1_PATH  "/opt/sound/officerunner1-mono.wav"
#define MUSIC2_PATH  "/opt/sound/officerunner2-mono.wav"
#define SFX_PATH     "/opt/sound/asl_success.wav"

/* ── the level ladder ─────────────────────────────────────────────────────────
 *
 * ⚠️ **Quietest FIRST, and the tool starts on the quietest rung rather than on the
 * shipped default.**  Every level ladder this project has walked so far ran
 * loud-to-quiet, and that direction biases adaptation: after a distorted rung a
 * clean one sounds *quiet*, and after a clean one a distorted rung sounds *loud*.
 * Starting at the bottom makes the honest direction the only one the pad offers.
 *
 * ⚠️ **Two SEPARATE questions per rung — "can you hear it?" and "is it clean?"**
 * Conflating them has already cost a session: *"all work"* was read as *"all
 * clean"* and a whole discriminator was built on it (`tests/CLAUDE.md`).
 *
 * The volume is the only thing that moves.  The master shift stays at ScummVM's
 * `>>1` — it is the DEVICE stage, one per speaker, and moving both at once would
 * make the walk a two-variable comparison.  The acoustic peak in the label is
 * `peak >> shift`, i.e. what actually reaches the amplifier.
 */
typedef struct { int vol; const char *note; } LevelRung;
static const LevelRung ladder[] = {
    {  24, "quietest"  },
    {  48, ""          },
    {  96, "default"   },   /* AUDIO_VOICE_VOL — one voice at the measured-clean 6144 */
    { 144, ""          },
    { 192, "ScummVM"   },   /* MixerImpl's own default arithmetic, then >>1 */
    { 256, "full"      },
};
#define LADDER_RUNGS ((int)(sizeof(ladder) / sizeof(ladder[0])))

typedef struct {
    Button      btn;
    Action      act;
    int         freq;
    int         ms;
    const char *path;               /* ACT_MUSIC / ACT_SFX only */
} Pad;

/** The pad table's size, and it is the EXACT count in use: 6 toggles + 5 tones +
 *  5 canned + 6 ms + 5 sample = 27.
 *
 * ⚠️ **This was 26 with 22 used, and adding a five-pad row silently produced a
 * row that did not exist** — `pad_add()` returned NULL past the cap and said
 * nothing, so the buttons were simply absent and nothing in the code looked
 * wrong. It is exact rather than padded on purpose: a spare slot restores the
 * silence for the next row. If you add pads, raise this AND check the log line
 * below, which is the only thing that can tell you the cap was hit. */
#define MAX_PADS 27
static Pad  pads[MAX_PADS];
static int  pad_count = 0;

static Pad *pad_add(Action act, const char *label, int x, int y, int w, int h,
                    uint32_t colour, int scale)
{
    if (pad_count >= MAX_PADS) {
        /* ⚠️ Loud, because the silent version is indistinguishable from a layout
         * bug: the pad is not drawn, not hit-tested and not in `pad_count`. */
        fprintf(stderr, "audio_mix_test: MAX_PADS (%d) EXCEEDED — the pad \"%s\" "
                        "does not exist and cannot be tapped\n", MAX_PADS, label);
        return NULL;
    }
    Pad *p = &pads[pad_count++];
    memset(p, 0, sizeof(*p));
    p->act = act;
    button_init_full(&p->btn, x, y, w, h, label,
                     colour, COLOR_WHITE, BTN_HIGHLIGHT_COLOR, scale);
    return p;
}

/* Lay `count` pads out evenly across the touch-safe width.  Buttons are
 * hit-tested, so they belong in SCREEN_SAFE_*, never in the visible band. */
static void row_geom(int count, int index, int gap, int *x, int *w)
{
    int total = SCREEN_SAFE_WIDTH - 2 * gap;
    int cell  = total / count;
    *x = SCREEN_SAFE_LEFT + gap + index * cell;
    *w = cell - gap;
}

/* ── the vertical layout, DERIVED ────────────────────────────────────────────
 *
 * ⚠️ **The five row offsets are computed from `SCREEN_SAFE_HEIGHT`, not written
 * down, and that is what makes a fifth row safe on a unit nobody has swept.**
 * The agreed target — rows at 72/136/200/274/348 with heights 54/54/64/64/58 and
 * gaps of 10 — ends at 406 and needs a safe height of ~410. `.188` has it
 * (`reach 0 4082 0 4095`, i.e. its digitizer reaches the hardware limit
 * vertically, so ~418), but at `FB_TOUCH_INSET_MAX` on both edges
 * `SCREEN_SAFE_HEIGHT` is only **384** and the same offsets overflow the
 * touchable rectangle by 22 px — a bottom row that is drawn and cannot be
 * pressed, which is precisely the failure the two-rectangle split exists to
 * prevent (../CLAUDE.md → *Screen edges*).
 *
 * So the shortfall is paid for in a fixed order, and the order is a judgement
 * about tapping rather than about pixels:
 *
 *   1. **gaps first, down to GAP_MIN** — whitespace between rows costs nothing to
 *      lose, and a pad that shrank is measurably harder to hit than one that moved.
 *   2. **then the TALLEST row, one pixel at a time** — so the 64 px rows give
 *      before the 54 px ones and the row heights converge rather than one row
 *      collapsing.
 *   3. **never below ROW_H_MIN**, at which point it gives up and lets the bottom
 *      row sit outside the safe area. ⚠️ That is a real outcome, not a
 *      theoretical one, and it is REPORTED (`rows: …`) on the log's first line
 *      rather than silently rendered — a bottom row 6 px outside the touchable
 *      band looks completely normal on a screenshot.
 */
#define ROWS_TOP       72   /* below the voice meter at SCREEN_SAFE_TOP+56, h 8 */
#define ROWS_BOTTOM_M   4   /* keep the last row off the very last safe pixel   */
#define ROW_COUNT       5
#define ROW_GAP_NOM    10
#define ROW_GAP_MIN     4
#define ROW_H_MIN      44   /* ~60x40 is the comfortable minimum (../CLAUDE.md) */

static const int row_nom_h[ROW_COUNT] = { 54, 54, 64, 64, 58 };
static int row_y[ROW_COUNT];
static int row_h[ROW_COUNT];
static int row_gap;

static void rows_layout(void)
{
    int nom = 0;
    for (int i = 0; i < ROW_COUNT; i++) { row_h[i] = row_nom_h[i]; nom += row_nom_h[i]; }

    int avail = SCREEN_SAFE_HEIGHT - ROWS_TOP - ROWS_BOTTOM_M;

    row_gap = ROW_GAP_NOM;
    while (row_gap > ROW_GAP_MIN && nom + row_gap * (ROW_COUNT - 1) > avail) row_gap--;

    int over = nom + row_gap * (ROW_COUNT - 1) - avail;
    while (over > 0) {
        int tallest = 0;
        for (int i = 1; i < ROW_COUNT; i++) if (row_h[i] > row_h[tallest]) tallest = i;
        if (row_h[tallest] <= ROW_H_MIN) break;      /* nothing left to give */
        row_h[tallest]--;
        over--;
    }

    int y = SCREEN_SAFE_TOP + ROWS_TOP;
    for (int i = 0; i < ROW_COUNT; i++) { row_y[i] = y; y += row_h[i] + row_gap; }

    /* The receipt.  `over` is the pixels the safe area could NOT absorb, so a
     * non-zero value means the bottom row is drawable and not pressable. */
    fprintf(stderr, "mix: rows safe_h=%d avail=%d gap=%d h=%d/%d/%d/%d/%d "
                    "y=%d/%d/%d/%d/%d bottom=%d over=%d\n",
            SCREEN_SAFE_HEIGHT, avail, row_gap,
            row_h[0], row_h[1], row_h[2], row_h[3], row_h[4],
            row_y[0] - SCREEN_SAFE_TOP, row_y[1] - SCREEN_SAFE_TOP,
            row_y[2] - SCREEN_SAFE_TOP, row_y[3] - SCREEN_SAFE_TOP,
            row_y[4] - SCREEN_SAFE_TOP,
            row_y[4] + row_h[4] - SCREEN_SAFE_TOP, over);
    if (over > 0)
        fprintf(stderr, "mix: ⚠ row 5 overflows the touch-safe area by %d px — "
                        "it is drawn but may not be pressable\n", over);
}

/* ── state ───────────────────────────────────────────────────────────────── */

typedef struct {
    bool cont;              /* CONT toggle: the one never-reset stream owns /dev/dsp */
    bool pump_before_cont;  /* ⚠️ CONT forces PUMP ON, so the operator's own PUMP
                             * position has to be remembered and put back on the way
                             * out — otherwise turning CONT off strands the panel in
                             * a state nobody chose, and the A/B compares the wrong
                             * two things.                                          */
    bool pump;
    bool keepalive;
    /* ⚠️ **There is no `hard` field here any more, and that is the fix.** The
     * limiter position was a tool-local bool printed as if it were a
     * measurement: `audio_mix_init()` sets AUDIO_MIX_HARD while a zeroed bool
     * reads SOFT, so both the pad and `/tmp/mix.log` said "SOFT" over a HARD bus
     * for a whole panel session (measured 2026-08-19). Everything now reads
     * `audio_mix_get_limit()`. Do not reintroduce a mirror of a library state
     * that has a getter. */
    int  rung;              /* index into `ladder` — the LEVEL under test.  Starts
                             * at 0, the quietest, so the walk runs upwards       */
    int  voices;
    uint32_t clipped;
    uint32_t limited;
    uint32_t starved;
    uint32_t lost;
    uint32_t dropped;
    bool music_on;          /* the bed still owes frames — asked of the library  */
    long music_loops;       /* times it has WRAPPED, which is the number the loop
                             * seam is judged against: "I heard the join" is only
                             * actionable beside a wrap count (F19)              */
    long lead_frames;       /* what the LIBRARY targeted, 0 = not measured yet */
    long period_frames;     /* the device period it was rounded up to           */
    uint32_t max_gap;       /* longest gap between two loop iterations, ms —
                             * reset with the library counters on PUMP: ON      */
    char last[40];
} View;

/** How often the counter line may force a full redraw.  ⚠️ Not cosmetic: with
 *  the soft limiter engaged `limited` increments on most samples, so a readout
 *  that redraws on every change redraws EVERY FRAME — and a full 800x450x4
 *  repaint plus fb_swap on this part is a plausible cause of the very starvation
 *  this tool is trying to attribute.  The voice count still redraws instantly. */
#define READOUT_MS  250

static void set_toggle_labels(Pad *cont_pad, Pad *pump_pad, Pad *keep_pad,
                              Pad *limit_pad, Pad *level_pad, const Audio *audio,
                              const View *v)
{
    char t[32];

    /* CONT is the outer switch and reads first.  It is drawn as INFO rather than
     * PRIMARY so the row shows at a glance which of the two device halves is open —
     * a verdict about the click is worthless without that, and the first report from
     * this tool could not be diagnosed because PUMP's position was recalled rather
     * than recorded. */
    snprintf(t, sizeof(t), "CONT: %s", v->cont ? "ON" : "OFF");
    button_set_text(&cont_pad->btn, t);
    button_set_colors(&cont_pad->btn,
                      v->cont ? BTN_COLOR_INFO : BTN_COLOR_SECONDARY,
                      COLOR_WHITE, BTN_HIGHLIGHT_COLOR);

    /* ⚠️ Under CONT the label is LOCK, not ON.  PUMP is not a choice there — the
     * library refuses audio_pump_enable(false) while the continuous stream is open,
     * because a stream nobody writes goes idle and an idle stream is the transition
     * this whole change exists to remove.  A pad reading "ON" invites a tap that
     * cannot do what it says. */
    snprintf(t, sizeof(t), "PUMP: %s", v->cont ? "LOCK" : (v->pump ? "ON" : "OFF"));
    button_set_text(&pump_pad->btn, t);
    button_set_colors(&pump_pad->btn,
                      v->cont ? BTN_COLOR_INFO
                              : (v->pump ? BTN_COLOR_PRIMARY : BTN_COLOR_SECONDARY),
                      COLOR_WHITE, BTN_HIGHLIGHT_COLOR);

    /* ⚠️ And KEEP is MEANINGLESS under CONT, not merely ignored: the continuous
     * stream is continuous by construction — every service writes, silence
     * included — so `keepalive` has nothing left to decide and audio_pump() never
     * reads it.  Say "n/a" rather than leave a dead toggle that looks live. */
    snprintf(t, sizeof(t), "KEEP: %s",
             v->cont ? "n/a" : (v->keepalive ? "ON" : "OFF"));
    button_set_text(&keep_pad->btn, t);
    button_set_colors(&keep_pad->btn,
                      v->cont ? BTN_COLOR_SECONDARY
                              : (v->keepalive ? BTN_COLOR_INFO : BTN_COLOR_SECONDARY),
                      COLOR_WHITE, BTN_HIGHLIGHT_COLOR);

    /* HARD is drawn as a WARNING, because it is the state the panel rejected.
     * Abbreviated because the row is five pads wide now: at the 48 px inset cap a
     * cell is 132 px, which "LIMIT: SOFT" fills exactly.  Nothing is lost — the log
     * line carries `limit=soft|hard` as its own field.
     *
     * ⚠️ **Read back from the LIBRARY, never from a tool flag.** This label said
     * SOFT over a HARD bus for a whole session, because `audio_mix_init()` sets
     * HARD and the tool's own bool started false. `audio_mix_get_limit()` exists
     * for exactly this; an A/B tool that prints its own intention is not an
     * instrument. */
    bool hard = (audio_mix_get_limit(&audio->mix) == AUDIO_MIX_HARD);
    snprintf(t, sizeof(t), "LIM: %s", hard ? "HARD" : "SOFT");
    button_set_text(&limit_pad->btn, t);
    button_set_colors(&limit_pad->btn,
                      hard ? BTN_COLOR_DANGER : BTN_COLOR_PRIMARY,
                      COLOR_WHITE, BTN_HIGHLIGHT_COLOR);

    /* ⚠️ The rung number comes from the tool, but the VOLUME comes from the
     * library — `audio_get_volume()`, not `ladder[rung].vol`.  A pad that printed
     * its own intention would have shown a level the library had clamped or
     * refused, and this file has already paid for a label that disagreed with the
     * device (the PUMP position that could not be diagnosed).  Six pads in the row
     * now, so the text stays inside a 112 px cell at the 48 px inset cap. */
    snprintf(t, sizeof(t), "LVL %d/%d", v->rung + 1, LADDER_RUNGS);
    button_set_text(&level_pad->btn, t);
    button_set_colors(&level_pad->btn,
                      (audio_get_volume(audio) >= AUDIO_VOICE_VOL) ? BTN_COLOR_DANGER
                                                                   : BTN_COLOR_INFO,
                      COLOR_WHITE, BTN_HIGHLIGHT_COLOR);
}

static void draw_screen(Framebuffer *fb, Button *exit_btn, const View *v,
                        int rate)
{
    fb_clear(fb, RGB(10, 12, 20));

    /* Title and the two readout lines live in the visible band above the pads:
     * they are read, never pressed.  INFO_X clears "MIX BUS" at scale 3 —
     * 7 chars x 6 px x 3 plus the left margin — measured rather than guessed,
     * because the first version overlapped and printed "4100 Hz". */
    fb_draw_text(fb, SCREEN_SAFE_LEFT + 4, SCREEN_VISIBLE_TOP + 4,
                 "MIX BUS", RGB(255, 200, 80), 3);
    int info_x = SCREEN_SAFE_LEFT + 8 + text_measure_width("MIX BUS", 3) + 12;

    /* ⚠️ The lead comes off the LIBRARY and carries its arithmetic with it, and
     * "not measured yet" is printed as such rather than as the header's request —
     * the two are different claims and only one of them is a measurement. */
    char lead[40];
    long lead_ms = audio_ms_for_frames(rate, v->lead_frames);
    if (v->lead_frames > 0)
        snprintf(lead, sizeof(lead), "lead %ld ms (%dx%ld)", lead_ms,
                 AUDIO_PUMP_LEAD_PERIODS,
                 audio_ms_for_frames(rate, v->period_frames));
    else
        snprintf(lead, sizeof(lead), "lead ? (pump has not measured)");

    char line[144];
    snprintf(line, sizeof(line), "%d Hz  %s  %d voices  worst frame %lu ms",
             rate, lead, AUDIO_MAX_VOICES, (unsigned long)v->max_gap);
    fb_draw_text(fb, info_x, SCREEN_VISIBLE_TOP + 6,
                 line, (lead_ms > 0 && v->max_gap > (uint32_t)lead_ms)
                       ? RGB(255, 180, 60) : RGB(130, 140, 160), 1);

    /* ⚠️ `bed` carries the WRAP COUNT, not just on/off.  F19's cheapest open
     * question is whether the loop seams, and "I heard the join" is only
     * actionable next to which wrap it was — the bed loops every ~44 s, so a
     * session produces several and they are otherwise indistinguishable. */
    snprintf(line, sizeof(line),
             "voices %d/%d  clip %lu  lim %lu  starve %lu  lost %lu  drop %lu  bed %s %ld",
             v->voices, AUDIO_MAX_VOICES,
             (unsigned long)v->clipped, (unsigned long)v->limited,
             (unsigned long)v->starved, (unsigned long)v->lost,
             (unsigned long)v->dropped,
             v->music_on ? "ON wraps" : "off wraps", v->music_loops);
    fb_draw_text(fb, info_x, SCREEN_VISIBLE_TOP + 20,
                 line, (v->clipped || v->starved || v->lost)
                       ? RGB(255, 180, 60) : RGB(130, 200, 140), 1);

    if (v->last[0])
        fb_draw_text(fb, info_x, SCREEN_VISIBLE_TOP + 34,
                     v->last, RGB(160, 160, 200), 1);

    /* Voice meter: one cell per slot, lit for as many as are sounding. */
    int meter_y = SCREEN_SAFE_TOP + 56;
    for (int i = 0; i < AUDIO_MAX_VOICES; i++) {
        int cw = 22, cx = SCREEN_SAFE_LEFT + 4 + i * (cw + 4);
        uint32_t c = (i < v->voices) ? RGB(80, 230, 120) : RGB(35, 40, 50);
        fb_fill_rect(fb, cx, meter_y, cw, 8, c);
    }

    for (int i = 0; i < pad_count; i++) button_draw(fb, &pads[i].btn);
    draw_exit_button(fb, exit_btn);
}

/* ── main ────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *fb_dev    = (argc > 1) ? argv[1] : "/dev/fb0";
    const char *touch_dev = (argc > 2) ? argv[2] : "/dev/input/touchscreen0";

    int lock_fd = acquire_instance_lock("audio_mix_test");
    if (lock_fd < 0) return 1;

    open_log();

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    hw_init();
    hw_set_backlight(100);

    Audio audio;
    audio_init(&audio);                 /* non-fatal: the UI still works */

    /* fb before touch — touch_init() reads the dims fb_init() publishes. */
    fb_set_bpp(fb_dev, 32);
    Framebuffer fb;
    if (fb_init(&fb, fb_dev) < 0) {
        fprintf(stderr, "audio_mix_test: cannot open %s\n", fb_dev);
        audio_close(&audio); return 1;
    }
    TouchInput touch;
    if (touch_init(&touch, touch_dev) < 0) {
        fprintf(stderr, "audio_mix_test: cannot open %s\n", touch_dev);
        fb_close(&fb); audio_close(&audio); return 1;
    }
    touch_set_screen_size(&touch, screen_base_width, screen_base_height);

    Button exit_btn;
    button_init_full(&exit_btn, LAYOUT_EXIT_BTN_X, SCREEN_SAFE_TOP + 4,
                     BTN_EXIT_WIDTH, BTN_EXIT_HEIGHT, "",
                     BTN_EXIT_COLOR, COLOR_WHITE, BTN_HIGHLIGHT_COLOR, 2);

    /* ── rows.  Everything is laid out from SCREEN_SAFE_*, so it is right on a
     * calibrated panel and unchanged on one whose reach has never been swept.
     * ⚠️ **The five vertical offsets are DERIVED by rows_layout(), not written
     * here** — see its comment for what the shortfall on a heavily-inset panel is
     * paid for with, and why a fifth row is where that starts to matter. */
    int x, w, y;
    int gap = 6;
    rows_layout();

    y = row_y[0];
    /* Six pads now, and CONT is first because it is the OUTER switch: it decides
     * which device half is open, and the others describe what is done with it. */
    row_geom(6, 0, gap, &x, &w);
    Pad *cont_pad = pad_add(ACT_CONT,      "CONT: OFF",   x, y, w, row_h[0], BTN_COLOR_SECONDARY, 2);
    row_geom(6, 1, gap, &x, &w);
    Pad *pump_pad = pad_add(ACT_PUMP,      "PUMP: OFF",   x, y, w, row_h[0], BTN_COLOR_SECONDARY, 2);
    row_geom(6, 2, gap, &x, &w);
    Pad *keep_pad = pad_add(ACT_KEEPALIVE, "KEEP: OFF",   x, y, w, row_h[0], BTN_COLOR_SECONDARY, 2);
    row_geom(6, 3, gap, &x, &w);
    /* ⚠️ "LIM" with no value: set_toggle_labels() runs before the first draw and
     * reads the real position out of the library, so a literal here could only
     * ever be a lie waiting for a code path that skips that call. */
    Pad *limit_pad = pad_add(ACT_LIMIT,    "LIM",         x, y, w, row_h[0], BTN_COLOR_PRIMARY, 2);
    row_geom(6, 4, gap, &x, &w);
    Pad *level_pad = pad_add(ACT_LEVEL,    "LVL 1/6",     x, y, w, row_h[0], BTN_COLOR_INFO, 2);
    row_geom(6, 5, gap, &x, &w);
    pad_add(ACT_STOP, "STOP", x, y, w, row_h[0], BTN_COLOR_DANGER, 2);

    y = row_y[1];
    /* ⚠️ Two SUSTAINED tones, because defect 3's decisive question is about
     * TIMBRE and a 200 ms blip cannot be compared with a signal generator.
     * `440 3s` is one voice at AUDIO_PEAK, which the soft limiter passes through
     * byte-identically (its knee IS AUDIO_PEAK) — so it separates "the limiter
     * distorts the sum" from "this device's 440 is not a sine at all".
     * ⚠️ **This row was 80 px tall and is now 54**, which is where most of row 5's
     * space came from: 54 is confirmed comfortable to tap and the other four rows
     * were never taller than 64. */
    static const struct { const char *l; int f, ms; } tones[5] = {
        { "DRONE 220", 220, 3000 }, { "440 3s", 440, 3000 },
        { "440",       440,  200 }, { "880",   880,  200 },
        { "1760",     1760,  200 }
    };
    for (int i = 0; i < 5; i++) {
        row_geom(5, i, gap, &x, &w);
        Pad *p = pad_add(ACT_TONE, tones[i].l, x, y, w, row_h[1],
                         (tones[i].ms >= 3000) ? RGB(120, 60, 160)
                                               : RGB(40, 90, 170), 2);
        if (p) { p->freq = tones[i].f; p->ms = tones[i].ms; }
    }

    y = row_y[2];
    static const struct { const char *l; Action a; } canned[5] = {
        { "BEEP", ACT_BEEP }, { "BLIP", ACT_BLIP }, { "SUCCESS", ACT_SUCCESS },
        { "FAIL", ACT_FAIL }, { "CHORD", ACT_CHORD }
    };
    for (int i = 0; i < 5; i++) {
        row_geom(5, i, gap, &x, &w);
        pad_add(canned[i].a, canned[i].l, x, y, w, row_h[2], RGB(45, 110, 90), 2);
    }

    y = row_y[3];
    static const int ms_row[6] = { 5, 10, 20, 40, 60, 100 };
    for (int i = 0; i < 6; i++) {
        char l[12]; snprintf(l, sizeof(l), "%dms", ms_row[i]);
        row_geom(6, i, gap, &x, &w);
        Pad *p = pad_add(ACT_TONE, l, x, y, w, row_h[3], RGB(150, 100, 30), 2);
        if (p) { p->freq = 880; p->ms = ms_row[i]; }
    }

    /* ── row 5: recorded PCM, and the one inharmonic tone ────────────────────
     *
     * ⚠️ **`INH 622` exists because every other tone pad is an OCTAVE of every
     * other one** — 220 / 440 / 880 / 1760 — so this tool could not make an
     * inharmonic pair at all, and the `CHORD` pad (523/659/784) was the only
     * substitute. 622 against 440 is 1.4136, within 0.03 % of √2: the tritone,
     * the interval whose harmonics coincide least (440x7 = 3080 against
     * 622x5 = 3110). Against DRONE 220 it is a tritone plus an octave. Sustained
     * at 3 s for the same reason the other two long pads are
     * (../IMPROVEMENT_PLAN.md F1 — harmonic fusion is already refuted; this pad
     * is what lets that refutation be re-run without borrowing CHORD).
     *
     * ⚠️ **`W STOP` is not `STOP`.** The toggle row's STOP is `audio_interrupt()`
     * — every voice at once. This one releases only the bed, so an effect over it
     * keeps sounding: that difference is the whole point of a bed. */
    y = row_y[4];
    static const struct { const char *l; Action a; const char *path; int f, ms; uint32_t c; } srow[5] = {
        { "WAV 1",   ACT_MUSIC,      MUSIC1_PATH, 0,   0,    RGB(170, 90, 40)  },
        { "WAV 2",   ACT_MUSIC,      MUSIC2_PATH, 0,   0,    RGB(170, 90, 40)  },
        { "W STOP",  ACT_MUSIC_STOP, NULL,        0,   0,    RGB(120, 60, 60)  },
        { "SFX",     ACT_SFX,        SFX_PATH,    0,   0,    RGB(60, 140, 170) },
        { "INH 622", ACT_TONE,       NULL,        622, 3000, RGB(120, 60, 160) }
    };
    for (int i = 0; i < 5; i++) {
        row_geom(5, i, gap, &x, &w);
        Pad *p = pad_add(srow[i].a, srow[i].l, x, y, w, row_h[4], srow[i].c, 2);
        if (p) { p->path = srow[i].path; p->freq = srow[i].f; p->ms = srow[i].ms; }
    }

    /* ⚠️ The receipt for MAX_PADS.  pad_add() already complains per refused pad;
     * this says whether the table is exactly full, which is what the next person
     * adding a row needs to know. */
    fprintf(stderr, "mix: pads %d of %d\n", pad_count, MAX_PADS);

    View v; memset(&v, 0, sizeof(v));
    snprintf(v.last, sizeof(v.last), "CONT OFF = the per-sound-reset path");
    /* ⚠️ Start on the QUIETEST rung, not on the shipped default: the walk has to
     * run quiet-to-loud, and a tool that begins in the middle cannot enforce it. */
    v.rung = 0;
    audio_set_volume(&audio, ladder[0].vol);
    set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, level_pad, &audio, &v);

    bool needs_redraw = true;
    uint32_t last_readout = 0;
    uint32_t prev_now     = 0;

    while (running) {
        touch_poll(&touch);
        TouchState ts = touch_get_state(&touch);
        uint32_t   now = get_time_ms();

        /* ⚠️ The pump holds only its LEAD — the measured one, ~139 ms here, not the
         * 80 ms AUDIO_PUMP_LEAD_MS asks for — so ANY iteration longer than that
         * starves the device however correct the mix is.  Measuring the worst one is
         * what tells a pacing fault from a mixing fault, and it is the number
         * `starve` cannot give, because starve counts the symptom. */
        if (prev_now != 0 && (now - prev_now) > v.max_gap) v.max_gap = now - prev_now;
        prev_now = now;

        if (button_check_tap(&exit_btn, &ts, now)) running = false;

        for (int i = 0; i < pad_count && running; i++) {
            if (!button_check_tap(&pads[i].btn, &ts, now)) continue;
            Pad *p = &pads[i];
            needs_redraw = true;

            /* ⚠️ Every tap logs the TOGGLE STATE with it.  The first panel
             * report of this tool ("the drone stops and the 440 plays") could
             * not be diagnosed, because PUMP's position at the time was recalled
             * rather than recorded and the two paths predict different things.
             * A verdict about the mix bus is worthless without knowing which bus
             * was running, so `/tmp/mix.log` now says so on every line.  Note it
             * prints the LIBRARY's opinion, not the label's — a label that
             * disagreed with `audio->pumping` would itself explain the report. */
            /* ⚠️ The pad's own freq/ms go in the line, not just `act`.  A log that
             * says "a tone was tapped" cannot answer the ~60 ms question at all —
             * the whole point of the ms row is WHICH stimulus was silent, so the
             * record has to be self-identifying the same way the stimuli are. */
            /* ⚠️ `cont` is logged from audio_cont_active(), i.e. from the LIBRARY,
             * for the same reason `pump_active` is: it names which of the two device
             * halves was actually open when the tap landed, and a click reported
             * against a recalled toggle position cannot be attributed to either.
             * `svc_us` rides with it because the service ceiling is the one number
             * that turns "I heard a gap" into a pacing verdict — compare it with
             * `gapmax` on the same line. */
            fprintf(stderr, "mix: tap act=%d pad=%s freq=%d ms=%d path=%s cont=%d "
                            "svc_us=%ld pump_label=%d "
                            "pump_active=%d keepalive=%d limit=%s vol=%d shift=%d acoustic=%d voices=%d "
                            "clip=%lu lim=%lu starve=%lu lost=%lu drop=%lu "
                            "bed=%d wraps=%ld "
                            "gapmax=%lu lead=%ldfr/%ldms period=%ldfr\n",
                    (int)p->act, p->btn.text, p->freq, p->ms,
                    p->path ? p->path : "-",
                    (int)audio_cont_active(&audio),
                    audio_cont_service_interval_us(&audio),
                    (int)v.pump, (int)audio_pump_active(&audio),
                    (int)v.keepalive,
                    (audio_mix_get_limit(&audio.mix) == AUDIO_MIX_HARD) ? "hard" : "soft",
                    audio_get_volume(&audio), audio_get_master_shift(&audio),
                    audio_voice_peak(audio_get_volume(&audio))
                        >> audio_get_master_shift(&audio),
                    audio_pump_voices(&audio),
                    (unsigned long)audio_pump_clipped(&audio),
                    (unsigned long)audio_pump_limited(&audio),
                    (unsigned long)audio_pump_starved(&audio),
                    (unsigned long)audio_pump_lost(&audio),
                    (unsigned long)audio_pump_dropped(&audio),
                    (int)audio_music_active(&audio), audio.music.wav.loops,
                    (unsigned long)v.max_gap,
                    audio_pump_lead(&audio),
                    audio_ms_for_frames(audio.sample_rate,
                                        audio_pump_lead(&audio)),
                    audio_pump_period(&audio));

            switch (p->act) {
            case ACT_CONT:
                if (!v.cont) {
                    /* ⚠️ Remember the operator's own PUMP position BEFORE the
                     * library forces it on, and put it back on the way out.  CONT
                     * implies PUMP — a continuous stream needs a writer every
                     * service — so this pad silently changes a second toggle, and
                     * without the save the A/B afterwards compares PUMP: ON against
                     * PUMP: ON and reads as "the click never went away". */
                    v.pump_before_cont = v.pump;
                    if (audio_cont_enable(&audio, true) != 0) {
                        /* The library restored the old path and said why on stderr;
                         * the labels are still truthful, so only the readout moves. */
                        snprintf(v.last, sizeof(v.last), "CONT refused - old path kept");
                        break;
                    }
                    v.cont = true;
                    v.pump = true;              /* the library did this — mirror it */
                    v.max_gap = 0; prev_now = 0;  /* cont_enable zeroes the counters */
                    snprintf(v.last, sizeof(v.last), "CONT on: one never-reset stream");
                } else {
                    if (audio_cont_enable(&audio, false) != 0) {
                        v.cont = false;
                        snprintf(v.last, sizeof(v.last), "CONT off FAILED - no device");
                        set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, level_pad, &audio, &v);
                        break;
                    }
                    v.cont = false;
                    /* audio_cont_enable(false) leaves the bus off, so restoring the
                     * saved position is an explicit call rather than a field write. */
                    v.pump = v.pump_before_cont;
                    audio_pump_enable(&audio, v.pump);
                    if (v.pump) { v.max_gap = 0; prev_now = 0; }
                    /* keepalive is a live choice again, and the library still holds
                     * whatever it was set to — re-assert it so the label cannot lie. */
                    audio_pump_set_keepalive(&audio, v.keepalive);
                    snprintf(v.last, sizeof(v.last), "CONT off: per-sound reset back");
                }
                set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, level_pad, &audio, &v);
                break;
            case ACT_PUMP:
                if (v.cont) {
                    /* ⚠️ Tapped through to the library ON PURPOSE, because this is the
                     * one place its loud refusal can be SEEN: it prints the reason and
                     * silences the voices without giving the stream away.  The label
                     * already reads LOCK, so nothing here flips. */
                    audio_pump_enable(&audio, false);
                    snprintf(v.last, sizeof(v.last), "PUMP locked by CONT - voices cut");
                    break;
                }
                v.pump = !v.pump;
                audio_pump_enable(&audio, v.pump);
                /* ⚠️ audio_pump_enable() zeroes the library's counters on ON, so
                 * the worst frame has to zero with them or the panel shows a
                 * start-up stall (2474 ms at boot, measured) beside counters that
                 * start at 0 — one number describing a different session from all
                 * the others.  prev_now too, or the gap ACROSS this tap becomes
                 * the new worst frame. */
                if (v.pump) { v.max_gap = 0; prev_now = 0; }
                set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, level_pad, &audio, &v);
                snprintf(v.last, sizeof(v.last), "pump %s", v.pump ? "on" : "off");
                break;
            case ACT_KEEPALIVE:
                if (v.cont) {
                    /* Not refused by the library — simply unread by it, which is
                     * worse: a silent no-op reads as a broken toggle.  Say so. */
                    snprintf(v.last, sizeof(v.last), "KEEP n/a: CONT always writes");
                    break;
                }
                v.keepalive = !v.keepalive;
                audio_pump_set_keepalive(&audio, v.keepalive);
                set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, level_pad, &audio, &v);
                snprintf(v.last, sizeof(v.last), "keepalive %s",
                         v.keepalive ? "on (silence written)" : "off");
                break;
            case ACT_LIMIT: {
                /* Switchable while a drone runs: the two curves agree below the
                 * knee, so the change is inaudible on a quiet passage and obvious
                 * on a loud one — which is the comparison worth hearing.
                 *
                 * ⚠️ **The current position is READ, then flipped.** It used to
                 * flip a tool-local bool, which started false while the library
                 * started HARD — so the first tap "turned SOFT on" by setting
                 * HARD, and everything downstream printed the opposite of the truth. */
                bool now_hard = (audio_mix_get_limit(&audio.mix) == AUDIO_MIX_HARD);
                audio_pump_set_limit(&audio, now_hard ? AUDIO_MIX_SOFT : AUDIO_MIX_HARD);
                set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, level_pad, &audio, &v);
                bool is_hard = (audio_mix_get_limit(&audio.mix) == AUDIO_MIX_HARD);
                snprintf(v.last, sizeof(v.last), "limit %s",
                         is_hard ? "HARD (clamp at int16)" : "soft (knee 18000)");
                break;
            }
            case ACT_LEVEL: {
                /* ⚠️ Wraps back to the QUIETEST rather than reversing, so a second
                 * pass runs in the same direction as the first — a ladder walked up
                 * and then down is two different listening tasks, and the second one
                 * is the biased one this pad exists to avoid. */
                v.rung = (v.rung + 1) % LADDER_RUNGS;
                audio_set_volume(&audio, ladder[v.rung].vol);
                set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, level_pad,
                                  &audio, &v);
                /* The acoustic peak — after the device shift — is the number the ear
                 * is judging, and it is read back from the library. */
                int vol   = audio_get_volume(&audio);
                int shift = audio_get_master_shift(&audio);
                snprintf(v.last, sizeof(v.last), "vol %d peak %d%s%s",
                         vol, audio_voice_peak(vol) >> shift,
                         ladder[v.rung].note[0] ? " " : "", ladder[v.rung].note);
                break;
            }
            case ACT_STOP:
                audio_interrupt(&audio);
                snprintf(v.last, sizeof(v.last), "interrupt: all voices stopped");
                break;
            case ACT_TONE:
                audio_tone(&audio, p->freq, p->ms);
                snprintf(v.last, sizeof(v.last), "tone %d Hz %d ms", p->freq, p->ms);
                break;
            case ACT_BEEP:    audio_beep(&audio);
                snprintf(v.last, sizeof(v.last), "beep 880 Hz 80 ms"); break;
            case ACT_BLIP:    audio_blip(&audio);
                snprintf(v.last, sizeof(v.last), "blip 1320 Hz 60 ms"); break;
            case ACT_SUCCESS: audio_success(&audio);
                snprintf(v.last, sizeof(v.last), "success: 3 notes, offset"); break;
            case ACT_FAIL:    audio_fail(&audio);
                snprintf(v.last, sizeof(v.last), "fail: 3 notes, offset"); break;
            case ACT_CHORD:
                /* ⚠️ **This pad was an ARPEGGIO and was recorded as a chord, and a
                 * refutation rests on it.** Three back-to-back `audio_tone()` calls
                 * are consecutive statements, i.e. microseconds apart, so
                 * AUDIO_TONE_CHAIN_MS's recency gate saw a motif and CHAINED them:
                 * note 2 started behind note 1's tail and note 3 behind note 2's.
                 * F1 carries "HARD vs SOFT was inaudible, therefore clipping is
                 * refuted", and that A/B was judged with this pad — which never put
                 * two voices on the bus at once and so could not distinguish two
                 * limiters. `audio_mix_add()` with delay 0 is what a chord is.
                 *
                 * ⚠️ **The `audio_tone()` fallback stays, deliberately, for OFF the
                 * bus** — there they queue and the flush between throws the previous
                 * one away, and making that difference audible is what this tool is
                 * for. `audio_pump_active()` decides, not `v.pump`. */
                if (audio_pump_active(&audio)) {
                    int peak = audio_voice_peak(audio_get_volume(&audio));
                    audio_mix_add(&audio.mix, 523, 400, 0, peak);
                    audio_mix_add(&audio.mix, 659, 400, 0, peak);
                    audio_mix_add(&audio.mix, 784, 400, 0, peak);
                    snprintf(v.last, sizeof(v.last), "chord: 3 voices, delay 0");
                } else {
                    audio_tone(&audio, 523, 400);
                    audio_tone(&audio, 659, 400);
                    audio_tone(&audio, 784, 400);
                    snprintf(v.last, sizeof(v.last), "chord: off bus, 3 queued");
                }
                break;
            case ACT_MUSIC:
                /* ⚠️ Every refusal is the LIBRARY's and is already on stderr with its
                 * reason; what belongs here is a short version on the panel, because
                 * an operator holding a checklist cannot read /tmp/mix.log. */
                if (audio_music_start(&audio, p->path, true))
                    snprintf(v.last, sizeof(v.last), "bed: %s looping",
                             strrchr(p->path, '/') ? strrchr(p->path, '/') + 1 : p->path);
                else
                    snprintf(v.last, sizeof(v.last), "bed REFUSED - see /tmp/mix.log");
                break;
            case ACT_MUSIC_STOP:
                /* Releases the bed only — an effect over it keeps sounding, which is
                 * the difference between this and the toggle row's STOP. */
                audio_music_stop(&audio);
                snprintf(v.last, sizeof(v.last), "bed: release armed (fades out)");
                break;
            case ACT_SFX:
                if (audio_sfx_play(&audio, p->path))
                    snprintf(v.last, sizeof(v.last), "sfx: %s over the bus",
                             strrchr(p->path, '/') ? strrchr(p->path, '/') + 1 : p->path);
                else
                    snprintf(v.last, sizeof(v.last), "sfx REFUSED - see /tmp/mix.log");
                break;
            }
        }

        /* The pump, once per frame, exactly where a game would put it. */
        audio_pump(&audio);

        int      nv = audio_pump_voices(&audio);
        if (nv != v.voices) {           /* a voice starting or ending: draw now */
            v.voices = nv;
            needs_redraw = true;
        }

        /* The effective lead only exists once a pump has read the device period,
         * so it appears mid-session rather than at startup — redraw when it does. */
        long nlead = audio_pump_lead(&audio);
        if (nlead != v.lead_frames) {
            v.lead_frames   = nlead;
            v.period_frames = audio_pump_period(&audio);
            needs_redraw    = true;
        }

        /* The counters move on almost every sample, so they are refreshed on a
         * timer rather than on change — see READOUT_MS. */
        if ((uint32_t)(now - last_readout) >= READOUT_MS) {
            uint32_t nc = audio_pump_clipped(&audio);
            uint32_t nl = audio_pump_limited(&audio);
            uint32_t ns = audio_pump_starved(&audio);
            uint32_t nf = audio_pump_lost(&audio);
            uint32_t nd = audio_pump_dropped(&audio);
            /* The bed's state rides on the same timer: `loops` moves once every
             * ~44 s and `music_on` only on a start or the end of a fade, so
             * neither needs a redraw of its own. */
            bool nm = audio_music_active(&audio);
            long nw = audio.music.wav.loops;
            if (nc != v.clipped || nl != v.limited || ns != v.starved ||
                nf != v.lost    || nd != v.dropped ||
                nm != v.music_on || nw != v.music_loops) {
                v.clipped = nc; v.limited = nl; v.starved = ns;
                v.lost    = nf; v.dropped = nd;
                v.music_on = nm; v.music_loops = nw;
                needs_redraw = true;
            }
            last_readout = now;
        }

        bool drew = needs_redraw;
        if (needs_redraw) {
            draw_screen(&fb, &exit_btn, &v, audio.sample_rate);
            fb_swap(&fb);
            needs_redraw = false;
        }

        /* ⚠️ audio_pump_active() must be in this decision.  The pump keeps only
         * AUDIO_PUMP_LEAD_MS inside the device, so a loop that idles at 100 ms
         * mid-sound starves it and you hear a gap — and the gap would look like
         * a mixing defect rather than a pacing one. */
        usleep((drew || audio_pump_active(&audio)) ? FRAME_DELAY_ACTIVE_US
                                                   : FRAME_DELAY_IDLE_US);
    }

    hw_leds_off();
    hw_set_backlight(100);
    fb_fade_out(&fb);
    audio_close(&audio);
    touch_close(&touch);
    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    fb_close(&fb);
    return 0;
}
