/* dss_scale_ab - is the DSS hardware scaler good enough to replace a software
 * upscale?  An A/B for the eye, because no instrument on this device can answer it.
 *
 * DEVICE ONLY, and hidden from the launcher grid.  Build line:
 *
 *   $CC -O2 -static -I. tests/dss_scale_ab.c -o build/dss_scale_ab
 *
 * WHY THIS EXISTS.  ScummVM keeps the resolution the engine asks for - 320x200 for
 * most SCUMM titles - and scales it to the panel in SOFTWARE, nearest neighbour, in
 * blitGameSurfaceToFramebuffer().  800/320 is 2.5 and 480/200 is 2.4, both
 * non-integer, so that resample doubles some columns and triples others.  The DSS
 * vid1 overlay can do the same upscale in hardware with real filter taps, for no
 * CPU at all.  Whether the result LOOKS better, worse or the same is not derivable
 * from the driver source and cannot be screenshotted: cat /dev/fb0 returns the gfx
 * plane, never the composited panel.  So the comparison has to be put in front of a
 * person, and this is the instrument that does it.
 *
 * ⚠️ NOT an argument for shrinking a native game.  The seven games are authored at
 * 800x480, so a reduced surface DOWNSAMPLES their art; ScummVM is authored at
 * 320x200, so a reduced surface REMOVES a software upscale of art that was already
 * that size.  The two are opposite trades and only the second is what this measures.
 *
 * MODES
 *   soft            fb0 at 800x480 16bpp, the card resampled 320x200 -> 800x480 in
 *                   software, the same shape ScummVM uses (X lookup table, per-row
 *                   source index, identical-row dedup).  ⚠️ WRITES fb0 MODE - stop
 *                   the app first:  /etc/init.d/roomwizard-app stop
 *   hard            fb1 funded at 320x200 16bpp, card copied in 1:1, overlay1
 *                   (vid1) output_size 800x480, enabled.  Touches NEITHER fb0 nor
 *                   its mode: vid1 sits above gfx in the fixed z-order and covers
 *                   it, so the launcher may keep running underneath.
 *   split           BOTH AT ONCE, which is the mode the eye is actually good at.
 *                   The top half of the card only, at the same 2.5x by 2.4x in each
 *                   arm: vid1 upscales a 320x100 fb1 to 800x240 across the panel
 *                   top, and fb0 carries the software resample of the same 320x100
 *                   region in its bottom half.  ⚠️ WRITES fb0 MODE, as soft does.
 *   --swap          split only, and it is the CONTROL: the top and bottom of this
 *                   LCD are viewed at different angles, so "the top looked softer"
 *                   is not yet a statement about the scaler.  Swapping which half
 *                   is which separates the two.  It needs overlay1 position to be
 *                   writable; if it is not, this says so and exits rather than
 *                   reporting a comparison it did not actually make.
 *
 * ONE VARIABLE.  Same card, same source pixels, same 16bpp format, same ratios in
 * every mode; only WHO SCALES changes.  ⚠️ The framebuffer geometry necessarily
 * differs between the arms - a software upscale writes 800x480 and a hardware one
 * writes 320x200 - because that difference IS the mechanism, not a confound.  It
 * does mean the printed per-frame cost is a store-bandwidth figure as much as a
 * resample figure, which is the honest reading of it and why fb_plane_bench
 * measures store cost on its own.
 *
 * THE CARD IS SYNTHETIC ON PURPOSE.  Five bands, each aimed at one artefact class a
 * 2.5x upscale can produce: 1px and 2px column combs (uneven doubling, the worst
 * case for nearest neighbour at a fractional ratio), 1px row combs and 1px
 * diagonals, concentric 1px rings (moire and filter ringing), four smooth ramps
 * (banding, and whether the scaler dithers rather than replicates), and hard-edged
 * colour blocks (edge bleed).  A 1px white border rings the whole card, so a crop
 * is seen rather than deduced.
 *
 * ⚠️ No real game frame is fed in, and there is deliberately NO --ppm option.  The
 * only 320x200 source that would settle the question is a frame out of a running
 * engine, and ScummVM has no path that emits one.  A frame grabbed off fb0 today is
 * the 800x480 OUTPUT of the software scale, i.e. it is already arm A, so it cannot
 * serve as the shared input.  A loader added before that source exists would be an
 * option with nothing to point at.  When such a source appears, this is the file
 * that grows one.
 *
 * WHAT IT RESTORES, AND WHY THAT MATTERS.  The fb0 vinfo is saved verbatim before
 * any write and restored on every exit path including SIGINT/SIGTERM/SIGHUP, and a
 * vinfo that could not be saved refuses the run rather than reprogramming a mode
 * it cannot put back; overlay1 goes enabled=0 then fb1/size=0, then output_size
 * and position back to what they read before.  Those last two matter because they
 * are inherited: an unrestored position silently places the hardware arm where a
 * later run does not claim it is.  A unit left in a mode nothing running asked for
 * is the characteristic failure of this class of tool, and one reference unit is in
 * exactly that state today.  The final readback prints all four, so the restore is
 * verified rather than assumed.
 *
 * READ THE RECEIPT, NOT THE PANEL ALONE.  It prints the funded byte count and what
 * the driver read back (fb1/size rounds UP to a page, so the two differ — the
 * exact count is written for precisely that reason), the geometry each node
 * actually accepted AND a refusal if the driver granted anything else, every
 * overlay1 attribute before and after the write, and the per-frame microseconds
 * beside the pixel count they were spent on.
 * Two runs whose pixel counts are not in the ratio of their areas are not an A/B.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fb.h>

#define PANEL_W 800
#define PANEL_H 480
#define CARD_W  320
#define CARD_H  200

#define OVL "/sys/devices/platform/omapdss/overlay1"

/* -- the card ------------------------------------------------------------- */

