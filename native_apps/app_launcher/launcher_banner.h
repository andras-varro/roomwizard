/*
 * launcher_banner.h — check for missing touch calibration.
 *
 * Returns the banner text to display if calibration is missing,
 * or NULL if the calibration file exists.
 */

#ifndef LAUNCHER_BANNER_H
#define LAUNCHER_BANNER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

static inline const char *launcher_check_calibration(const char *calib_path) {
    if (calib_path == NULL) {
        return NULL;
    }

    FILE *f = fopen(calib_path, "r");
    if (f != NULL) {
        fclose(f);
        return NULL;  /* file exists, calibration present */
    }

    /* file does not exist, calibration missing */
    return "NOT CALIBRATED - SEE CONTROL PANEL";
}

#endif /* LAUNCHER_BANNER_H */
