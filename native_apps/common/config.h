/*
 * Configuration Library — central key/value store for RoomWizard native apps.
 *
 * Provides persistent settings via a plain-text file at CONFIG_FILE_PATH.
 * Format: one "key=value\n" per line.  Lines starting with '#' are comments.
 *
 * File: /opt/games/rw_config.conf
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stdio.h>      /* FILE — file_write_atomic_* */
#include <stddef.h>     /* size_t */

#define CONFIG_FILE_PATH  "/opt/games/rw_config.conf"
#define CONFIG_MAX_KEYS   32
#define CONFIG_KEY_LEN    64
#define CONFIG_VAL_LEN    64

typedef struct {
    char key[CONFIG_KEY_LEN];
    char value[CONFIG_VAL_LEN];
} ConfigEntry;

typedef struct {
    ConfigEntry entries[CONFIG_MAX_KEYS];
    int count;
    char filepath[128];
} Config;

/* Initialize config with default file path. Does NOT load from disk. */
void config_init(Config *cfg);

/* Initialize config with a custom file path. */
void config_init_path(Config *cfg, const char *path);

/* Load config from disk. Returns 0 on success, -1 on file missing/error. */
int config_load(Config *cfg);

/* Save config to disk. Creates parent dir if needed. Returns 0 on success. */
int config_save(const Config *cfg);

/* Get a string value by key. Returns the value or default_val if key not found. */
const char *config_get(const Config *cfg, const char *key, const char *default_val);

/* Get an integer value by key. Returns the value or default_val if key not found/invalid. */
int config_get_int(const Config *cfg, const char *key, int default_val);

/* Get a boolean value by key. Recognizes "1","true","yes","on" as true. Returns default_val if not found. */
bool config_get_bool(const Config *cfg, const char *key, bool default_val);

/* Set a string value. Adds the key if it doesn't exist, updates if it does. */
void config_set(Config *cfg, const char *key, const char *value);

/* Set an integer value. */
void config_set_int(Config *cfg, const char *key, int value);

/* Set a boolean value (stores "1" or "0"). */
void config_set_bool(Config *cfg, const char *key, bool value);

/* Remove a key. Returns true if key was found and removed. */
bool config_remove(Config *cfg, const char *key);

/* Remove all entries. */
void config_clear(Config *cfg);

/* ── Convenience helpers for common settings ─────────────────────────── */

/* Check if audio is disabled. Reads "audio_enabled" key (default: true). */
bool config_audio_enabled(const Config *cfg);

/* The two games-menu toggles, BELOW audio_enabled rather than beside it:
 * audio_enabled off means this process opens no device at all, so these two are
 * never consulted.  Both default true, so an old config file behaves as before.
 * Written by app_launcher's own screen; read by common/audio.c's audio_init()
 * and by nothing else (../IMPROVEMENT_PLAN.md F1 Phase 5 ⑤). */
bool config_music_enabled(const Config *cfg);
bool config_effects_enabled(const Config *cfg);

/* Check if LED effects are disabled. Reads "led_enabled" key (default: true). */
bool config_led_enabled(const Config *cfg);

/* Get LED brightness percentage. Reads "led_brightness" key (default: 100). */
int config_led_brightness(const Config *cfg);

/* Get backlight brightness percentage. Reads "backlight_brightness" key (default: 100). */
int config_backlight_brightness(const Config *cfg);

/* ── Atomic file writes ──────────────────────────────────────────────────
 *
 * fopen(path, "w") truncates before a single byte is written, and without an
 * fsync the new contents can still be in page cache when the power goes.  A
 * power cut mid-write therefore left rw_config.conf empty and silently reverted
 * every setting to its default.  These three calls replace that pattern:
 *
 *     char tmp[160];
 *     FILE *f = file_write_atomic_open(path, tmp, sizeof(tmp));
 *     if (!f) return -1;
 *     if (fprintf(f, ...) < 0) { file_write_atomic_abort(f, tmp); return -1; }
 *     return file_write_atomic_commit(f, tmp, path);
 *
 * The original file is untouched until the rename, so any failure leaves the
 * previous good contents in place.
 *
 * It lives here rather than in a new common/atomic_file.c so that no link line
 * in build-and-deploy.sh, vnc_client/Makefile or ScummVM's configure patch has
 * to gain an object for ten lines of code.  highscore.c picks it up by
 * including config.h; it already links config.o.
 */

/* Open "<path>.tmp" for writing and store that temp path in tmp_path.
 * Creates the parent directory if needed. Returns NULL on failure. */
FILE *file_write_atomic_open(const char *path, char *tmp_path, size_t tmp_sz);

/* fflush + fsync + fclose the temp file, rename it over path, then fsync the
 * parent directory so the rename itself survives a power cut.  Returns 0 on
 * success; on failure the temp file is removed and path is left as it was. */
int file_write_atomic_commit(FILE *f, const char *tmp_path, const char *path);

/* Give up: close the temp file and unlink it. path is left untouched. */
void file_write_atomic_abort(FILE *f, const char *tmp_path);

#endif /* CONFIG_H */