static uint16_t card[CARD_W * CARD_H];

static uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void card_build(void) {
    const uint16_t W = rgb565(255, 255, 255), K = rgb565(0, 0, 0);

    for (int i = 0; i < CARD_W * CARD_H; i++) card[i] = K;

    /* band 0, rows 0-39: column combs, 1px on the left and 2px on the right.  At
     * 2.5x nearest neighbour cannot render either evenly, and the beat pattern
     * that results is the most legible artefact on the card. */
    for (int y = 0; y < 40; y++)
        for (int x = 0; x < CARD_W; x++) {
            int on = (x < CARD_W / 2) ? (x & 1) : ((x >> 1) & 1);
            card[y * CARD_W + x] = on ? W : K;
        }

    /* band 1, rows 40-79: 1px row comb on the left, which is the same test on the
     * 2.4x vertical axis, and 1px diagonals on the right for staircase and ringing. */
    for (int y = 40; y < 80; y++)
        for (int x = 0; x < CARD_W; x++) {
            int on = (x < CARD_W / 2) ? (y & 1) : (((x + y) & 3) == 0);
            card[y * CARD_W + x] = on ? W : K;
        }

    /* band 2, rows 80-119: concentric 1px rings.  Curves at a fractional ratio are
     * where a filter shows ringing and nearest neighbour shows moire.  dy is
     * tripled so the rings read as round in a band three times wider than tall. */
    for (int y = 80; y < 120; y++)
        for (int x = 0; x < CARD_W; x++) {
            int dx = x - CARD_W / 2, dy = (y - 100) * 3;
            int d = dx * dx + dy * dy;
            int r = 0;
            while ((r + 1) * (r + 1) * 36 < d) r++;      /* ring index, spacing 6 */
            card[y * CARD_W + x] = (r * r * 36 <= d && d < r * r * 36 + 200) ? W : K;
        }

    /* band 3, rows 120-159: four 10-row ramps - grey, red, green, blue.  Banding,
     * and whether the scaler dithers between source columns or replicates one. */
    for (int y = 120; y < 160; y++) {
        int which = (y - 120) / 10;
        for (int x = 0; x < CARD_W; x++) {
            int v = x * 255 / (CARD_W - 1);
            card[y * CARD_W + x] = which == 0 ? rgb565(v, v, v)
                                 : which == 1 ? rgb565(v, 0, 0)
                                 : which == 2 ? rgb565(0, v, 0)
                                              : rgb565(0, 0, v);
        }
    }

    /* band 4, rows 160-199: eight hard-edged blocks with 1px white separators.
     * Edge bleed, and a flat field to judge the filter against. */
    static const int blk[8][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0},
        {255, 0, 255}, {0, 255, 255}, {128, 128, 128}, {255, 255, 255}
    };
    for (int y = 160; y < CARD_H; y++)
        for (int x = 0; x < CARD_W; x++) {
            int b = x / 40;
            card[y * CARD_W + x] = (x % 40 == 0) ? W
                                 : rgb565(blk[b][0], blk[b][1], blk[b][2]);
        }

    /* 1px border, so a crop is seen and not deduced. */
    for (int x = 0; x < CARD_W; x++) {
        card[x] = W;
        card[(CARD_H - 1) * CARD_W + x] = W;
    }
    for (int y = 0; y < CARD_H; y++) {
        card[y * CARD_W] = W;
        card[y * CARD_W + CARD_W - 1] = W;
    }
}

