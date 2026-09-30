/* audio_page.c — control_panel's Audio page: enable, music/effects, output
 * device, and the TEST chime.
 *
 * Opened from the home grid's Audio tile, and the one home for the four audio
 * keys control_panel writes; the Tests tab's tone sweep is a hardware test and
 * carries no setting.  Exposed only as cp_audio_page (cp_page.h); its state and
 * its audio bus live in this file.
 *
 * ⚠️ There is no SAVE button, on purpose — the LED page's rule.  Every toggle
 * and every OUT press writes its key the moment it changes, so TEST (which plays
 * the device the OUT button SHOWS) and a game (which plays the SAVED one) can
 * never be talking about two different outputs.
 */
#include "cp_page.h"
#include "cp_ui.h"
#include "../common/common.h"
#include "../common/audio.h"

#include <stdio.h>
#include <string.h>

/* ── State and persistence ─────────────────────────────────────────────── */

/* "onboard" | "usb" | "auto", in the order the OUT button cycles them. */
static const char *audio_device_names[3]  = { "onboard", "usb", "auto" };
static const char *audio_device_labels[3] = { "OUT: ONBOARD", "OUT: USB", "OUT: AUTO" };

/* ⚠️ The one index with a name, because it is the one index the DIM rule turns on:
 * "usb" is the only setting whose preference a unit can fail to meet.  A bare `1`
 * there read as "the middle one" and invited the mistake it replaced — the first
 * version of that line tested `== 0` and so dimmed AUTO as well. */
#define AUDIO_DEV_IDX_USB 1

typedef struct {
    bool enabled;       /* config key audio_enabled */
    bool music;         /* music_enabled — subordinate to enabled, see layout */
    bool effects;       /* effects_enabled — likewise */
    int  dev_idx;       /* audio_device, as an index into audio_device_names[] */
} AudioPageState;

static AudioPageState audio_state;      /* the values, as saved */

/* The page's own bus: open while the page is up, so TEST is a queue and a pump
 * rather than an open, a blocking hold and a close per press. */
static Audio page_audio;
static bool  page_audio_open;
static int   page_audio_idx;            /* the dev_idx it was opened on */

/* What the last paint showed for "is a USB DAC there" — input() compares the
 * live probe against it, so a DAC plugged in or pulled repaints the OUT button
 * without waiting for an unrelated touch. */
static bool shown_usb;

/* Anything unrecognised maps to onboard — the same thing audio_out_device_for()
 * does with an unknown value, so the button cannot show a state a game would not
 * actually resolve to. */
static int audio_device_index_of(const char *name) {
    for (int i = 0; i < 3; i++)
        if (name && strcmp(name, audio_device_names[i]) == 0) return i;
    return 0;
}

/* Every value read through config.c's helpers, which are what common/audio.c
 * reads, so the switch on screen cannot disagree with what a game will do —
 * and on a cleared Config they return exactly that default. */
static void audio_page_load(const Config *cfg) {
    audio_state.enabled = config_audio_enabled(cfg);
    audio_state.music   = config_music_enabled(cfg);
    audio_state.effects = config_effects_enabled(cfg);
    audio_state.dev_idx = audio_device_index_of(config_audio_device(cfg));
}

/* Writes the four keys into the FILE by re-reading it, not by saving the
 * caller's Config, and updates the in-memory copy too — led_page.c's
 * led_persist() and its reasons: a later whole-file save elsewhere then writes
 * these values rather than the old ones, and this one cannot persist anything
 * another page holds unsaved. */
static void audio_persist(const AudioPageState *s, Config *mem) {
    Config *both[2];
    Config disk;
    config_init(&disk);
    config_load(&disk);                 /* a missing file just starts empty */
    both[0] = mem;
    both[1] = &disk;
    for (int i = 0; i < 2; i++) {
        config_set_bool(both[i], "audio_enabled",   s->enabled);
        config_set_bool(both[i], "music_enabled",   s->music);
        config_set_bool(both[i], "effects_enabled", s->effects);
        config_set(both[i], "audio_device", audio_device_names[s->dev_idx]);
    }
    if (config_save(&disk) != 0) {
        fprintf(stderr, "control_panel: audio settings save failed\n");
        cp_status("AUDIO SAVE FAILED", false);
    }
}

static void page_audio_close(void) {
    if (!page_audio_open) return;
    audio_close(&page_audio);
    page_audio_open = false;
}

