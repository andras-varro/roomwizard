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
 *     common/audio.c common/audio_gen.c common/audio_out.c \
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
    ACT_CONT, ACT_PUMP, ACT_KEEPALIVE, ACT_LIMIT, ACT_STOP,
    ACT_TONE,                       /* uses freq/ms */
    ACT_BEEP, ACT_BLIP, ACT_SUCCESS, ACT_FAIL, ACT_CHORD
} Action;

typedef struct {
    Button btn;
    Action act;
    int    freq;
    int    ms;
} Pad;

#define MAX_PADS 24
static Pad  pads[MAX_PADS];
static int  pad_count = 0;

static Pad *pad_add(Action act, const char *label, int x, int y, int w, int h,
                    uint32_t colour, int scale)
{
    if (pad_count >= MAX_PADS) return NULL;
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
    bool hard;              /* LIMIT toggle: true = the pre-limiter hard clamp */
    int  voices;
    uint32_t clipped;
    uint32_t limited;
    uint32_t starved;
    uint32_t lost;
    uint32_t dropped;
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
                              Pad *limit_pad, const View *v)
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
     * line carries `limit=soft|hard` as its own field. */
    snprintf(t, sizeof(t), "LIM: %s", v->hard ? "HARD" : "SOFT");
    button_set_text(&limit_pad->btn, t);
    button_set_colors(&limit_pad->btn,
                      v->hard ? BTN_COLOR_DANGER : BTN_COLOR_PRIMARY,
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

    char line[112];
    snprintf(line, sizeof(line), "%d Hz  %s  %d voices  worst frame %lu ms",
             rate, lead, AUDIO_MAX_VOICES, (unsigned long)v->max_gap);
    fb_draw_text(fb, info_x, SCREEN_VISIBLE_TOP + 6,
                 line, (lead_ms > 0 && v->max_gap > (uint32_t)lead_ms)
                       ? RGB(255, 180, 60) : RGB(130, 140, 160), 1);

    snprintf(line, sizeof(line), "voices %d/%d  clip %lu  lim %lu  starve %lu  lost %lu  drop %lu",
             v->voices, AUDIO_MAX_VOICES,
             (unsigned long)v->clipped, (unsigned long)v->limited,
             (unsigned long)v->starved, (unsigned long)v->lost,
             (unsigned long)v->dropped);
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
     * The four rows are spread down the panel rather than packed at the top:
     * the bottom row still ends well above SCREEN_SAFE_BOTTOM on a calibrated
     * 800x450 (376 px against ~418), and bigger pads are easier to tap in
     * quick succession — which is the whole interaction this tool tests. */
    int x, w, y;
    int gap = 6;

    y = SCREEN_SAFE_TOP + 72;
    /* Five pads now, and CONT is first because it is the OUTER switch: it decides
     * which device half is open, and the other three describe what is done with it. */
    row_geom(5, 0, gap, &x, &w);
    Pad *cont_pad = pad_add(ACT_CONT,      "CONT: OFF",   x, y, w, 54, BTN_COLOR_SECONDARY, 2);
    row_geom(5, 1, gap, &x, &w);
    Pad *pump_pad = pad_add(ACT_PUMP,      "PUMP: OFF",   x, y, w, 54, BTN_COLOR_SECONDARY, 2);
    row_geom(5, 2, gap, &x, &w);
    Pad *keep_pad = pad_add(ACT_KEEPALIVE, "KEEP: OFF",   x, y, w, 54, BTN_COLOR_SECONDARY, 2);
    row_geom(5, 3, gap, &x, &w);
    Pad *limit_pad = pad_add(ACT_LIMIT,    "LIM: SOFT",   x, y, w, 54, BTN_COLOR_PRIMARY, 2);
    row_geom(5, 4, gap, &x, &w);
    pad_add(ACT_STOP, "STOP ALL", x, y, w, 54, BTN_COLOR_DANGER, 2);

    y = SCREEN_SAFE_TOP + 140;
    /* ⚠️ Two SUSTAINED tones, because defect 3's decisive question is about
     * TIMBRE and a 200 ms blip cannot be compared with a signal generator.
     * `440 3s` is one voice at AUDIO_PEAK, which the soft limiter passes through
     * byte-identically (its knee IS AUDIO_PEAK) — so it separates "the limiter
     * distorts the sum" from "this device's 440 is not a sine at all". */
    static const struct { const char *l; int f, ms; } tones[5] = {
        { "DRONE 220", 220, 3000 }, { "440 3s", 440, 3000 },
        { "440",       440,  200 }, { "880",   880,  200 },
        { "1760",     1760,  200 }
    };
    for (int i = 0; i < 5; i++) {
        row_geom(5, i, gap, &x, &w);
        Pad *p = pad_add(ACT_TONE, tones[i].l, x, y, w, 80,
                         (tones[i].ms >= 3000) ? RGB(120, 60, 160)
                                               : RGB(40, 90, 170), 2);
        if (p) { p->freq = tones[i].f; p->ms = tones[i].ms; }
    }

    y = SCREEN_SAFE_TOP + 234;
    static const struct { const char *l; Action a; } canned[5] = {
        { "BEEP", ACT_BEEP }, { "BLIP", ACT_BLIP }, { "SUCCESS", ACT_SUCCESS },
        { "FAIL", ACT_FAIL }, { "CHORD", ACT_CHORD }
    };
    for (int i = 0; i < 5; i++) {
        row_geom(5, i, gap, &x, &w);
        pad_add(canned[i].a, canned[i].l, x, y, w, 64, RGB(45, 110, 90), 2);
    }

    y = SCREEN_SAFE_TOP + 312;
    static const int ms_row[6] = { 5, 10, 20, 40, 60, 100 };
    for (int i = 0; i < 6; i++) {
        char l[12]; snprintf(l, sizeof(l), "%dms", ms_row[i]);
        row_geom(6, i, gap, &x, &w);
        Pad *p = pad_add(ACT_TONE, l, x, y, w, 64, RGB(150, 100, 30), 2);
        if (p) { p->freq = 880; p->ms = ms_row[i]; }
    }

    View v; memset(&v, 0, sizeof(v));
    snprintf(v.last, sizeof(v.last), "CONT OFF = the per-sound-reset path");
    set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, &v);

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
            fprintf(stderr, "mix: tap act=%d pad=%s freq=%d ms=%d cont=%d "
                            "svc_us=%ld pump_label=%d "
                            "pump_active=%d keepalive=%d limit=%s voices=%d "
                            "clip=%lu lim=%lu starve=%lu lost=%lu drop=%lu "
                            "gapmax=%lu lead=%ldfr/%ldms period=%ldfr\n",
                    (int)p->act, p->btn.text, p->freq, p->ms,
                    (int)audio_cont_active(&audio),
                    audio_cont_service_interval_us(&audio),
                    (int)v.pump, (int)audio_pump_active(&audio),
                    (int)v.keepalive, v.hard ? "hard" : "soft",
                    audio_pump_voices(&audio),
                    (unsigned long)audio_pump_clipped(&audio),
                    (unsigned long)audio_pump_limited(&audio),
                    (unsigned long)audio_pump_starved(&audio),
                    (unsigned long)audio_pump_lost(&audio),
                    (unsigned long)audio_pump_dropped(&audio),
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
                        set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, &v);
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
                set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, &v);
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
                set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, &v);
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
                set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, &v);
                snprintf(v.last, sizeof(v.last), "keepalive %s",
                         v.keepalive ? "on (silence written)" : "off");
                break;
            case ACT_LIMIT:
                /* Switchable while a drone runs: the two curves agree below the
                 * knee, so the change is inaudible on a quiet passage and obvious
                 * on a loud one — which is the comparison worth hearing. */
                v.hard = !v.hard;
                audio_pump_set_limit(&audio, v.hard ? AUDIO_MIX_HARD : AUDIO_MIX_SOFT);
                set_toggle_labels(cont_pad, pump_pad, keep_pad, limit_pad, &v);
                snprintf(v.last, sizeof(v.last), "limit %s",
                         v.hard ? "HARD (clamp at int16)" : "soft (knee 18000)");
                break;
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
                /* Three tones at once, deliberately.  On the pump these are three
                 * voices with no offsets; off it they queue back to back and the
                 * flush in between throws the previous one away — which is the
                 * whole difference this tool exists to make audible. */
                audio_tone(&audio, 523, 400);
                audio_tone(&audio, 659, 400);
                audio_tone(&audio, 784, 400);
                snprintf(v.last, sizeof(v.last), "chord: 3 notes together");
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
            if (nc != v.clipped || nl != v.limited || ns != v.starved ||
                nf != v.lost    || nd != v.dropped) {
                v.clipped = nc; v.limited = nl; v.starved = ns;
                v.lost    = nf; v.dropped = nd;
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
