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

#endif