/* The unchecked open bypasses the ENABLE gate ON PURPOSE: a hardware test must
 * drive the speaker even with audio switched off, and audio_init() would make it
 * obey the very setting it exists to test.  The _pref form opens on the device
 * the OUT button shows — which, saved on change, is also the saved one.
 *
 * Held for the whole page, not per press: open → two blocking holds → close froze
 * the UI ~0.9 s per TEST and paid a stream start and stop each time.  Also why
 * this is not the library's serviced hold: input() pumps instead, every main-loop
 * iteration (and naming that call here would exempt this file from
 * check-audio-pacing.sh). */
static void page_audio_reopen(void) {
    page_audio_close();
    page_audio_idx  = audio_state.dev_idx;
    page_audio_open = (audio_init_unchecked_pref(&page_audio,
                           audio_device_names[audio_state.dev_idx]) == 0);
}

static void audio_page_enter(void) { page_audio_reopen(); }
static void audio_page_leave(void) { page_audio_close(); }

/* The main loop's pacing question (cp_page.h): true exactly while the stream
 * is open, which is audio_pump_active()'s own answer — FRAME_DELAY_IDLE_US
 * would starve it. */
static bool audio_page_busy(void) {
    return page_audio_open && audio_pump_active(&page_audio);
}

/* For the control panel's RESET DEFAULTS, called AFTER config_clear(cfg):
 * reads the state back from the cleared cfg, so the switches show config.c's
 * defaults — the file gets them when cp_reset_all_defaults() saves the cleared
 * Config right after.  An open bus follows the OUT default. */
static void audio_page_reset_defaults(Config *cfg) {
    audio_page_load(cfg);
    if (page_audio_open && page_audio_idx != audio_state.dev_idx)
        page_audio_reopen();
}

/* ── Layout ─────────────────────────────────────────────────────────────── */

static ToggleSwitch audio_toggle;
static ToggleSwitch music_toggle, effects_toggle;
static Button       audio_dev_btn;
static Button       test_audio_btn;

/* Row 2 carries MUSIC / EFFECTS / OUT under the master switch. */
#define AUD_SEC_Y      (CONTENT_Y + 2)
#define AUD_ROW2_Y     (AUD_SEC_Y + 54)
#define AUD_TRACK_W    60
#define AUD_TRACK_H    28

/* ⚠️ The OUT button's scale is pinned to 1 deliberately.  scale 2 needs 144 px
 * of glyphs, which fits landscape and NOT the ~150 px row 2 has left in
 * portrait.  Pinning also keeps the button_init macro out of it: that macro
 * picks scale 3 for any box wider than 150 px, keyed off width alone.
 *
 * ⚠️ button_draw() centres the text and neither pads nor clips it, so a label
 * wider than its box paints outside the button in silence.  The box is therefore
 * measured from the WIDEST of the three labels, not from the current one. */
#define AUD_OUT_LABEL_WIDEST "OUT: ONBOARD"      /* 12 chars; USB and AUTO are shorter */

/* A toggle's hit box right edge, as toggle_check_press() computes it. */
static int toggle_right(const ToggleSwitch *t) {
    return t->x - 5 + t->track_w + text_measure_width(t->label, 1) + 20;
}