/* -- restore state, held globally so a signal handler can reach it --------- */

static struct fb_var_screeninfo fb0_saved;
static int fb0_saved_ok = 0;
static int touched_fb0 = 0;
static int touched_ovl = 0;
/* output_size and position are ours to put back too.  Leaving them is what left
 * a stale output_size on a reference unit, and an unrestored position silently
 * misplaces the hardware arm on the NEXT run — hard and unswapped split used not
 * to write position at all, so they would have inherited a --swap run's 0,240
 * while announcing y=0. */
static char ovl_saved_out[64];
static char ovl_saved_pos[64];
static int ovl_saved_ok = 0;

static int write_attr(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    size_t want = strlen(val);
    ssize_t n = write(fd, val, want);
    /* close() can succeed and still reset errno, and all three callers print
     * strerror(errno) — so the reason has to be carried across it by hand or a
     * refused write reports "Success". */
    int err = (n < 0) ? errno : 0;
    close(fd);
    if (n < 0 || (size_t)n != want) {
        errno = (n < 0) ? err : EIO;   /* a short write is a failed write */
        return -1;
    }
    return 0;
}

static int read_attr(const char *path, char *buf, size_t len) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, len - 1);
    close(fd);
    if (n < 0) return -1;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == ' ')) n--;
    buf[n] = 0;
    return 0;
}

static void restore(void) {
    if (touched_ovl) {
        /* The documented undo, in the documented order. */
        write_attr(OVL "/enabled", "0");
        write_attr("/sys/class/graphics/fb1/size", "0");
        if (ovl_saved_ok) {
            write_attr(OVL "/output_size", ovl_saved_out);
            write_attr(OVL "/position", ovl_saved_pos);
        }
        touched_ovl = 0;
    }
    if (touched_fb0 && fb0_saved_ok) {
        int fd = open("/dev/fb0", O_RDWR);
        if (fd >= 0) {
            if (ioctl(fd, FBIOPUT_VSCREENINFO, &fb0_saved) < 0)
                perror("  RESTORE fb0 vinfo");
            close(fd);
        }
        touched_fb0 = 0;
    }
}

static void on_signal(int sig) {
    restore();
    fprintf(stderr, "\ndss_scale_ab: signal %d - state restored\n", sig);
    _exit(128 + sig);
}

/* -- framebuffer helpers -------------------------------------------------- */

/* Set one node to w by h at 16bpp RGB565.  The channel offsets are written
 * explicitly: asking for 16 alone lets the driver pick a layout, and
 * dss_scale_test.c writes RGB565 shorts into whatever fb0 happens to be, which on
 * a 32bpp fb0 is garbage rather than the red screen it announces. */
