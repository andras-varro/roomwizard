/*
 * start_menu — the arcade start menu every game opens on.
 *
 * A title, optional subtitle / instructions / warning, and up to
 * SM_MAX_ENTRIES entries in one centred column.  An entry is an ACTION
 * (activating it returns its index — START, EXIT) or a CHOICE (a list of
 * values, LEFT/RIGHT or a tap cycles it, wrapping — 1/2 PLAYERS, DIFFICULTY).
 *
 * The selected entry is drawn "> LABEL <".  The label is always lit; the
 * brackets blink, SM_BLINK_ON_MS on then SM_BLINK_OFF_MS off, timed from the
 * moment the menu opened or the selection last moved.  Each of the first
 * SM_PING_RUN blinks of every SM_PING_CYCLE pings (audio_ping()), the rest are
 * silent, so an idle menu calls for attention without droning.
 *
 * The widget owns no clock: the caller passes `now` (get_time_ms()).  Its
 * decisions — blink phase, ping, navigation, touch — live in
 * start_menu_step(), which touches no framebuffer and no audio device, so
 * tests/start_menu_test.c drives it on the host.  start_menu_update() is that
 * plus the two device calls.
 *
 * Redraw: OR start_menu_needs_redraw() into the loop's flag.  It is set only
 * on a bracket edge or an input change, so an idle menu costs two frames a
 * second; a loop that forgets it freezes the blink.  The loop must also run
 * audio_pump() while the menu is up, or the pings never leave the mix bus.
 */
#ifndef START_MENU_H
#define START_MENU_H

#include <stdbool.h>
#include <stdint.h>
#include "framebuffer.h"
#include "gamepad.h"
#include "audio.h"
#include "ui_focus.h"   /* UiRect only — the column needs no spatial search */

#define SM_MAX_ENTRIES   6
#define SM_MAX_VALUES    8
#define SM_BLINK_ON_MS   500
#define SM_BLINK_OFF_MS  500
#define SM_BLINK_MS      (SM_BLINK_ON_MS + SM_BLINK_OFF_MS)
#define SM_PING_CYCLE    6     /* blinks per ping cycle ...      */
#define SM_PING_RUN      3     /* ... of which the first N ping  */

#define SM_NONE  (-1)          /* start_menu_update(): nothing activated */
#define SM_EXIT  (-2)          /* BACK was pressed                       */

#define SM_NO_BLINK 0xFFFFFFFFu  /* "no blink seen yet" for the ping latch */

/* An unattended game must not make noise forever: after SM_SOUND_IDLE_MS
 * with no input everything goes silent — the menu's pings, and a demo's
 * music bed, which the game gates on start_menu_sound_allowed() — while the
 * blink and the attract cycle carry on.  Any input restarts the window. */
#define SM_SOUND_IDLE_MS 60000u

/* Attract cycle.  After SM_ATTRACT_MS with no input the menu hands the screen
 * to the game: DEMO (only if the game has one, start_menu_set_attract()),
 * then SCORES, then MENU again, SM_ATTRACT_MS each, round and round.  The
 * game draws DEMO and SCORES itself, reading start_menu_attract(); the menu
 * is silent there, and any input returns to MENU and is SWALLOWED — it
 * starts nothing, and the button needs a fresh press to act.  A demo may end
 * early (its AI died): start_menu_demo_over() moves to SCORES on the next
 * update, and SCORES then runs its full SM_ATTRACT_MS. */
#define SM_ATTRACT_MS 15000u
typedef enum { SM_ATTRACT_MENU, SM_ATTRACT_DEMO, SM_ATTRACT_SCORES } SmAttract;

#define SM_WARN_NEED_P2 "CONNECT A SECOND CONTROLLER (INPUT PAGE)"

typedef enum { SM_ACTION, SM_CHOICE } StartMenuKind;

typedef struct {
    StartMenuKind kind;
    char label[32];                 /* uppercased copy; "" = a choice shows its value only */
    const char *const *values;      /* CHOICE: caller-owned, outlives the menu */
    int  nvalues;
    int  value;                     /* CHOICE: current value index */
    unsigned value_off;             /* CHOICE: bit v set = value v not selectable now */
    bool disabled;                  /* skipped by navigation, refuses touch, drawn grey */
} StartMenuEntry;

