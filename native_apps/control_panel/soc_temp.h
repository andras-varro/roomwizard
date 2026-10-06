/* soc_temp.h — the SoC temperature row on the Monitor page.
 *
 * Pure: the functions take the text of a sysfs file, never a path, so a host
 * test drives them with fixture strings.  monitor_page.c does the reading.
 *
 * The source is /sys/class/thermal/thermal_zone<N>/temp, millidegrees Celsius
 * as a decimal integer.  On this board it is the OMAP3 on-die bandgap
 * (ti-soc-thermal, "cpu_thermal" zone), which the kernel flags as unreliable:
 * a 7-bit ADC through a 1-2 degree table, untrimmed on this part.  It moves
 * with load, so it is shown as a trend figure, not a calibrated junction
 * temperature.  On a kernel without the driver the zone either does not exist
 * or its read fails, and the row is not drawn at all.
 */
#ifndef SOC_TEMP_H
#define SOC_TEMP_H

#include <stddef.h>

/* Readings outside this window are refused as not a temperature (a stuck or
 * misparsed value), so the row disappears instead of showing nonsense. */
#define SOC_TEMP_MIN_MC (-60000)
#define SOC_TEMP_MAX_MC 200000

/* A thermal zone's temp text: optional '-', digits, then only whitespace.
 * 0 with *mc set, or -1. */
int soc_temp_parse(const char *text, int *mc);

/* Whole degrees, rounded to nearest (half away from zero): "73 C", "-3 C". */
void soc_temp_format(int mc, char *buf, size_t len);

#endif
