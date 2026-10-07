/* sys_settings.h — the System page's pure logic: the SSH mode read out of
 * sshd_config text, and the date/time editor's field arithmetic.
 *
 * Pure: functions take text or a struct, never a path, so a host test drives
 * them (tests/sys_settings_test.c).  system_page.c does the reading and the
 * setting.
 */
#ifndef SYS_SETTINGS_H
#define SYS_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SSH_MODE_UNKNOWN,    /* absent, a hybrid, or not understood */
    SSH_MODE_KEY_ONLY,   /* PasswordAuthentication no */
    SSH_MODE_PASSWORD    /* PasswordAuthentication yes and PermitRootLogin yes */
} SshMode;

/* The mode an OpenSSH sshd_config text sets, by the two directives the
 * provisioning groups write.  As sshd reads it: keywords and values are
 * case-insensitive, `Keyword value` or `Keyword=value`, '#' lines are
 * comments, the FIRST occurrence of a keyword wins, and a Match block ends
 * the global section.  A directive that is absent does not count as a mode:
 * PASSWORD needs both PasswordAuthentication yes and PermitRootLogin yes (the
 * password group's pair); KEY_ONLY needs PasswordAuthentication no.  Anything
 * else, including NULL, is UNKNOWN. */
SshMode sys_ssh_mode(const char *sshd_config_text);

/* Rewrite sshd_config text so PasswordAuthentication is `yes` or `no`.
 * Writes the new text to out (NUL-terminated, LF only: any CR is dropped) and
 * returns its length, or -1 when out is too small or an argument is NULL.
 * Policy, from sshd's first-match-wins: every ACTIVE PasswordAuthentication
 * line in the global section (before the first Match) is rewritten, so no
 * stale duplicate can still win; commented lines and everything else are
 * copied unchanged, as is everything from the first Match on.  If no active
 * line exists the directive is inserted just BEFORE the first Match line (an
 * append would land inside the Match block), else appended at the end.
 * PubkeyAuthentication and PermitRootLogin are never touched. */
int sys_ssh_set_password_auth(const char *text, bool password_yes,
                              char *out, size_t out_len);

/* True when authorized_keys text holds at least one plausible public-key
 * line: not blank, not '#', with a key-type token (ssh-*, ecdsa-sha2-*, sk-*)
 * followed by a base64 blob of 20+ characters.  The lockout guard for KEY
 * ONLY: it cannot prove the key is the operator's, only that one exists. */
bool sys_authkeys_plausible(const char *text);

typedef struct { int year, mon, day, hour, min; } SysDateTime;
enum { SYS_DT_YEAR, SYS_DT_MON, SYS_DT_DAY, SYS_DT_HOUR, SYS_DT_MIN, SYS_DT_FIELDS };

#define SYS_YEAR_MIN 2024
#define SYS_YEAR_MAX 2099

/* 28..31 for month 1..12 of year y (Gregorian leap rule); 0 for a bad month. */
int sys_days_in_month(int year, int mon);

/* Step one field by dir (-1 or +1).  Month, hour, minute and day wrap; year
 * clamps to SYS_YEAR_MIN..MAX.  After any step the day is clamped into the
 * (possibly shorter) month, so the result is always valid. */
void sys_dt_step(SysDateTime *dt, int field, int dir);

bool sys_dt_valid(const SysDateTime *dt);

/* "YYYY-MM-DD HH:MM:00" — the argument `date -s` takes.  Empty if invalid. */
void sys_dt_format(const SysDateTime *dt, char *out, size_t len);


/* The curated timezone list the System page cycles through (names are
 * zoneinfo paths below /usr/share/zoneinfo). */
int sys_tz_count(void);
const char *sys_tz_name(int idx);                 /* NULL out of range */

/* Index of the zone a /etc/localtime link target names (anything ending
 * "zoneinfo/<zone>", absolute or relative), or -1 if it is not in the list or
 * the target is NULL. */
int sys_tz_from_link(const char *target);

/* Next (dir > 0) or previous (dir < 0) index, wrapping.  From -1 (zone not
 * in the list) a step lands on the first (+) or last (-) entry. */
int sys_tz_step(int idx, int dir);


#endif