typedef struct {
    char title[64];
    char subtitle[64];
    char instructions[256];
    char warning[96];
    StartMenuEntry entries[SM_MAX_ENTRIES];
    int  count;
    int  sel;
    int  players_idx;               /* the start_menu_players() entry, or -1 */
    bool warn_is_players;           /* the warning is that entry's, and clears with it */

    uint32_t t0;                    /* blink origin: init, selection change, reveal */
    bool     marker_on;             /* bracket phase as last drawn */
    uint32_t seen_on_idx;           /* last blink whose on-phase was seen (ping latch) */
    bool     shown;                 /* the brackets are drawn at all: a touch hides them */
    bool     dirty;

    uint32_t  last_input_ms;        /* idle clock: the sound cutoff */
    uint32_t  cycle_origin_ms;      /* attract clock: last input, moved back by a demo ending early */
    SmAttract attract;              /* phase on screen now */
    bool      has_demo;
    bool      demo_over;            /* start_menu_demo_over() pending for the next update */

    bool prev_held[BTN_ID_COUNT];   /* own edges, seeded TRUE: a held button needs a fresh press */
    bool was_touching;              /* seeded TRUE: a finger down at init must lift first */
    int  last_tx, last_ty;          /* where the finger was last seen down */
    int  pressed;                   /* entry under the touch press, or -1 */

    bool    laid_out;
    UiRect  rect[SM_MAX_ENTRIES];   /* one layout for draw AND hit test */
    int     title_y, sub_y, inst_y, warn_y;
} StartMenu;

/* Setup.  title/instructions may be NULL; instructions may contain '\n'. */
void start_menu_init(StartMenu *m, const char *title, const char *instructions, uint32_t now);
void start_menu_set_subtitle(StartMenu *m, const char *subtitle);
void start_menu_set_warning(StartMenu *m, const char *warning);   /* NULL clears */
int  start_menu_add_action(StartMenu *m, const char *label);      /* index, or -1 when full */
int  start_menu_add_choice(StartMenu *m, const char *label,
                           const char *const *values, int nvalues, int initial);
/* A "1 PLAYER" / "2 PLAYERS" choice.  2 is selectable only while slots P1 and
 * P2 both hold a device; if P2 leaves while 2 is chosen it drops to 1 and the
 * warning becomes SM_WARN_NEED_P2. */
int  start_menu_players(StartMenu *m);
void start_menu_set_disabled(StartMenu *m, int idx, bool disabled);
void start_menu_select(StartMenu *m, int idx, uint32_t now);      /* e.g. START by default */

/* Pure decisions (host-testable). */
void start_menu_phase(uint32_t elapsed_ms, bool *marker_on, uint32_t *blink_idx);
bool start_menu_sound_allowed(uint32_t idle_ms);
bool start_menu_ping_due(bool marker_on, uint32_t blink_idx, uint32_t idle_ms,
                         uint32_t *seen_on_idx);
SmAttract start_menu_attract_at(uint32_t idle_ms, bool has_demo);

/* One frame without devices: input in (in may be NULL), result out, *ping
 * says to play audio_ping().  player_mask is gamepad_player_mask()'s. */
int  start_menu_step(StartMenu *m, const InputState *in, int tx, int ty, bool touching,
                     int player_mask, uint32_t now, bool *ping);
/* The same, reading the mask from gm (NULL = P1 only) and playing the ping. */
int  start_menu_update(StartMenu *m, const InputState *in, int tx, int ty, bool touching,
                       const GamepadManager *gm, Audio *audio, uint32_t now);

bool start_menu_needs_redraw(const StartMenu *m);
void start_menu_draw(StartMenu *m, Framebuffer *fb);   /* whole screen; caller swaps */
int  start_menu_value(const StartMenu *m, int idx);    /* CHOICE value index, -1 otherwise */
int  start_menu_player_count(const StartMenu *m);      /* 1 or 2; 1 with no players entry */
void start_menu_set_attract(StartMenu *m, bool has_demo);  /* default: no DEMO phase */
SmAttract start_menu_attract(const StartMenu *m);      /* what the game draws this frame */
void start_menu_demo_over(StartMenu *m);               /* DEMO -> SCORES at the next update */
uint32_t start_menu_idle_ms(const StartMenu *m, uint32_t now);  /* feed start_menu_sound_allowed() */

#endif /* START_MENU_H */
