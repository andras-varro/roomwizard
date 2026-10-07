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
 * D  the date -s string, and an invalid value formats to nothing;
 * E  rewriting PasswordAuthentication (absent, commented, duplicate, Match, CRLF);
 * F  the authorized_keys lockout guard.
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

    CHECK(sys_tz_count() >= 10, "zone list size %d", sys_tz_count());
    CHECK(strcmp(sys_tz_name(0), "UTC") == 0, "first zone UTC");
    CHECK(sys_tz_name(-1) == NULL && sys_tz_name(sys_tz_count()) == NULL, "name out of range");
    CHECK(sys_tz_from_link("/usr/share/zoneinfo/America/Chicago") == 2, "chicago index");
    CHECK(sys_tz_from_link("../usr/share/zoneinfo/Europe/Budapest") == 7, "relative target");
    CHECK(sys_tz_from_link("/usr/share/zoneinfo/UTC") == 0, "UTC target");
    CHECK(sys_tz_from_link("/usr/share/zoneinfo/America/Chicago2") == -1, "suffix is not a match");
    CHECK(sys_tz_from_link("/usr/share/zoneinfo/Pacific/Fiji") == -1, "unlisted zone");
    CHECK(sys_tz_from_link("/etc/foo") == -1 && sys_tz_from_link(NULL) == -1, "not a zoneinfo link");
    CHECK(sys_tz_step(0, -1) == sys_tz_count() - 1, "step back wraps");
    CHECK(sys_tz_step(sys_tz_count() - 1, +1) == 0, "step forward wraps");
    CHECK(sys_tz_step(2, +1) == 3 && sys_tz_step(2, -1) == 1, "plain steps");
    CHECK(sys_tz_step(-1, +1) == 0 && sys_tz_step(-1, -1) == sys_tz_count() - 1, "unlisted start");
    for (int i = 0; i < sys_tz_count(); i++) {
        char l[96];
        snprintf(l, sizeof(l), "/usr/share/zoneinfo/%s", sys_tz_name(i));
        CHECK(sys_tz_from_link(l) == i, "round trip %d", i);
    }

    /* E  rewriting PasswordAuthentication */
    {
        char o[512];
        int n;
        n = sys_ssh_set_password_auth("PermitRootLogin yes\nPasswordAuthentication yes\nPubkeyAuthentication yes\n", false, o, sizeof(o));
        CHECK(n > 0 && strcmp(o, "PermitRootLogin yes\nPasswordAuthentication no\nPubkeyAuthentication yes\n") == 0, "E1 active line replaced: [%s]", o);
        CHECK(n == (int)strlen(o), "E1 length");
        n = sys_ssh_set_password_auth("PermitRootLogin yes\n", false, o, sizeof(o));
        CHECK(n > 0 && strcmp(o, "PermitRootLogin yes\nPasswordAuthentication no\n") == 0, "E2 absent: appended: [%s]", o);
        n = sys_ssh_set_password_auth("PermitRootLogin yes", true, o, sizeof(o));
        CHECK(n > 0 && strcmp(o, "PermitRootLogin yes\nPasswordAuthentication yes\n") == 0, "E3 no trailing newline: [%s]", o);
        n = sys_ssh_set_password_auth("#PasswordAuthentication yes\nPort 22\n", false, o, sizeof(o));
        CHECK(n > 0 && strcmp(o, "#PasswordAuthentication yes\nPort 22\nPasswordAuthentication no\n") == 0, "E4 comment kept, directive added: [%s]", o);
        n = sys_ssh_set_password_auth("#PasswordAuthentication yes\nPasswordauthentication = yes\n", false, o, sizeof(o));
        CHECK(n > 0 && strcmp(o, "#PasswordAuthentication yes\nPasswordAuthentication no\n") == 0, "E5 case and '=' form replaced: [%s]", o);
        n = sys_ssh_set_password_auth("PasswordAuthentication yes\nPort 22\nPasswordAuthentication yes\n", false, o, sizeof(o));
        CHECK(n > 0 && strcmp(o, "PasswordAuthentication no\nPort 22\nPasswordAuthentication no\n") == 0, "E6 duplicates all rewritten: [%s]", o);
        n = sys_ssh_set_password_auth("Port 22\nMatch User bob\n  PasswordAuthentication yes\n", false, o, sizeof(o));
        CHECK(n > 0 && strcmp(o, "Port 22\nPasswordAuthentication no\nMatch User bob\n  PasswordAuthentication yes\n") == 0, "E7 absent before Match: inserted before it, Match body untouched: [%s]", o);
        n = sys_ssh_set_password_auth("PasswordAuthentication yes\nMatch User bob\n  PasswordAuthentication yes\n", false, o, sizeof(o));
        CHECK(n > 0 && strcmp(o, "PasswordAuthentication no\nMatch User bob\n  PasswordAuthentication yes\n") == 0, "E8 present before Match: only global edited: [%s]", o);
        n = sys_ssh_set_password_auth("Port 22\r\nPasswordAuthentication yes\r\n", false, o, sizeof(o));
        CHECK(n > 0 && strchr(o, '\r') == NULL && strstr(o, "PasswordAuthentication no\n") != NULL, "E9 CRLF in, LF out");
        n = sys_ssh_set_password_auth("", true, o, sizeof(o));
        CHECK(n > 0 && strcmp(o, "PasswordAuthentication yes\n") == 0, "E10 empty text: [%s]", o);
        n = sys_ssh_set_password_auth("PermitRootLogin yes\nPasswordAuthentication yes\n", false, o, 10);
        CHECK(n == -1, "E11 too small refuses, got %d", n);
        CHECK(sys_ssh_set_password_auth(NULL, true, o, sizeof(o)) == -1, "E12 NULL");
        /* round trip through the reader */
        n = sys_ssh_set_password_auth("PermitRootLogin yes\nPasswordAuthentication yes\n", false, o, sizeof(o));
        CHECK(n > 0 && sys_ssh_mode(o) == SSH_MODE_KEY_ONLY, "E13 reads back KEY ONLY");
        char o2[512];
        n = sys_ssh_set_password_auth(o, true, o2, sizeof(o2));
        CHECK(n > 0 && sys_ssh_mode(o2) == SSH_MODE_PASSWORD, "E14 and back to PASSWORD");
        CHECK(strstr(o2, "PermitRootLogin yes") != NULL, "E15 PermitRootLogin kept");
    }

    /* F  authorized_keys lockout guard */
    {
        const char *k = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFooBarBazQuxQuuxFooBar user@host\n";
        CHECK(sys_authkeys_plausible(k), "F1 ed25519 line");
        CHECK(sys_authkeys_plausible("ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQClWVzG0lyP6XVhk6KVBMm6\n"), "F2 rsa, no comment");
        CHECK(sys_authkeys_plausible("# note\n\n  \ncommand=\"/bin/true\",no-pty ecdsa-sha2-nistp256 AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTY= x\n"), "F3 options + ecdsa after comment");
        CHECK(sys_authkeys_plausible("sk-ssh-ed25519@openssh.com AAAAGnNrLXNzaC1lZDI1NTE5QG9wZW5zc2guY29tAAAA\r\n"), "F4 sk key, CRLF");
        CHECK(!sys_authkeys_plausible(""), "F5 empty");
        CHECK(!sys_authkeys_plausible("# ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQClWVzG0lyP6XVhk6KVBMm6\n"), "F6 commented key");
        CHECK(!sys_authkeys_plausible("ssh-rsa short\n"), "F7 blob too short");
        CHECK(!sys_authkeys_plausible("hello world this is not a key at all\n"), "F8 no key type");
        CHECK(!sys_authkeys_plausible("ssh-rsa\n"), "F9 type only");
        CHECK(!sys_authkeys_plausible(NULL), "F10 NULL");
    }

    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("sys_settings_test: all passed\n");
    return 0;
}
