/* fb_plane_bench — what a smaller drawing surface actually costs, measured.
 *
 * DEVICE ONLY.  Cross-compiled and deployed like any other tool; there is no
 * host build, because the number it reports is a property of this SoC's memory
 * bandwidth and nothing a host measurement predicts.  Build line (from
 * build-and-deploy.sh, which is the only build path):
 *
 *   $CC -O2 -static -I. tests/fb_plane_bench.c $COMMON_OBJ -o build/fb_plane_bench -lm
 *
 * Why it exists.  Rendering into a half-size surface on a scaling DSS overlay
 * is supposed to cost a quarter of the pixel fill, because the drawing and the
 * fb_swap() copy both shrink with AREA.  "Supposed to" is a prediction; this
 * measures it.  Run it against two framebuffers and compare:
 *
 *   fb_plane_bench /dev/fb0            # 800x480, the shipped geometry
 *   fb_plane_bench /dev/fb1            # whatever fbset left there
 *
 * ⚠️ The scene is defined in FRACTIONS of the surface, never in absolute
 * pixels, for the one reason that decides whether this instrument means
 * anything: a scene fixed in pixels would draw the same work at both
 * geometries and report no difference, and a scene fixed to 800x480 would
 * report a 4x win it did not earn.  The pixel count it prints is the receipt
 * for that — read it before believing the timing.  Two runs whose
 * "scene pixels" do not stand in the ratio of their areas are not an A/B.
 *
 * ⚠️ It reports CPU and wall separately.  They diverge here: the drawing is
 * pure store bandwidth, so a run that is slower in wall time without being
 * slower in CPU time is being held up by the DSS fetching the panel, which is
 * exactly what changes when an overlay is enabled.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "common/framebuffer.h"

/* 32-bit ARM: sizeof(long) == 4, so never scale a tv_sec into microseconds.
 * Baseline both clocks to the first reading and accumulate in long long. */
static long long ms_since(const struct timespec *base, const struct timespec *now)
{
    long long s  = (long long)now->tv_sec  - (long long)base->tv_sec;
    long long ns = (long long)now->tv_nsec - (long long)base->tv_nsec;
    return s * 1000LL + ns / 1000000LL;
}

int main(int argc, char *argv[])
{
    const char *dev    = (argc > 1) ? argv[1] : "/dev/fb0";
    int         frames = (argc > 2) ? atoi(argv[2]) : 300;

    if (frames < 1) frames = 1;

    Framebuffer fb;
    fb_set_bpp(dev, 32);
    if (fb_init(&fb, dev) < 0) {
        fprintf(stderr, "fb_plane_bench: fb_init(%s) failed\n", dev);
        return 1;
    }

    int w = (int)fb.width, h = (int)fb.height;

    /* Scene geometry, all of it derived.  tile is a twentieth of the width, so
     * the grid holds the same NUMBER of cells at any geometry and each cell's
     * area scales with the surface's. */
    int tile = w / 20;
    if (tile < 2) tile = 2;
    int cell = tile - tile / 5;              /* 80% of the tile, leaving a gutter */
    int cols = w / tile;
    int rows = h / tile;
    int band_h = h / 3;
    int scale  = h / 120;                    /* 4 at 480 rows, 2 at 240 — same on-panel size */
    if (scale < 1) scale = 1;

    /* The receipt: what one frame asks the CPU to store, counted rather than
     * assumed.  fb_swap() copies the whole surface on top of this. */
    long long scene_px = (long long)w * h                        /* fb_clear */
                       + (long long)cols * rows * cell * cell    /* the grid */
                       + (long long)w * band_h;                  /* the gradient band */
    long long swap_px  = (long long)w * h;

    printf("fb_plane_bench: %s  %dx%d  %u bpp  stride %u\n",
           dev, w, h, fb.bytes_per_pixel * 8, fb.line_length);
    printf("  scene pixels/frame %lld   swap pixels/frame %lld   frames %d\n",
           scene_px, swap_px, frames);
    printf("  grid %dx%d cells of %dpx, band %dpx, text scale %d\n",
           cols, rows, cell, band_h, scale);
    fflush(stdout);

    struct timespec w0, c0, w1, c1;
    clock_gettime(CLOCK_MONOTONIC, &w0);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c0);

    for (int f = 0; f < frames; f++) {
        /* Vary the frame so nothing can be hoisted out of the loop, and so a
         * run is not accidentally measuring a no-op. */
        int phase = f & 31;

        fb_clear(&fb, RGB(8, 8, 16 + phase));

        for (int r = 0; r < rows; r++) {
            for (int c = 0; c < cols; c++) {
                uint32_t col = RGB((c * 11 + phase * 7) & 0xff,
                                   (r * 13 + phase * 5) & 0xff,
                                   (c * 7 + r * 3) & 0xff);
                fb_fill_rect(&fb, c * tile, r * tile, cell, cell, col);
            }
        }

        fb_fill_rect_gradient(&fb, 0, (h - band_h) / 2, w, band_h,
                              RGB(phase * 4, 0, 128), RGB(0, phase * 4, 32));

        fb_draw_text(&fb, tile, tile, "PLANE BENCH", COLOR_WHITE, scale);
        fb_draw_line(&fb, 0, 0, w - 1, h - 1, COLOR_WHITE);

        fb_swap(&fb);
    }

    clock_gettime(CLOCK_MONOTONIC, &w1);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &c1);

    long long wall = ms_since(&w0, &w1);
    long long cpu  = ms_since(&c0, &c1);

    printf("  wall %lld ms total, %lld us/frame   cpu %lld ms total, %lld us/frame\n",
           wall, (wall * 1000LL) / frames, cpu, (cpu * 1000LL) / frames);
    if (wall > 0)
        printf("  cpu/wall %lld%%   fps %lld\n", (cpu * 100LL) / wall,
               (frames * 1000LL) / wall);
    printf("  per Mpixel of scene: cpu %lld us\n",
           scene_px ? (cpu * 1000LL * 1000000LL) / scene_px : 0);

    fb_clear(&fb, COLOR_BLACK);
    fb_swap(&fb);
    fb_close(&fb);
    return 0;
}
