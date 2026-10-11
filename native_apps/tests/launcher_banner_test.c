/*
 * launcher_banner_test.c — touch calibration banner for the launcher.
 *
 * Tests the launcher_check_calibration() helper, which returns the banner
 * text to display when /etc/touch_calibration.conf is absent, or NULL when
 * it is present.
 *
 * Build and run (host gcc, from native_apps/):
 *   gcc -Wall -Wextra -Wno-unused-parameter -I. -o build/launcher_banner_test \
 *       tests/launcher_banner_test.c && \
 *   ./build/launcher_banner_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "app_launcher/launcher_banner.h"

static int failures = 0;
static int checks = 0;

static void check(bool cond, const char *what) {
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL: %s\n", what);
    } else {
        printf("  ok:   %s\n", what);
    }
}

int main(void) {
    printf("launcher_banner_test: touch calibration banner checks\n\n");

    /* Group A: present file should return NULL */
    {
        printf("Group A: present file\n");
        /* Create a temporary file */
        const char *tmpfile = "/tmp/test_calib_present.conf";
        int fd = open(tmpfile, O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) {
            close(fd);
            const char *result = launcher_check_calibration(tmpfile);
            check(result == NULL, "present file returns NULL");
            unlink(tmpfile);
        } else {
            printf("  SKIP: could not create temp file\n");
        }
    }

    /* Group B: absent file should return the banner text */
    {
        printf("Group B: absent file\n");
        const char *missing_file = "/tmp/nonexistent_calib_12345.conf";
        /* ensure it does not exist */
        unlink(missing_file);
        const char *result = launcher_check_calibration(missing_file);
        check(result != NULL, "absent file returns non-NULL text");
        if (result != NULL) {
            check(strstr(result, "NOT CALIBRATED") != NULL,
                  "text contains 'NOT CALIBRATED'");
            check(strstr(result, "CONTROL PANEL") != NULL,
                  "text contains 'CONTROL PANEL'");
        }
    }

    /* Group C: NULL path should return NULL (negative control) */
    {
        printf("Group C: NULL path (negative control)\n");
        const char *result = launcher_check_calibration(NULL);
        check(result == NULL, "NULL path returns NULL");
    }

    printf("\n");
    printf("Totals: %d checks, %d failures\n", checks, failures);
    return (failures == 0) ? 0 : 1;
}
