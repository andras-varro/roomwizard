/*
 * Configuration Library — implementation.
 * See config.h for API documentation.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <libgen.h>

/* ── Internal helpers ───────────────────────────────────────────────────── */

/** Trim leading and trailing whitespace in-place. Returns pointer to trimmed start. */
static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/** Case-insensitive string compare. */
static int str_eq_nocase(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/** Find index of key in entries, or -1 if not found. */
static int find_key(const Config *cfg, const char *key) {
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0)
            return i;
    }
    return -1;
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

void config_init(Config *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->filepath, CONFIG_FILE_PATH, sizeof(cfg->filepath) - 1);
    cfg->count = 0;
}

void config_init_path(Config *cfg, const char *path) {
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->filepath, path, sizeof(cfg->filepath) - 1);
    cfg->count = 0;
}

/* ── File I/O ────────────────────────────────────────────────────────────── */

int config_load(Config *cfg) {
    cfg->count = 0;

    FILE *f = fopen(cfg->filepath, "r");
    if (!f) return -1;

    char line[CONFIG_KEY_LEN + CONFIG_VAL_LEN + 16];
    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char *trimmed = trim(line);

        /* Skip blank lines and comments */
        if (*trimmed == '\0' || *trimmed == '#')
            continue;

        /* Find '=' separator */
        char *eq = strchr(trimmed, '=');
        if (!eq) continue;

        /* Split into key and value */
        *eq = '\0';
        char *key = trim(trimmed);
        char *val = trim(eq + 1);

        if (*key == '\0') continue;

        /* Store entry */
        if (cfg->count < CONFIG_MAX_KEYS) {
            strncpy(cfg->entries[cfg->count].key, key, CONFIG_KEY_LEN - 1);
            cfg->entries[cfg->count].key[CONFIG_KEY_LEN - 1] = '\0';
            strncpy(cfg->entries[cfg->count].value, val, CONFIG_VAL_LEN - 1);
            cfg->entries[cfg->count].value[CONFIG_VAL_LEN - 1] = '\0';
            cfg->count++;
        }
    }

    fclose(f);
    return 0;
}

int config_save(const Config *cfg) {
    /* Create parent directory if needed */
    char tmp[128];
    strncpy(tmp, cfg->filepath, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *dir = dirname(tmp);
    mkdir(dir, 0755);   /* ok if already exists */

    FILE *f = fopen(cfg->filepath, "w");
    if (!f) return -1;

    fprintf(f, "# RoomWizard Configuration\n");
    fprintf(f, "# Auto-generated — edit with hardware_config tool\n\n");

    for (int i = 0; i < cfg->count; i++) {
        fprintf(f, "%s=%s\n", cfg->entries[i].key, cfg->entries[i].value);
    }

    fclose(f);
    return 0;
}

/* ── Getters ─────────────────────────────────────────────────────────────── */

const char *config_get(const Config *cfg, const char *key, const char *default_val) {
    int idx = find_key(cfg, key);
    if (idx < 0) return default_val;
    return cfg->entries[idx].value;
}

int config_get_int(const Config *cfg, const char *key, int default_val) {
    const char *val = config_get(cfg, key, NULL);
    if (!val) return default_val;

    /* Verify the string is actually numeric (optional leading '-', then digits) */
    const char *p = val;
    if (*p == '-' || *p == '+') p++;
    if (*p == '\0') return default_val;
    while (*p) {
        if (!isdigit((unsigned char)*p)) return default_val;
        p++;
    }

    return atoi(val);
}

bool config_get_bool(const Config *cfg, const char *key, bool default_val) {
    const char *val = config_get(cfg, key, NULL);
    if (!val) return default_val;

    if (str_eq_nocase(val, "1") ||
        str_eq_nocase(val, "true") ||
        str_eq_nocase(val, "yes") ||
        str_eq_nocase(val, "on")) {
        return true;
    }

    if (str_eq_nocase(val, "0") ||
        str_eq_nocase(val, "false") ||
        str_eq_nocase(val, "no") ||
        str_eq_nocase(val, "off")) {
        return false;
    }

    return default_val;
}

/* ── Setters ─────────────────────────────────────────────────────────────── */

void config_set(Config *cfg, const char *key, const char *value) {
    int idx = find_key(cfg, key);
    if (idx >= 0) {
        /* Update existing */
        strncpy(cfg->entries[idx].value, value, CONFIG_VAL_LEN - 1);
        cfg->entries[idx].value[CONFIG_VAL_LEN - 1] = '\0';
        return;
    }

    /* Append new entry — silently fail if full */
    if (cfg->count >= CONFIG_MAX_KEYS) return;

    strncpy(cfg->entries[cfg->count].key, key, CONFIG_KEY_LEN - 1);
    cfg->entries[cfg->count].key[CONFIG_KEY_LEN - 1] = '\0';
    strncpy(cfg->entries[cfg->count].value, value, CONFIG_VAL_LEN - 1);
    cfg->entries[cfg->count].value[CONFIG_VAL_LEN - 1] = '\0';
    cfg->count++;
}

void config_set_int(Config *cfg, const char *key, int value) {
    char buf[CONFIG_VAL_LEN];
    snprintf(buf, sizeof(buf), "%d", value);
    config_set(cfg, key, buf);
}

void config_set_bool(Config *cfg, const char *key, bool value) {
    config_set(cfg, key, value ? "1" : "0");
}

/* ── Removal ─────────────────────────────────────────────────────────────── */

bool config_remove(Config *cfg, const char *key) {
    int idx = find_key(cfg, key);
    if (idx < 0) return false;

    /* Shift remaining entries down */
    for (int i = idx; i < cfg->count - 1; i++) {
        cfg->entries[i] = cfg->entries[i + 1];
    }
    cfg->count--;
    return true;
}

void config_clear(Config *cfg) {
    cfg->count = 0;
}

/* ── Convenience helpers ─────────────────────────────────────────────────── */

bool config_audio_enabled(const Config *cfg) {
    return config_get_bool(cfg, "audio_enabled", true);
}

bool config_led_enabled(const Config *cfg) {
    return config_get_bool(cfg, "led_enabled", true);
}

int config_led_brightness(const Config *cfg) {
    return config_get_int(cfg, "led_brightness", 100);
}

int config_backlight_brightness(const Config *cfg) {
    return config_get_int(cfg, "backlight_brightness", 100);
}
