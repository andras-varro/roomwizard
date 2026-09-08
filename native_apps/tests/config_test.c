/* Host-side regression for the config key/value store's parser and ceilings.
 *
 * Runs on the DEV MACHINE with native gcc, not on the device.  common/config.c
 * links libc and nothing else — no framebuffer, no /dev/dsp, no ioctl, no clock
 * — and config_init_path() is the seam: it takes the file the store will read
 * and write, so a fixture under build/ exercises the shipped parser, the shipped
 * writer and the shipped atomic-rename path with no device in the loop.  That
 * makes every rule below observable here, including three that are silent on the
 * panel: an overflowing file, an over-long line and an over-long key all load
 * successfully and lose data without a word.
 *
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common -o build/config_test tests/config_test.c common/config.c -lm && ./build/config_test
 *
 * What it asserts, and why: groups A-D are the contract every caller relies on —
 * a set/get/save/load round-trip, the documented defaults, the comment/blank/
 * whitespace rules, an empty value being a STORED empty string rather than the
 * caller's default (audio_bed reads "present and empty" as a game's off switch,
 * so conflating it with "absent" changes behaviour), the typed getters' numeric
 * and boolean vocabularies, and in-place update, removal and clear.  Groups E-J
 * pin behaviour no caller would predict from config.h, and five of those are
 * DEFECTS rather than intent — each is labelled at its group, because a
 * regression that silently blesses a defect is worse than none.  There is no
 * fixed-arity sscanf here to pin (the parse is one strchr('=')), so the
 * analogous silent-success hazards are the three ceilings: CONFIG_MAX_KEYS in F,
 * the 144-byte line buffer in G, and CONFIG_KEY_LEN / Config::filepath in I.
 *
 * NOT part of build-and-deploy.sh: that script cross-compiles for ARM and this
 * is a host binary.  Run it by hand, or through tests/run-all.sh, after touching
 * common/config.c.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include "config.h"

static int fails = 0;

static void check(int cond, const char *what)
{
    if (cond) printf("  ok   %-38s\n", what);
    else      { printf("  FAIL %-38s\n", what); fails++; }
}

static void check_str(const char *what, const char *got, const char *want)
{
    if (got && strcmp(got, want) == 0) printf("  ok   %-38s '%s'\n", what, got);
    else { printf("  FAIL %-38s got '%s' want '%s'\n", what, got ? got : "(null)", want);
           fails++; }
}

static void check_int(const char *what, int got, int want)
{
    if (got == want) printf("  ok   %-38s %d\n", what, got);
    else { printf("  FAIL %-38s got %d want %d\n", what, got, want); fails++; }
}

/* Fixtures live under build/, not /tmp: Git Bash's /tmp and WSL's /tmp are
 * different filesystems and WSL's does not survive between wsl.exe calls
 * (../CLAUDE.md), so a fixture staged in one call can be gone by the next.
 * audio_bed_test.c puts its set file here for the same reason. */
#define FIX      "build/config_test.conf"
#define FIX2     "build/config_test.2.conf"
#define FIX_DIR  "build/config_test.d"
#define FIX_SUB  "build/config_test.d2/x.conf"
#define FIX_DEEP "build/config_test.d3/deeper/x.conf"

static void fixtures_remove(void)
{
    unlink(FIX);      unlink(FIX      ".tmp");
    unlink(FIX2);     unlink(FIX2     ".tmp");
    unlink(FIX_SUB);  unlink(FIX_SUB  ".tmp");
    unlink(FIX_DEEP); unlink(FIX_DEEP ".tmp");
    rmdir(FIX_DIR);
    rmdir("build/config_test.d2");
    rmdir("build/config_test.d3/deeper");
    rmdir("build/config_test.d3");
}

static void write_fix(const char *path, const char *body)
{
    FILE *f = fopen(path, "w");
    if (!f) { printf("  FAIL %-38s %s\n", "fixture is not writable", path); fails++; return; }
    fputs(body, f);
    fclose(f);
}

/* Push a body through the shipped parser and hand back its return code. */
static int load_body(Config *cfg, const char *body)
{
    write_fix(FIX, body);
    config_init_path(cfg, FIX);
    return config_load(cfg);
}

