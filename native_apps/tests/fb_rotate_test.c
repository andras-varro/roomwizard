/* Host-side regression for fb_swap()'s portrait rotation.
 *
 * Runs on the DEV MACHINE with native gcc.  fb_swap() reads only Framebuffer
 * fields, so a synthetic Framebuffer whose `buffer` is a malloc'd stand-in for
 * the /dev/fb0 mapping drives the REAL function with no device in the loop.
 * Build and run:
 *
 *   cd native_apps && gcc -O2 -Wall -Wextra -Wno-unused-parameter -I common \
 *       -o build/fb_rotate_test tests/fb_rotate_test.c \
 *       common/framebuffer.c common/hardware.c common/config.c -lm \
 *       && ./build/fb_rotate_test
 *
 * What it asserts: the rotated copy was rewritten so that the panel side is
 * written in ascending address runs (it used to write down a physical column,
 * one store per line_length).  The output must not change by one byte.  So the
 * pre-rewrite loop is kept below VERBATIM as ref_rotate(), and both are run on
 * the same random frame into two panel buffers pre-filled with the same
 * sentinel; the WHOLE panel buffer is compared, so a store outside the visible
 * rectangle (a bezel band, the line padding) fails exactly like a wrong pixel.
 *
 * Geometries: the one RW09 logs in portrait ("453x800 logical at (13,0) on a
 * 480x800 panel [portrait], 32 bpp" — physical fb 800x480, bezel 13 left / 14
 * right in the rotated frame), at both bpp because ScummVM and vnc_client run
 * 16bpp and take this path too when /opt/games/portrait.mode exists; a padded
 * line_length; a non-zero view_y; heights that are below, equal to, and not a
 * multiple of the 64-row band; and the degenerate 1x1.
 *
 * The timing lines it prints are a HOST micro-benchmark: a cached heap buffer
 * on an x86 core, nothing like the write-combined mapping on a Cortex-A8.  They
 * show the new loop is not slower in itself; they say nothing about the device.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "framebuffer.h"

/* The rotation as it stood before the rewrite, copied verbatim from fb_swap(). */
static void ref_rotate(Framebuffer *fb) {
        const uint32_t bpp = fb->bytes_per_pixel;
        const bool is16 = (fb->bytes_per_pixel == 2);
        uint32_t lw = fb->width;
        uint32_t lh = fb->height;
        uint32_t ph_minus_1 = fb->phys_height - 1;
        const uint8_t *src = (const uint8_t *)fb->back_buffer;
        uint8_t *dst = (uint8_t *)fb->buffer;

        for (uint32_t ly = 0; ly < lh; ly++) {
            const uint8_t *src_row = src + (size_t)ly * lw * bpp;
            uint32_t px = ly + fb->view_y;   // Physical X = virtual Y
            for (uint32_t lx = 0; lx < lw; lx++) {
                uint32_t py = ph_minus_1 - (lx + fb->view_x);
                uint8_t *d = dst + (size_t)py * fb->line_length + (size_t)px * bpp;
                if (is16) *(uint16_t *)d = ((const uint16_t *)src_row)[lx];
                else      *(uint32_t *)d = ((const uint32_t *)src_row)[lx];
            }
        }
}

typedef struct {
    const char *name;
    uint32_t phys_w, phys_h;   /* the physical fb (xres, yres) */
    uint32_t lw, lh;           /* logical surface, rotated frame */
    int vx, vy;                /* viewport origin, rotated frame */
    uint32_t pad;              /* extra bytes per physical line */
} Geo;

static const Geo GEOS[] = {
    { "RW09 portrait 453x800 at (13,0)", 800, 480, 453, 800, 13, 0, 0 },
    { "no bezel 480x800",                800, 480, 480, 800,  0, 0, 0 },
    { "padded line, view_y=7",           800, 480, 453, 786, 13, 7, 64 },
    { "height below one band",           100,  60,  37,  41,  5, 9, 0 },
    { "height exactly one band",         100,  60,  37,  64,  5, 3, 0 },
    { "height 2 bands + 1",              200,  60,  40, 129, 11, 2, 12 },
    { "odd width, odd offsets",          130,  77,  71, 123,  3, 5, 4 },
    { "1x1",                              10,  10,   1,   1,  4, 6, 0 },
};

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int fails = 0;