static int set_mode_565(int fd, int w, int h) {
    struct fb_var_screeninfo v;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0) {
        perror("FBIOGET_VSCREENINFO");
        return -1;
    }
    v.xres = v.xres_virtual = (uint32_t)w;
    v.yres = v.yres_virtual = (uint32_t)h;
    v.xoffset = v.yoffset = 0;
    v.bits_per_pixel = 16;
    v.red.offset = 11;   v.red.length = 5;
    v.green.offset = 5;  v.green.length = 6;
    v.blue.offset = 0;   v.blue.length = 5;
    v.transp.offset = 0; v.transp.length = 0;
    if (ioctl(fd, FBIOPUT_VSCREENINFO, &v) < 0) {
        perror("FBIOPUT_VSCREENINFO");
        return -1;
    }
    /* FBIOPUT_VSCREENINFO writes back what the driver ACTUALLY set, and it can
     * clamp the resolution or hand back a different bpp while still returning 0.
     * Discarding v would let a 32bpp grant read as success — and then every
     * RGB565 store below, and the stride_px = line_length / 2 that assumes 16bpp,
     * is wrong and the panel shows garbage the operator would read as a scaler
     * artefact.  This is the same defect class as dss_scale_test.c's unset bpp
     * (see the note above set_mode_565's channel offsets): explicit offsets fix
     * the layout half, and only a readback fixes the acceptance half. */
    if (v.xres != (uint32_t)w || v.yres != (uint32_t)h || v.bits_per_pixel != 16) {
        fprintf(stderr,
                "  REFUSING: asked for %dx%d @16bpp, driver granted %ux%u @ %u bpp\n"
                "  Every store below assumes 16bpp and a 2-byte stride, so the card\n"
                "  would be garbage and the eye run would be judging the garbage\n"
                "  rather than the scaler.\n",
                w, h, v.xres, v.yres, v.bits_per_pixel);
        return -1;
    }
    return 0;
}

static void report_mode(const char *what, int fd) {
    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0) return;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0) return;
    printf("  %-8s accepted %ux%u @ %u bpp  r%u/%u g%u/%u b%u/%u  stride %u\n",
           what, v.xres, v.yres, v.bits_per_pixel,
           v.red.length, v.red.offset, v.green.length, v.green.offset,
           v.blue.length, v.blue.offset, f.line_length);
}

/* One home for "map the whole visible plane".  Inline, this was three copies
 * that each ignored both ioctl return values and fed a possibly uninitialised
 * line_length straight to mmap as a length.  It also blacks the WHOLE mapping:
 * each frame writes only CARD_W*2 bytes per row, so a stride wider than that
 * leaves stale memory in the tail, and on the hardware arm that tail is upscaled
 * onto the panel where it reads as a scaler artefact rather than as our bug. */
static uint16_t *map_plane(const char *what, int fd, size_t *len_out,
                           int *stride_px) {
    struct fb_fix_screeninfo f;
    struct fb_var_screeninfo v;
    if (ioctl(fd, FBIOGET_FSCREENINFO, &f) < 0 ||
        ioctl(fd, FBIOGET_VSCREENINFO, &v) < 0) {
        fprintf(stderr, "  cannot read %s geometry: %s\n", what, strerror(errno));
        return NULL;
    }
    size_t len = (size_t)f.line_length * v.yres;
    uint16_t *p = mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "  mmap %s: %s\n", what, strerror(errno));
        return NULL;
    }
    memset(p, 0, len);
    *len_out = len;
    *stride_px = (int)(f.line_length / 2);
    return p;
}

/* ⚠️ 32-bit ARM: sizeof(long) == 4, so the seconds term is baselined on the first
 * call and never on epoch 0, which would overflow the multiply. */
static long long now_us(void) {
    static long long base_sec = -1;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    if (base_sec < 0) base_sec = (long long)ts.tv_sec;
    return ((long long)ts.tv_sec - base_sec) * 1000000LL + ts.tv_nsec / 1000;
}

/* -- the software arm: the same resample ScummVM does --------------------- */

/* Nearest neighbour with a precomputed X table and identical-row dedup, which is
 * what roomwizard-graphics.cpp does after its O2 and O8 optimisations.
 * Reproducing the SHAPE and not merely the result is the point: the number this
 * prints is meant to be comparable with what ScummVM pays today, so the
 * optimisations it already has must be present here too or the arm is a straw man. */
static long long soft_scale(uint16_t *dst, int dst_stride_px, int dst_x, int dst_y,
                            int out_w, int out_h,
                            const uint16_t *src, int src_w, int src_rows) {
    int *xtab = malloc(sizeof(int) * (size_t)out_w);
    if (!xtab) return -1;
    for (int dx = 0; dx < out_w; dx++) {
        int sx = dx * src_w / out_w;
        xtab[dx] = (sx < src_w) ? sx : src_w - 1;
    }

    long long t0 = now_us();
    int prev_src_y = -1;
    uint16_t *prev_row = NULL;
    for (int dy = 0; dy < out_h; dy++) {
        uint16_t *row = dst + (size_t)(dst_y + dy) * dst_stride_px + dst_x;
        int sy = dy * src_rows / out_h;
        if (sy >= src_rows) sy = src_rows - 1;
        if (sy == prev_src_y && prev_row) {
            memcpy(row, prev_row, (size_t)out_w * 2);   /* the dedup */
            continue;
        }
        const uint16_t *s = src + (size_t)sy * src_w;
        for (int dx = 0; dx < out_w; dx++) row[dx] = s[xtab[dx]];
        prev_src_y = sy;
        prev_row = row;
    }
    long long t1 = now_us();
    free(xtab);
    return t1 - t0;
}

