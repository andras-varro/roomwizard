/* Host regression for the System page's pure logic (control_panel/sys_settings.c).
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -o build/sys_settings_test \
 *       tests/sys_settings_test.c control_panel/sys_settings.c && ./build/sys_settings_test
 *
 * A  sshd_config text -> mode: the two provisioning groups' pairs, comments,
 *    '=' and mixed case, first occurrence wins, Match ends the global section,
 *    absent/hybrid/NULL are UNKNOWN;
 * B  days in month, including the century leap rules;
 * C  field stepping: wraps, the day clamp after a month or year change, the
 *    year limits;
 * D  the date -s string, and an invalid value formats to nothing.
 */
#include "control_panel/sys_settings.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void mode(const char *text, SshMode want, const char *what) {
    SshMode got = sys_ssh_mode(text);
    CHECK(got == want, "%s: mode %d, want %d", what, (int)got, (int)want);
}

int main(void) {
    mode("PermitRootLogin prohibit-password\nPasswordAuthentication no\n", SSH_MODE_KEY_ONLY, "key group");
    mode("PasswordAuthentication yes\nPermitRootLogin yes\n", SSH_MODE_PASSWORD, "password group");
    mode("#PasswordAuthentication no\nPasswordAuthentication yes\nPermitRootLogin yes\n", SSH_MODE_PASSWORD, "comment skipped");
    mode("  passwordauthentication=NO\r\n", SSH_MODE_KEY_ONLY, "case, '=', CRLF, indent");
    mode("PasswordAuthentication no\nPasswordAuthentication yes\n", SSH_MODE_KEY_ONLY, "first occurrence wins");
    mode("Port 22\nMatch User x\nPasswordAuthentication no\n", SSH_MODE_UNKNOWN, "Match ends the global section");
    mode("PasswordAuthentication yes\n", SSH_MODE_UNKNOWN, "password without root yes");
    mode("PasswordAuthentication yes\nPermitRootLogin prohibit-password\n", SSH_MODE_UNKNOWN, "hybrid");
    mode("PermitRootLogin yes\n", SSH_MODE_UNKNOWN, "password directive absent");
    mode("", SSH_MODE_UNKNOWN, "empty");
    mode(NULL, SSH_MODE_UNKNOWN, "NULL");

    CHECK(sys_days_in_month(2024, 2) == 29, "2024 leap");
    CHECK(sys_days_in_month(2025, 2) == 28, "2025 not leap");
    CHECK(sys_days_in_month(2100, 2) == 28, "2100 not leap");
    CHECK(sys_days_in_month(2000, 2) == 29, "2000 leap");
    CHECK(sys_days_in_month(2026, 4) == 30 && sys_days_in_month(2026, 12) == 31, "30/31");
    CHECK(sys_days_in_month(2026, 0) == 0 && sys_days_in_month(2026, 13) == 0, "bad month");

    SysDateTime d = { 2026, 1, 31, 23, 59 };
    sys_dt_step(&d, SYS_DT_MON, +1);
    CHECK(d.mon == 2 && d.day == 28, "Jan 31 + month -> Feb 28, got %d-%d", d.mon, d.day);
    d = (SysDateTime){ 2028, 3, 31, 0, 0 };
    sys_dt_step(&d, SYS_DT_MON, -1);
    CHECK(d.mon == 2 && d.day == 29, "leap Mar 31 - month -> Feb 29");
    d = (SysDateTime){ 2028, 2, 29, 0, 0 };
    sys_dt_step(&d, SYS_DT_YEAR, +1);
    CHECK(d.year == 2029 && d.day == 28, "Feb 29 + year -> Feb 28");
    d = (SysDateTime){ 2026, 12, 1, 0, 0 };
    sys_dt_step(&d, SYS_DT_MON, +1);
    CHECK(d.mon == 1, "month wraps");
    d = (SysDateTime){ 2026, 2, 1, 0, 0 };
    sys_dt_step(&d, SYS_DT_DAY, -1);
    CHECK(d.day == 28, "day wraps to month end");
    d = (SysDateTime){ 2026, 2, 28, 23, 59 };
    sys_dt_step(&d, SYS_DT_HOUR, +1); sys_dt_step(&d, SYS_DT_MIN, +1);
    CHECK(d.hour == 0 && d.min == 0 && d.day == 28, "hour and minute wrap, day untouched");
    d.year = SYS_YEAR_MAX; sys_dt_step(&d, SYS_DT_YEAR, +1);
    CHECK(d.year == SYS_YEAR_MAX, "year clamps high");
    d.year = SYS_YEAR_MIN; sys_dt_step(&d, SYS_DT_YEAR, -1);
    CHECK(d.year == SYS_YEAR_MIN, "year clamps low");

    char b[32];
    d = (SysDateTime){ 2026, 10, 6, 9, 5 };
    sys_dt_format(&d, b, sizeof(b));
    CHECK(strcmp(b, "2026-10-06 09:05:00") == 0, "format \"%s\"", b);
    d = (SysDateTime){ 2026, 11, 31, 9, 5 };
    sys_dt_format(&d, b, sizeof(b));
    CHECK(b[0] == '\0', "invalid formats to nothing");

    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("sys_settings_test: all passed\n");
    return 0;
}
