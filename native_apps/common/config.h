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

/* Check if LED effects are disabled. Reads "led_enabled" key (default: true). */
bool config_led_enabled(const Config *cfg);

/* Get LED brightness percentage. Reads "led_brightness" key (default: 100). */
int config_led_brightness(const Config *cfg);

/* Get backlight brightness percentage. Reads "backlight_brightness" key (default: 100). */
int config_backlight_brightness(const Config *cfg);

#endif /* CONFIG_H */