/* -- overlay programming -------------------------------------------------- */

/* Fund fb1, set its geometry, point vid1 at the panel.  ⚠️ input_size is NOT
 * writable - Permission denied; the driver derives it from the framebuffer
 * geometry, so only output_size is ours to set.  fb1/size reads back page-rounded,
 * which is why the receipt prints the request and the readback side by side.
 * Returns an open fd on fb1, -1 on failure, or -2 when a requested position write
 * was refused. */
static int overlay_up(int src_w, int src_h, int out_w, int out_h,
                      int pos_y, int want_swap) {
    char buf[64], back[64];
    long bytes = (long)src_w * src_h * 2;

    /* Save what we are about to overwrite so restore() can put it back, and
     * print it: a stale value inherited from an earlier run is exactly the thing
     * that would invalidate this A/B without showing up anywhere. */
    if (read_attr(OVL "/output_size", ovl_saved_out, sizeof(ovl_saved_out)) == 0 &&
        read_attr(OVL "/position", ovl_saved_pos, sizeof(ovl_saved_pos)) == 0)
        ovl_saved_ok = 1;
    printf("  overlay1 before: output_size %s  position %s\n",
           ovl_saved_ok ? ovl_saved_out : "<unreadable>",
           ovl_saved_ok ? ovl_saved_pos : "<unreadable>");

    /* Write the EXACT byte count.  Rounding up to a page here first made the
     * request and the readback identical, so the receipt's page-rounding line
     * always described a no-op and could never show the driver's own rounding —
     * which is the one thing that line exists to record. */
    printf("  funding fb1: %ld bytes for %dx%d @16bpp\n", bytes, src_w, src_h);
    snprintf(buf, sizeof(buf), "%ld", bytes);
    if (write_attr("/sys/class/graphics/fb1/size", buf) < 0) {
        fprintf(stderr, "  cannot write fb1/size: %s\n", strerror(errno));
        return -1;
    }
    touched_ovl = 1;
    if (read_attr("/sys/class/graphics/fb1/size", back, sizeof(back)) == 0)
        printf("  fb1/size: wrote %ld, driver reads %s   (page rounding, not a\n"
               "    short write - the driver rounds up to a whole page)\n", bytes, back);

    int fd = open("/dev/fb1", O_RDWR);
    if (fd < 0) {
        perror("  open /dev/fb1");
        return -1;
    }
    if (set_mode_565(fd, src_w, src_h) < 0) {
        close(fd);
        return -1;
    }
    report_mode("fb1", fd);

    /* Position BEFORE enable, so a swap that cannot happen is found before the
     * panel shows a comparison the operator would read as the swapped one.
     * Written UNCONDITIONALLY, including the pos_y == 0 case: guarding it on
     * "pos_y != 0 || want_swap" meant hard and unswapped split never wrote it,
     * so either would silently inherit a previous --swap run's 0,240 and place
     * the hardware arm on the bottom half while announcing y=0. */
    snprintf(buf, sizeof(buf), "0,%d", pos_y);
    if (write_attr(OVL "/position", buf) < 0) {
        fprintf(stderr, "  overlay1/position is NOT writable (%s)\n",
                strerror(errno));
        if (want_swap) {
            fprintf(stderr, "  --swap cannot be honoured, and printing the unswapped\n"
                            "  comparison as though it were swapped would be a false\n"
                            "  control - refusing rather than reporting it.\n");
            close(fd);
            return -2;
        }
        /* Not swapping: 0,0 may already be the value, in which case the arm is
         * where we say it is.  Anything else and the arm is misplaced by an
         * amount we cannot correct, which is not an A/B either. */
        if (read_attr(OVL "/position", back, sizeof(back)) != 0 ||
            strcmp(back, "0,0") != 0) {
            fprintf(stderr, "  position reads %s and cannot be set to 0,0, so the\n"
                            "  hardware arm is not where this run would claim it is -\n"
                            "  refusing rather than reporting a misplaced comparison.\n",
                    back);
            close(fd);
            return -2;
        }
        fprintf(stderr, "  ...but it already reads 0,0, which is what this mode needs.\n");
    }
    if (read_attr(OVL "/position", back, sizeof(back)) == 0)
        printf("  overlay1/position: %s\n", back);

    snprintf(buf, sizeof(buf), "%d,%d", out_w, out_h);
    if (write_attr(OVL "/output_size", buf) < 0) {
        fprintf(stderr, "  cannot write overlay1/output_size: %s\n", strerror(errno));
        close(fd);
        return -1;
    }
    if (write_attr(OVL "/enabled", "1") < 0) {
        fprintf(stderr, "  cannot enable overlay1: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    static const char *const names[] = {
        "name", "enabled", "input_size", "output_size",
        "position", "zorder", "global_alpha", "screen_width"
    };
    printf("  overlay1 after the write:\n");
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char p[160];
        snprintf(p, sizeof(p), OVL "/%s", names[i]);
        if (read_attr(p, back, sizeof(back)) == 0)
            printf("    %-13s %s\n", names[i], back);
        else
            printf("    %-13s <unreadable>\n", names[i]);
    }
    return fd;
}