static void setup(Framebuffer *fb, const Geo *g, uint32_t bpp, void *panel, void *back) {
    memset(fb, 0, sizeof *fb);
    fb->portrait_mode = true;
    fb->double_buffering = true;
    fb->phys_width = g->phys_w;
    fb->phys_height = g->phys_h;
    fb->width = g->lw;
    fb->height = g->lh;
    fb->view_x = g->vx;
    fb->view_y = g->vy;
    fb->bytes_per_pixel = bpp;
    fb->line_length = g->phys_w * bpp + g->pad;
    fb->screen_size = (size_t)fb->line_length * g->phys_h;
    fb->back_buffer_size = (size_t)g->lw * g->lh * bpp;
    fb->buffer = (uint32_t *)panel;
    fb->back_buffer = (uint32_t *)back;
}

static double now_s(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(void) {
    /* A rotation that writes past the panel buffer corrupts the heap and dies
     * in free(); line buffering keeps the verdicts printed before that. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    for (size_t gi = 0; gi < sizeof GEOS / sizeof GEOS[0]; gi++) {
        const Geo *g = &GEOS[gi];
        /* The geometry must fit the panel, or the test is of nothing. */
        if ((uint32_t)g->vx + g->lw > g->phys_h || (uint32_t)g->vy + g->lh > g->phys_w) {
            printf("  HARNESS ERROR %s does not fit its panel\n", g->name);
            return 2;
        }
        for (uint32_t bpp = 2; bpp <= 4; bpp += 2) {
            Framebuffer fb;
            size_t panel_bytes = (size_t)(g->phys_w * bpp + g->pad) * g->phys_h;
            size_t back_bytes = (size_t)g->lw * g->lh * bpp;
            uint8_t *want = malloc(panel_bytes), *got = malloc(panel_bytes);
            uint8_t *back = malloc(back_bytes);
            if (!want || !got || !back) { printf("  HARNESS ERROR malloc\n"); return 2; }
            int bad = 0;
            for (int trial = 0; trial < 3 && !bad; trial++) {
                for (size_t i = 0; i < back_bytes; i++) back[i] = (uint8_t)rnd();
                for (size_t i = 0; i < panel_bytes; i++) want[i] = (uint8_t)(0xA5 ^ (i * 7));
                memcpy(got, want, panel_bytes);
                setup(&fb, g, bpp, want, back);
                ref_rotate(&fb);
                setup(&fb, g, bpp, got, back);
                fb_swap(&fb);
                if (memcmp(want, got, panel_bytes) != 0) {
                    size_t i = 0; while (want[i] == got[i]) i++;
                    printf("  FAIL %-34s %ubpp: first difference at byte %zu"
                           " (row %zu)\n", g->name, bpp * 8, i,
                           i / fb.line_length);
                    bad = 1;
                }
            }
            if (!bad) printf("  ok   %-34s %ubpp: byte-identical, whole panel\n",
                             g->name, bpp * 8);
            fails += bad;
            free(want); free(got); free(back);
        }
    }

    /* Indicative host timing at the device geometry (see the header). */
    for (uint32_t bpp = 2; bpp <= 4; bpp += 2) {
        const Geo *g = &GEOS[0];
        Framebuffer fb;
        size_t panel_bytes = (size_t)g->phys_w * bpp * g->phys_h;
        uint8_t *panel = calloc(1, panel_bytes);
        uint8_t *back = calloc(1, (size_t)g->lw * g->lh * bpp);
        if (!panel || !back) { printf("  HARNESS ERROR malloc\n"); return 2; }
        enum { FRAMES = 200 };
        setup(&fb, g, bpp, panel, back);
        double t0 = now_s();
        for (int f = 0; f < FRAMES; f++) ref_rotate(&fb);
        double t1 = now_s();
        for (int f = 0; f < FRAMES; f++) fb_swap(&fb);
        double t2 = now_s();
        printf("  host timing (NOT ARM-representative), %ubpp: old %.3f ms/frame,"
               " new %.3f ms/frame\n", bpp * 8, (t1 - t0) * 1e3 / FRAMES,
               (t2 - t1) * 1e3 / FRAMES);
        free(panel); free(back);
    }

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