/* Re-run whenever the logical screen changes (rebuild_ui()). */
static void audio_page_layout(void) {
    const AudioPageState *s = &audio_state;

    toggle_init(&audio_toggle, CONTENT_LEFT + 5, AUD_SEC_Y + 20,
                AUD_TRACK_W, AUD_TRACK_H, "AUDIO ENABLED", s->enabled);

    /* ── MUSIC / EFFECTS ────────────────────────────────────────────────────
     * The two keys every game reads through common/audio.c's audio_init().  They
     * are SUBORDINATE to AUDIO ENABLED: the master off means the process opens
     * no device at all, so these two decide nothing (common/config.h documents
     * the same hierarchy, and draw() dims them when the master is off).
     *
     * A per-game copy would need a live setter for `Audio.music_on`, a mid-run
     * bed stop, and seven writers of one config key.  This is one writer and no
     * new audio API; the price is that changing them means leaving the game.
     *
     * ⚠️ Widths are MEASURED, not guessed: toggle_draw() puts the label at
     * scale 1 eight pixels right of the track, and that whole box is what
     * toggle_check_press() hit-tests.  Both tracks are FLUSH with the master's
     * at CONTENT_LEFT + 5 — an indent read as a stray row rather than as a
     * child, so the subordination is carried by the dimming instead. */
    int music_w   = AUD_TRACK_W + 8 + text_measure_width("MUSIC", 1);
    int effects_w = AUD_TRACK_W + 8 + text_measure_width("EFFECTS", 1);
    toggle_init(&music_toggle, CONTENT_LEFT + 5, AUD_ROW2_Y,
                AUD_TRACK_W, AUD_TRACK_H, "MUSIC", s->music);
    toggle_init(&effects_toggle, CONTENT_LEFT + 5 + music_w + 40, AUD_ROW2_Y,
                AUD_TRACK_W, AUD_TRACK_H, "EFFECTS", s->effects);

    /* OUT: "onboard" | "usb" | "auto" as ONE button that cycles, because no
     * multi-choice widget exists in this app.  To the RIGHT of EFFECTS, 40 px
     * on — the gap this row already uses between MUSIC and EFFECTS. */
    int out_x = CONTENT_LEFT + 5 + music_w + 40 + effects_w + 40;
    int out_w = text_measure_width(AUD_OUT_LABEL_WIDEST, 1) + 16;   /* 8 px each side */
    button_init_full(&audio_dev_btn, out_x, AUD_ROW2_Y, out_w, AUD_TRACK_H,
                     audio_device_labels[s->dev_idx],
                     BTN_COLOR_INFO, COLOR_WHITE, BTN_COLOR_HIGHLIGHT, 1);

    button_init_full(&test_audio_btn, CONTENT_RIGHT - 100, AUD_SEC_Y + 18,
                     90, 30, "TEST", BTN_COLOR_INFO, COLOR_WHITE,
                     BTN_COLOR_HIGHLIGHT, 2);

    /* ⚠️ THE RECEIPT.  Everything here hangs off CONTENT_Y, which comes from a
     * per-unit touch inset, so a row pushed past the touchable rect looks
     * perfect in a screenshot and is dead to a finger.  Row 2 is the lowest
     * row; and row 2 is also the one exposed sideways — it stacks three
     * text-derived widths left to right, and the portrait content rect is only
     * about 150 px wider than the first two, so the OUT button is the widget
     * whose right edge can leave it.  Both read off the widgets as placed. */
    {
        int bottom = (audio_dev_btn.y + audio_dev_btn.height) - CONTENT_Y;
        int right  = audio_dev_btn.x + audio_dev_btn.width;
        const ToggleSwitch *tg[3] = { &audio_toggle, &music_toggle, &effects_toggle };
        for (int i = 0; i < 3; i++)
            if (toggle_right(tg[i]) > right) right = toggle_right(tg[i]);
        if (test_audio_btn.x + test_audio_btn.width > right)
            right = test_audio_btn.x + test_audio_btn.width;
        const char *verdict = bottom > CONTENT_H     ? "⚠ PAST CONTENT BOTTOM"
                            : right  > CONTENT_RIGHT ? "⚠ PAST CONTENT RIGHT"
                            : "fits";
        printf("control_panel: audio stack %s — bottom +%d of CONTENT_H %d, "
               "right %d of CONTENT_RIGHT %d (safe %dx%d, %s)\n",
               verdict, bottom, CONTENT_H, right, CONTENT_RIGHT,
               SCREEN_SAFE_WIDTH, SCREEN_SAFE_HEIGHT,
               CONTENT_WIDTH < 600 ? "portrait" : "landscape");
    }
}

/* ── Draw and input ─────────────────────────────────────────────────────── */