/* -- modes ---------------------------------------------------------------- */

static int mode_soft(int frames) {
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        perror("open /dev/fb0");
        return 1;
    }

    if (ioctl(fd, FBIOGET_VSCREENINFO, &fb0_saved) < 0) {
        perror("FBIOGET_VSCREENINFO fb0");
        fprintf(stderr, "  refusing to reprogram a mode we could not save first -\n"
                        "  that is exactly how a unit is left in a mode nothing\n"
                        "  running asked for.\n");
        close(fd);
        return 1;
    }
    fb0_saved_ok = 1;
    printf("  fb0 was %ux%u @ %u bpp - saved for restore\n",
           fb0_saved.xres, fb0_saved.yres, fb0_saved.bits_per_pixel);

    touched_fb0 = 1;
    if (set_mode_565(fd, PANEL_W, PANEL_H) < 0) {
        close(fd);
        return 1;
    }
    report_mode("fb0", fd);

    size_t len;
    int stride_px;
    uint16_t *fb = map_plane("fb0", fd, &len, &stride_px);
    if (!fb) {
        close(fd);
        return 1;
    }

    long long total = 0;
    for (int i = 0; i < frames; i++) {
        long long us = soft_scale(fb, stride_px, 0, 0, PANEL_W, PANEL_H,
                                  card, CARD_W, CARD_H);
        if (us < 0) {
            munmap(fb, len);
            close(fd);
            return 1;
        }
        total += us;
    }
    printf("\n  SOFTWARE resample %dx%d -> %dx%d, %d frames\n",
           CARD_W, CARD_H, PANEL_W, PANEL_H, frames);
    printf("    %lld us/frame over %d written pixels   (the ScummVM path today)\n",
           total / frames, PANEL_W * PANEL_H);

    munmap(fb, len);
    close(fd);
    return 0;
}

static int mode_hard(int frames) {
    int fd = overlay_up(CARD_W, CARD_H, PANEL_W, PANEL_H, 0, 0);
    if (fd < 0) return 1;

    size_t len;
    int stride_px;
    uint16_t *fb = map_plane("fb1", fd, &len, &stride_px);
    if (!fb) {
        close(fd);
        return 1;
    }

    long long total = 0;
    for (int i = 0; i < frames; i++) {
        long long t0 = now_us();
        for (int y = 0; y < CARD_H; y++)
            memcpy(fb + (size_t)y * stride_px, card + (size_t)y * CARD_W,
                   (size_t)CARD_W * 2);
        total += now_us() - t0;
    }
    printf("\n  HARDWARE upscale %dx%d -> %dx%d by vid1, %d frames\n",
           CARD_W, CARD_H, PANEL_W, PANEL_H, frames);
    printf("    %lld us/frame over %d written pixels   (a straight copy - the scale\n"
           "    itself costs no CPU at all)\n", total / frames, CARD_W * CARD_H);

    munmap(fb, len);
    close(fd);
    return 0;
}