/* Lines of a saved file that are neither blank nor a comment. */
static int payload_lines(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '\0' && *p != '\n' && *p != '#') n++;
    }
    fclose(f);
    return n;
}

static int first_char_of(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int c = fgetc(f);
    fclose(f);
    return c;
}

int main(void)
{
    Config cfg, re;
    int rc;

    printf("config_test — the key/value store's parser, writer and ceilings\n");
    /* build/ is where the documented build line puts the binary, so it normally
     * exists; created here anyway, because "fixture is not writable" is a much
     * worse first report than a missing directory. */
    mkdir("build", 0755);
    fixtures_remove();

    /* ── A: the round-trip every caller relies on ─────────────────────────── */
    printf("\nA  happy path: init, set, get, save, load\n");
    config_init_path(&cfg, FIX);
    check_str("A filepath is the one given", cfg.filepath, FIX);
    check_int("A a fresh config holds no keys", cfg.count, 0);
    check_str("A a never-set key takes the default",
              config_get(&cfg, "nothing_here", "DEF"), "DEF");

    unlink(FIX);
    check_int("A an absent file loads as -1", config_load(&cfg), -1);
    check_int("A a failed load leaves no keys", cfg.count, 0);

    config_init_path(&cfg, FIX);
    config_set(&cfg, "alpha", "one");
    config_set(&cfg, "beta",  "two");
    check_int("A two sets make two keys", cfg.count, 2);
    check_str("A a set value reads back", config_get(&cfg, "alpha", "DEF"), "one");
    check_int("A save returns 0", config_save(&cfg), 0);
    check_int("A the writer opens with a comment", first_char_of(FIX), '#');
    check_int("A the file holds only the keys", payload_lines(FIX), 2);
    check_int("A the temp file does not survive", access(FIX ".tmp", F_OK), -1);

    config_init_path(&re, FIX);
    check_int("A reload returns 0", config_load(&re), 0);
    check_int("A the header counts as no keys", re.count, 2);
    check_str("A alpha survives the round-trip", config_get(&re, "alpha", "DEF"), "one");
    check_str("A beta survives the round-trip",  config_get(&re, "beta",  "DEF"), "two");

    config_init_path(&cfg, FIX2);
    check_int("A an empty config saves", config_save(&cfg), 0);
    config_init_path(&re, FIX2);
    rc = config_load(&re);
    check_int("A an empty config loads as 0", rc, 0);
    check_int("A and holds no keys", re.count, 0);

    /* ── B: what the parser does with a line ──────────────────────────────── */
    printf("\nB  parsing: comments, blanks, whitespace, the '=' split\n");
    rc = load_body(&cfg,
        "# a comment\n"
        "\n"
        "   \n"
        "   # an indented comment\n"
        "# commented=out\n"
        "   # indented=out\n"
        "kept=yes\n"
        "  spaced   =   value with spaces   \n"
        "\tt\t=\tv\t\n"
        "no_equals_at_all\n"
        " =orphan\n"
        "empty=\n"
        "twoeq=a=b\n"
        "hash=v # not a comment\n");
    check_int("B the body loads as 0", rc, 0);
    check_int("B comments and blanks are skipped", cfg.count, 6);
    check_str("B a plain pair", config_get(&cfg, "kept", "DEF"), "yes");
    check_str("B whitespace around '=' is trimmed",
              config_get(&cfg, "spaced", "DEF"), "value with spaces");
    check_str("B tabs are whitespace too", config_get(&cfg, "t", "DEF"), "v");
    check_str("B a line with no '=' is dropped",
              config_get(&cfg, "no_equals_at_all", "DEF"), "DEF");
    check_str("B an empty key is dropped", config_get(&cfg, "", "DEF"), "DEF");
    /* A comment line that CONTAINS an '=' is the only shape the '#' test can be
     * seen working on: one without an '=' is dropped by the next rule anyway,
     * which makes the assertion vacuous.  Measured — a sabotage deleting the
     * '#' check went undetected until these two lines existed. */
    check_str("B a commented-out pair is dropped",
              config_get(&cfg, "# commented", "DEF"), "DEF");
    check_str("B even an indented one", config_get(&cfg, "# indented", "DEF"), "DEF");
    /* An empty value is STORED and returned as "", never as the caller's
     * default: audio_bed reads "<tag>_music present and empty" as a whole-game
     * off switch, which is a different statement from "absent". */
    check_str("B an empty value is stored, not defaulted",
              config_get(&cfg, "empty", "DEF"), "");
    check_str("B only the FIRST '=' splits", config_get(&cfg, "twoeq", "DEF"), "a=b");
    check_str("B '#' mid-line stays in the value",
              config_get(&cfg, "hash", "DEF"), "v # not a comment");

    /* ── C: the typed getters ─────────────────────────────────────────────── */
    printf("\nC  typed getters: int, bool, the convenience helpers\n");
    rc = load_body(&cfg,
        "num=42\n" "neg=-12\n" "plus=+7\n" "notnum=12abc\n" "empty=\n"
        "b_one=1\n" "b_true=TrUe\n" "b_yes=YES\n" "b_on=on\n"
        "b_zero=0\n" "b_false=FALSE\n" "b_no=No\n" "b_off=OFF\n"
        "b_junk=maybe\n");
    check_int("C the body loads as 0", rc, 0);
    check_int("C a plain integer",     config_get_int(&cfg, "num",    99), 42);
    check_int("C a leading '-'",       config_get_int(&cfg, "neg",    99), -12);
    check_int("C a leading '+'",       config_get_int(&cfg, "plus",   99), 7);
    check_int("C a non-numeric value", config_get_int(&cfg, "notnum", 99), 99);
    check_int("C an empty value",      config_get_int(&cfg, "empty",  99), 99);
    check_int("C an absent key",       config_get_int(&cfg, "absent", 99), 99);
    check(config_get_bool(&cfg, "b_one",   false), "C '1' is true");
    check(config_get_bool(&cfg, "b_true",  false), "C 'TrUe' is true, case-blind");
    check(config_get_bool(&cfg, "b_yes",   false), "C 'YES' is true");
    check(config_get_bool(&cfg, "b_on",    false), "C 'on' is true");
    check(!config_get_bool(&cfg, "b_zero",  true), "C '0' is false");
    check(!config_get_bool(&cfg, "b_false", true), "C 'FALSE' is false");
    check(!config_get_bool(&cfg, "b_no",    true), "C 'No' is false");
    check(!config_get_bool(&cfg, "b_off",   true), "C 'OFF' is false");
    check(config_get_bool(&cfg, "b_junk",   true), "C an unknown word takes the default");
    check(!config_get_bool(&cfg, "b_junk", false), "C ... in both directions");
    check(config_get_bool(&cfg, "absent",   true), "C an absent bool takes the default");

    config_clear(&cfg);
    check(config_audio_enabled(&cfg),   "C audio defaults on");
    check(config_music_enabled(&cfg),   "C music defaults on");
    check(config_effects_enabled(&cfg), "C effects default on");
    check(config_led_enabled(&cfg),     "C leds default on");
    check_int("C led brightness defaults 100",       config_led_brightness(&cfg), 100);
    check_int("C backlight brightness defaults 100", config_backlight_brightness(&cfg), 100);
    config_set(&cfg, "led_brightness", "40");
    config_set_bool(&cfg, "audio_enabled", false);
    check_int("C a stored brightness wins", config_led_brightness(&cfg), 40);
    check(!config_audio_enabled(&cfg), "C a stored toggle wins");

    /* ── D: setters, removal, clear ───────────────────────────────────────── */
    printf("\nD  setters, removal, clear\n");
    config_init_path(&cfg, FIX);
    config_set(&cfg, "a", "1");
    config_set(&cfg, "b", "2");
    config_set(&cfg, "c", "3");
    config_set(&cfg, "b", "22");
    check_int("D an update does not append", cfg.count, 3);
    check_str("D the update lands in place", config_get(&cfg, "b", "DEF"), "22");
    check_str("D and does not move the key", cfg.entries[1].key, "b");
    config_set_int(&cfg, "i", -5);
    check_str("D set_int stores the digits", config_get(&cfg, "i", "DEF"), "-5");
    config_set_bool(&cfg, "t", true);
    config_set_bool(&cfg, "f", false);
    check_str("D set_bool true is '1'",  config_get(&cfg, "t", "DEF"), "1");
    check_str("D set_bool false is '0'", config_get(&cfg, "f", "DEF"), "0");
    check(config_remove(&cfg, "b"), "D remove reports the removal");
    check_int("D remove drops the count", cfg.count, 5);
    check_str("D remove closes the gap", cfg.entries[1].key, "c");
    check_str("D the removed key is gone", config_get(&cfg, "b", "DEF"), "DEF");
    check_str("D the tail survived the shift", config_get(&cfg, "f", "DEF"), "0");
    check(!config_remove(&cfg, "b"), "D removing it twice is false");
    check(!config_remove(&cfg, "never_there"), "D removing an absent key is false");
    config_clear(&cfg);
    check_int("D clear empties the store", cfg.count, 0);
    check_str("D and every key reads default", config_get(&cfg, "a", "DEF"), "DEF");

    /* ── E: DUPLICATE keys — a pinned DEFECT ──────────────────────────────── */
    /* config_load() never consults find_key(), so a file naming one key twice
     * stores two entries: config_get() answers with the first, config_save()
     * writes both back, and config_set() updates only the first — so the stale
     * second value survives every round-trip and surfaces the moment the first
     * is removed.  Pinned as measured behaviour, not as intent. */
    printf("\nE  duplicate keys are BOTH stored (pinned defect)\n");
    rc = load_body(&cfg, "dup=first\nother=x\ndup=second\n");
    check_int("E the file loads as 0", rc, 0);
    check_int("E a doubled key counts twice", cfg.count, 3);
    check_str("E a get answers with the first", config_get(&cfg, "dup", "DEF"), "first");
    check_str("E the second entry survives load", cfg.entries[2].value, "second");
    /* Round-trip the duplicate through the writer, using only the public API to
     * build it — config_set() cannot make a duplicate, which is the asymmetry. */
    config_init_path(&cfg, FIX2);
    {
        Config dup;
        config_init_path(&dup, FIX2);
        rc = load_body(&dup, "dup=first\ndup=second\n");
        strncpy(dup.filepath, FIX2, sizeof(dup.filepath) - 1);
        check_int("E a duplicate saves", config_save(&dup), 0);
    }
    check_int("E both copies are written", payload_lines(FIX2), 2);
    config_init_path(&re, FIX2);
    rc = config_load(&re);
    check_int("E the saved duplicate reloads", rc, 0);
    check_int("E and both come back", re.count, 2);
    config_set(&re, "dup", "third");
    check_int("E a set does not collapse them", re.count, 2);
    check_str("E a set updates the first only", re.entries[0].value, "third");
    check_str("E the stale second is left behind", re.entries[1].value, "second");
    check(config_remove(&re, "dup"), "E removing the first reports true");
    check_str("E and exposes the stale second",
              config_get(&re, "dup", "DEF"), "second");

    /* ── F: the CONFIG_MAX_KEYS ceiling — a pinned DEFECT ─────────────────── */
    /* Past 32 keys config_load() stops storing and still returns 0, and
     * config_set() returns without a word, so a load-then-save cycle rewrites
     * the file with the overflow DELETED.  Nothing reports it: the return code
     * is success and no counter exists.  Pinned as measured behaviour. */
    printf("\nF  past 32 keys the rest are dropped in silence (pinned defect)\n");
    {
        char body[4096];
        size_t used = 0;
        int i;
        for (i = 0; i < 40; i++)
            used += (size_t)snprintf(body + used, sizeof(body) - used, "k%d=v%d\n", i, i);
        rc = load_body(&cfg, body);
    }
    check_int("F an over-full file still loads 0", rc, 0);
    check_int("F exactly 32 keys are kept", cfg.count, 32);
    check_str("F the last kept key",     config_get(&cfg, "k31", "DEF"), "v31");
    check_str("F key 33 is simply gone", config_get(&cfg, "k32", "DEF"), "DEF");
    check_str("F key 40 is simply gone", config_get(&cfg, "k39", "DEF"), "DEF");
    config_set(&cfg, "one_too_many", "x");
    check_int("F a set on a full store is a no-op", cfg.count, 32);
    check_str("F and stores nothing", config_get(&cfg, "one_too_many", "DEF"), "DEF");
    strncpy(cfg.filepath, FIX2, sizeof(cfg.filepath) - 1);
    check_int("F the truncated store saves", config_save(&cfg), 0);
    check_int("F a load-save cycle DELETES 8 keys", payload_lines(FIX2), 32);

    /* ── G: the 144-byte line buffer — a pinned DEFECT ────────────────────── */
    /* line[] is CONFIG_KEY_LEN + CONFIG_VAL_LEN + 16 == 144 bytes, so fgets()
     * splits any line over 143 characters.  The head keeps the key with a value
     * cut to CONFIG_VAL_LEN-1, and the TAIL is then parsed as a line of its own:
     * if it contains an '=' it becomes a key that appears nowhere in the file.
     * A legal maximum pair (63 + '=' + 63 == 127) fits, so only an over-long
     * value reaches this — silently, in both halves.  Pinned as measured
     * behaviour. */
    printf("\nG  a line over 143 bytes splits into two (pinned defect)\n");
    {
        char body[512];
        size_t n = 0;
        int i;
        n += (size_t)snprintf(body + n, sizeof(body) - n, "pad=");
        for (i = 0; i < 139; i++) body[n++] = 'x';
        body[n] = '\0';
        n += (size_t)snprintf(body + n, sizeof(body) - n, "spurious=yes\n");
        rc = load_body(&cfg, body);
    }
    check_int("G the long line loads as 0", rc, 0);
    check_int("G one line became two keys", cfg.count, 2);
    check_str("G the head keeps its key", cfg.entries[0].key, "pad");
    check_int("G its value is cut at 63",
              (int)strlen(config_get(&cfg, "pad", "")), CONFIG_VAL_LEN - 1);
    check_str("G the tail's '=' invents a key",
              config_get(&cfg, "spurious", "DEF"), "yes");
    {
        char body[512];
        size_t n = 0;
        int i;
        n += (size_t)snprintf(body + n, sizeof(body) - n, "pad=");
        for (i = 0; i < 139; i++) body[n++] = 'x';
        body[n] = '\0';
        n += (size_t)snprintf(body + n, sizeof(body) - n, "tail_without_any_equals\n");
        rc = load_body(&cfg, body);
    }
    check_int("G a tail with no '=' loads as 0", rc, 0);
    check_int("G a tail with no '=' is dropped", cfg.count, 1);

    /* ── H: CRLF input ───────────────────────────────────────────────────── */
    /* A file edited on Windows parses correctly, because the whole line is
     * trimmed before the '=' split and '\r' is isspace().  Worth asserting: a
     * '\r' left clinging to a value would be invisible in every log line. */
    printf("\nH  CRLF input parses, because trim() eats the '\\r'\n");
    rc = load_body(&cfg, "a=1\r\n\r\n# comment\r\nb = 2 \r\nc=\r\n");
    check_int("H a CRLF file loads as 0", rc, 0);
    check_int("H CRLF blanks and comments skip", cfg.count, 3);
    check_str("H no '\\r' clings to the value", config_get(&cfg, "a", "DEF"), "1");
    check_str("H nor to a padded value",        config_get(&cfg, "b", "DEF"), "2");
    check_str("H nor to an empty one",          config_get(&cfg, "c", "DEF"), "");
    check_int("H nor to the key", (int)strlen(cfg.entries[0].key), 1);

    /* ── I: silent length truncation ──────────────────────────────────────── */
    /* Two more ceilings that succeed while losing data.  A key over 63 bytes is
     * cut, so two keys differing only past byte 63 become one; a path over 127
     * bytes is cut by config_init_path(), which then addresses a DIFFERENT file
     * rather than failing.  Both pinned as measured behaviour. */
    printf("\nI  over-long keys and paths are cut, not refused\n");
    {
        char body[256];
        size_t n = 0;
        int i;
        for (i = 0; i < 70; i++) body[n++] = 'K';
        body[n] = '\0';
        n += (size_t)snprintf(body + n, sizeof(body) - n, "=v\n");
        rc = load_body(&cfg, body);
    }
    check_int("I an over-long key loads as 0", rc, 0);
    check_int("I it is stored, once", cfg.count, 1);
    check_int("I cut to CONFIG_KEY_LEN-1",
              (int)strlen(cfg.entries[0].key), CONFIG_KEY_LEN - 1);
    check_str("I its value is intact", cfg.entries[0].value, "v");
    {
        char big[300];
        memset(big, 'p', sizeof(big));
        big[sizeof(big) - 1] = '\0';
        config_init_path(&cfg, big);
        check_int("I an over-long path is cut to 127",
                  (int)strlen(cfg.filepath), (int)sizeof(cfg.filepath) - 1);
    }

    /* ── J: the write path and its failures ───────────────────────────────── */
    printf("\nJ  save failures, the atomic rename, a directory as a path\n");
    config_init_path(&cfg, "/no_such_root_dir_zz/sub/x.conf");
    config_set(&cfg, "k", "v");
    check_int("J save on an unopenable path is -1", config_save(&cfg), -1);
    config_init_path(&cfg, FIX_SUB);
    config_set(&cfg, "k", "v");
    check_int("J save creates ONE missing level", config_save(&cfg), 0);
    config_init_path(&cfg, FIX_DEEP);
    config_set(&cfg, "k", "v");
    check_int("J but not two", config_save(&cfg), -1);
    {
        char tmp[8];
        FILE *f = file_write_atomic_open(FIX, tmp, sizeof(tmp));
        check(f == NULL, "J a truncating temp name is refused");
        check_int("J and the temp path is cleared", tmp[0], 0);
        if (f) fclose(f);
    }
    write_fix(FIX, "old=kept\n");
    {
        char tmp[160];
        FILE *f = file_write_atomic_open(FIX, tmp, sizeof(tmp));
        check(f != NULL, "J the temp file opens");
        if (f) {
            fprintf(f, "new=discarded\n");
            file_write_atomic_abort(f, tmp);
        }
        check_int("J an aborted write removes the temp", access(tmp, F_OK), -1);
        config_init_path(&re, FIX);
        rc = config_load(&re);
        check_int("J an aborted write loads as 0", rc, 0);
        check_str("J the original file is untouched",
                  config_get(&re, "old", "DEF"), "kept");
        check_str("J and the discarded key is absent",
                  config_get(&re, "new", "DEF"), "DEF");
    }
    /* A DIRECTORY reads as an empty config rather than an error: glibc's
     * fopen(dir, "r") succeeds and the first fgets() fails, so config_load()
     * returns 0 with no keys.  Pinned as measured behaviour — a caller reading 0
     * as "the file was there and is valid" silently gets every default. */
    if (mkdir(FIX_DIR, 0755) == 0 || access(FIX_DIR, F_OK) == 0) {
        config_init_path(&cfg, FIX_DIR);
        rc = config_load(&cfg);
        check_int("J a directory path loads as 0 (defect)", rc, 0);
        check_int("J with no keys at all", cfg.count, 0);
    } else {
        printf("  FAIL %-38s %s\n", "J cannot create the directory", FIX_DIR);
        fails++;
    }

    /* ── K: the sound-set path ───────────────────────────────────────────── */
    /* config_load_sound_set() is the only place C builds a path, and the set
     * files live on the device, so -1 here is the NORMAL outcome; what the host
     * can check is the path it asked for. */
    printf("\nK  config_load_sound_set builds the installed path\n");
    check_int("K an absent set is -1", config_load_sound_set(&cfg, "snake"), -1);
    check_str("K a tag names <dir>/<tag><ext>", cfg.filepath,
              CONFIG_SOUND_SET_DIR "/snake" CONFIG_SOUND_SET_EXT);
    (void)config_load_sound_set(&cfg, NULL);
    check_str("K a NULL tag reads 'default'", cfg.filepath,
              CONFIG_SOUND_SET_DIR "/default" CONFIG_SOUND_SET_EXT);
    (void)config_load_sound_set(&cfg, "");
    check_str("K an empty tag reads 'default'", cfg.filepath,
              CONFIG_SOUND_SET_DIR "/default" CONFIG_SOUND_SET_EXT);

    fixtures_remove();
    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