static void audio_page_draw(Framebuffer *fb) {
    const AudioPageState *s = &audio_state;

    draw_section_header(fb, AUD_SEC_Y, "AUDIO");
    audio_toggle.state   = s->enabled;  /* derived every frame, never cached */
    music_toggle.state   = s->music;
    effects_toggle.state = s->effects;
    toggle_draw(fb, &audio_toggle);
    button_draw(fb, &test_audio_btn);

    /* MUSIC / EFFECTS are still LIVE with the master off — they are saved
     * preferences, and refusing the press would just look broken — but they are
     * drawn dimmed, because with no device opened neither of them decides
     * anything and a bright green switch that changes nothing is a lie. */
    uint32_t on_c   = s->enabled ? RGB(0, 180, 60)    : RGB(0,  70, 25);
    uint32_t off_c  = s->enabled ? RGB(100, 100, 100) : RGB(55, 55, 55);
    uint32_t knob_c = s->enabled ? COLOR_WHITE        : RGB(150, 150, 150);
    uint32_t lbl_c  = s->enabled ? RGB(200, 200, 200) : RGB(120, 120, 120);
    toggle_set_colors(&music_toggle,   on_c, off_c, knob_c, lbl_c);
    toggle_set_colors(&effects_toggle, on_c, off_c, knob_c, lbl_c);
    toggle_draw(fb, &music_toggle);
    toggle_draw(fb, &effects_toggle);

    /* ⚠️ Dim, do not hide — and dim on the honest condition rather than on "is a
     * DAC plugged in".  The box always occupies its slot, so the row's geometry is
     * card-independent and the receipt means the same thing whatever is attached;
     * it goes grey when the preference it names cannot currently be met, which is
     * the master being off, or USB being asked for with no /dev/dsp1 to open.
     *
     * ⚠️ USB is the ONLY index that dims for absence.  ONBOARD is always there.
     * AUTO's preference is "whatever can be opened" — audio_out_device_for()
     * falls it back to /dev/dsp silently — so it is met on every unit; dimming
     * AUTO for a missing dongle was the first version of this line, and it told
     * the operator that a setting which works everywhere was unavailable.
     *
     * The press stays LIVE in both cases, for exactly the reason MUSIC and EFFECTS
     * do: this records a choice, and refusing "usb" before the DAC is plugged in
     * would just look broken — unlike the USB page's buttons, which START
     * something against a device that must exist. */
    shown_usb = audio_out_usb_present();
    bool out_live = s->enabled &&
                    (s->dev_idx != AUDIO_DEV_IDX_USB || shown_usb);
    audio_dev_btn.bg_color     = out_live ? BTN_COLOR_INFO : RGB(80, 80, 80);
    audio_dev_btn.text_color   = out_live ? COLOR_WHITE    : RGB(150, 150, 150);
    audio_dev_btn.border_color = audio_dev_btn.text_color;
    button_set_text(&audio_dev_btn, audio_device_labels[s->dev_idx]);
    button_draw(fb, &audio_dev_btn);
}

static CpPageResult audio_page_input(Config *cfg, int tx, int ty,
                                     bool touching, uint32_t now) {
    AudioPageState *s = &audio_state;
    CpPageResult act = CP_PAGE_IDLE;
    bool changed = false;

    /* Flip from the truth, not a stale widget. */
    audio_toggle.state   = s->enabled;
    music_toggle.state   = s->music;
    effects_toggle.state = s->effects;
    if (toggle_check_press(&audio_toggle, tx, ty, touching, now)) {
        s->enabled = audio_toggle.state;   changed = true;
    }
    if (toggle_check_press(&music_toggle, tx, ty, touching, now)) {
        s->music = music_toggle.state;     changed = true;
    }
    if (toggle_check_press(&effects_toggle, tx, ty, touching, now)) {
        s->effects = effects_toggle.state; changed = true;
    }
    /* Cycles onboard -> usb -> auto -> onboard.  The label is not written here;
     * draw() derives it from the index every frame, so the two cannot disagree. */
    if (button_update(&audio_dev_btn, tx, ty, touching, now)) {
        s->dev_idx = (s->dev_idx + 1) % 3;
        changed = true;
    }
    if (changed) {
        audio_persist(s, cfg);
        act = CP_PAGE_REDRAW;
    }

    /* OUT changed: move the open bus onto it, so the next TEST is heard where
     * the button points.  Also the lazy open for a bus whose page-entry open
     * failed. */
    if (button_update(&test_audio_btn, tx, ty, touching, now)) {
        if (!page_audio_open || page_audio_idx != s->dev_idx)
            page_audio_reopen();
        /* Queued, not played: the audio_pump() below delivers it, so the press
         * returns at once. */
        if (page_audio_open)
            audio_test_chime(&page_audio);
    }
    if (page_audio_open && page_audio_idx != s->dev_idx)
        page_audio_reopen();

    /* ⚠️ Every main-loop iteration: the panel calls input() once per iteration
     * whether or not anything was touched, which makes this the page's frame
     * tick.  audio_page_busy() keeps that tick at FRAME_DELAY_ACTIVE_US while
     * the bus is open; a closed bus is a no-op here. */
    audio_pump(&page_audio);

    if (audio_out_usb_present() != shown_usb)
        act = CP_PAGE_REDRAW;           /* a DAC came or went: re-dim OUT */
    return act;
}

const CpPage cp_audio_page = {
    .name           = "Audio",
    .icon           = "cp_audio",
    .load           = audio_page_load,
    .layout         = audio_page_layout,
    .enter          = audio_page_enter,
    .leave          = audio_page_leave,
    .draw           = audio_page_draw,
    .input          = audio_page_input,
    .reset_defaults = audio_page_reset_defaults,
    .busy           = audio_page_busy,
};