/* Both arms on the panel at once, same ratios, one variable.  The TOP HALF of the
 * card only: a 320x100 source to 800x240 is 2.5x by 2.4x, which is exactly the
 * full card ratio pair.  Shrinking the output without shrinking the input would
 * have compared two different scale factors and answered nothing. */
static int mode_split(int frames, int swap) {
    int half_h = CARD_H / 2;           /* 100 source rows */
    int out_h = PANEL_H / 2;           /* 240 panel rows  */
    int hw_y = swap ? out_h : 0;       /* where the HARDWARE arm sits */
    int sw_y = swap ? 0 : out_h;       /* where the SOFTWARE arm sits */

    printf("  hardware arm at panel y=%d, software arm at panel y=%d%s\n",
           hw_y, sw_y, swap ? "   (SWAPPED - the viewing-angle control)" : "");

    int fd0 = open("/dev/fb0", O_RDWR);
    if (fd0 < 0) {
        perror("open /dev/fb0");
        return 1;
    }
    if (ioctl(fd0, FBIOGET_VSCREENINFO, &fb0_saved) < 0) {
        perror("FBIOGET_VSCREENINFO fb0");
        fprintf(stderr, "  refusing to reprogram a mode we could not save first.\n");
        close(fd0);
        return 1;
    }
    fb0_saved_ok = 1;
    printf("  fb0 was %ux%u @ %u bpp - saved for restore\n",
           fb0_saved.xres, fb0_saved.yres, fb0_saved.bits_per_pixel);

    /* The overlay first: if --swap cannot be honoured we must not already have
     * changed the mode of fb0 for a comparison we are about to refuse to make. */
    int fd1 = overlay_up(CARD_W, half_h, PANEL_W, out_h, hw_y, swap);
    if (fd1 < 0) {
        close(fd0);
        return (fd1 == -2) ? 2 : 1;
    }

    touched_fb0 = 1;
    if (set_mode_565(fd0, PANEL_W, PANEL_H) < 0) {
        close(fd1);
        close(fd0);
        return 1;
    }
    report_mode("fb0", fd0);

    size_t len0, len1;
    int s0, s1;
    uint16_t *fb0 = map_plane("fb0", fd0, &len0, &s0);
    uint16_t *fb1 = fb0 ? map_plane("fb1", fd1, &len1, &s1) : NULL;
    if (!fb0 || !fb1) {
        if (fb0) munmap(fb0, len0);
        close(fd1);
        close(fd0);
        return 1;
    }

    long long soft_total = 0, hard_total = 0;
    int done = 0;
    for (int i = 0; i < frames; i++) {
        long long us = soft_scale(fb0, s0, 0, sw_y, PANEL_W, out_h,
                                  card, CARD_W, half_h);
        if (us < 0) break;
        soft_total += us;

        long long t0 = now_us();
        for (int y = 0; y < half_h; y++)
            memcpy(fb1 + (size_t)y * s1, card + (size_t)y * CARD_W,
                   (size_t)CARD_W * 2);
        hard_total += now_us() - t0;
        done++;
    }
    /* Divide by what actually ran.  Dividing by the requested count after a
     * mid-loop break printed a fabricated low us/frame with no warning, which is
     * worse than printing nothing: mode_soft bails on the same condition. */
    if (done == 0) {
        fprintf(stderr, "  no frame completed - nothing to report\n");
        munmap(fb1, len1);
        munmap(fb0, len0);
        close(fd1);
        close(fd0);
        return 1;
    }

    printf("\n  SPLIT, %d frames, source region %dx%d in BOTH arms\n",
           done, CARD_W, half_h);
    printf("    software  %5lld us/frame over %d written pixels\n",
           soft_total / done, PANEL_W * out_h);
    printf("    hardware  %5lld us/frame over %d written pixels\n",
           hard_total / done, CARD_W * half_h);
    printf("    Both arms scale by the same 2.5x by 2.4x.  The pixel counts differ\n"
           "    BECAUSE that is the mechanism, and their ratio is the area ratio.\n");

    munmap(fb1, len1);
    munmap(fb0, len0);
    close(fd1);
    close(fd0);
    return 0;
}

/* -- main ----------------------------------------------------------------- */

static void usage_to(FILE *out) {
    fprintf(out,
        "dss_scale_ab <soft|hard|split> [hold-seconds] [--swap] [--frames N]\n"
        "\n"
        "  soft    software nearest-neighbour 320x200 -> 800x480 on fb0 (WRITES fb0 MODE)\n"
        "  hard    vid1 hardware upscale of a 320x200 fb1              (fb0 untouched)\n"
        "  split   both at once, top half of the card, same ratios     (WRITES fb0 MODE)\n"
        "  --swap  split only: hardware arm at the BOTTOM, the viewing-angle control\n"
        "\n"
        "  Defaults: hold 20 s, 60 timed frames.\n"
        "\n"
        "  soft and split change the mode of fb0, so stop the app first:\n"
        "      /etc/init.d/roomwizard-app stop\n"
        "  Everything is restored on exit and on Ctrl-C, and the restore is printed.\n"
        "  \xe2\x9a\xa0 No screenshot can see the hardware arm: cat /dev/fb0 returns the gfx\n"
        "  plane, never the composited panel.  This needs an eye at the panel.\n");
}

static void usage(void) { usage_to(stderr); }

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    const char *mode = argv[1];
    if (!strcmp(mode, "--help") || !strcmp(mode, "-h")) {
        usage_to(stdout);
        return 0;
    }
    int hold = 20, frames = 60, swap = 0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--swap")) {
            swap = 1;
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            hold = atoi(argv[i]);
        } else {
            usage();
            return 1;
        }
    }
    if (frames < 1) frames = 1;
    if (hold < 0) hold = 0;

    /* Validate BEFORE the banner: printing "mode=banana" and the card line to
     * stdout and only then failing reads like a run that started. */
    if (strcmp(mode, "soft") && strcmp(mode, "hard") && strcmp(mode, "split")) {
        fprintf(stderr, "dss_scale_ab: unknown mode '%s'\n\n", mode);
        usage();
        return 1;
    }
    /* --swap means something in split alone.  Accepting it elsewhere put
     * "swap=1" in the banner for a control that was never applied — the same
     * false control this tool refuses to print when position is unwritable. */
    if (swap && strcmp(mode, "split")) {
        fprintf(stderr, "dss_scale_ab: --swap applies to split only, and %s would\n"
                        "  announce it in the banner while applying nothing.\n", mode);
        return 1;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    atexit(restore);

    card_build();
    printf("dss_scale_ab: mode=%s hold=%ds frames=%d%s\n",
           mode, hold, frames, swap ? " swap=1" : "");
    printf("  card %dx%d 16bpp: column combs / row combs + diagonals / rings /\n"
           "  ramps / hard blocks, 1px white border\n", CARD_W, CARD_H);

    int rc;
    if (!strcmp(mode, "soft")) {
        rc = mode_soft(frames);
    } else if (!strcmp(mode, "hard")) {
        rc = mode_hard(frames);
    } else if (!strcmp(mode, "split")) {
        rc = mode_split(frames, swap);
    } else {
        usage();
        return 1;
    }
    if (rc == 0 && hold > 0) {
        printf("\n  holding %d s - LOOK AT THE PANEL.  Ctrl-C restores early.\n", hold);
        fflush(stdout);
        sleep((unsigned)hold);
    }

    restore();

    /* The restore is printed, not assumed: a unit left in a mode nothing asked for
     * is the characteristic failure of this whole class of tool. */
    char back[64];
    printf("\n  after restore:\n");
    if (read_attr("/sys/class/graphics/fb1/size", back, sizeof(back)) == 0)
        printf("    fb1/size         %s\n", back);
    if (read_attr(OVL "/enabled", back, sizeof(back)) == 0)
        printf("    overlay1 enabled %s\n", back);
    /* These two are the ones the old undo left behind, so they belong in the
     * receipt that claims the undo happened. */
    if (read_attr(OVL "/output_size", back, sizeof(back)) == 0)
        printf("    output_size      %s\n", back);
    if (read_attr(OVL "/position", back, sizeof(back)) == 0)
        printf("    position         %s\n", back);
    int fd = open("/dev/fb0", O_RDONLY);
    if (fd >= 0) {
        report_mode("fb0", fd);
        close(fd);
    }

    return rc;
}
