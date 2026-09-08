/* Host-side regression for the PPM loader and its nearest-neighbour scaler.
 *
 * Runs on the DEV MACHINE with native gcc, not on the device.  ppm.c links
 * nothing but libc — no framebuffer, no /dev/fb0, no config — so a fixture
 * written to a file and a malloc'd source array exercise every line of it with
 * no device in the loop.  That matters here because the defect this file was
 * written for is an out-of-bounds READ: on the device it silently returns
 * whatever bytes sat before the icon buffer, which renders as a plausible-
 * looking icon rather than as a crash, and the launcher is the only caller.
 *
 *   cd native_apps && gcc -Wall -Wextra -Wno-unused-parameter -I common -o build/ppm_test tests/ppm_test.c common/ppm.c -lm && ./build/ppm_test
 *
 * What it asserts, and why: ppm_scale() validated only its DESTINATION size, so
 * a src_h of 0 made "if (sy >= src_h) sy = src_h - 1" assign sy = -1 and the
 * inner loop read src[-src_w + sx] — before the allocation.  Group H is that
 * case, and it uses the guard-byte pattern framebuffer_bpp_test.c established,
 * inverted: the source payload is bracketed by a wide region of a sentinel
 * pixel value, so an out-of-bounds read does not merely maybe-crash, it lands a
 * value in the OUTPUT that could have come from nowhere else.  The guard is a
 * tracer rather than a tripwire because nothing here writes out of bounds; the
 * dst allocation is always the size the caller asked for.  Groups A-D pin the
 * header parser (position-coded pixels, so a shifted or transposed result
 * cannot pass), group E pins a KNOWN DEFECT rather than correct behaviour —
 * read its comment — and F/G pin the sampling rule the launcher's icon
 * rescaling depends on, floor-based nearest neighbour, verified by reading.
 *
 * Fixtures are written under build/ and removed again.  Not /tmp: this suite is
 * run from native_apps, build/ is already the scratch directory the build line
 * writes into, and a fixture there cannot collide with another user's.
 *
 * NOT part of build-and-deploy.sh: that script cross-compiles for ARM and this
 * is a host binary.  Run it by hand after touching ppm.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ppm.h"

static int fails = 0;

static void check(int cond, const char *what) {
    if (cond) { printf("  ok   %-38s\n", what); }
    else      { printf("  FAIL %-38s\n", what); fails++; }
}

static void expect_px(const char *what, uint32_t got, uint32_t want) {
    if (got == want) { printf("  ok   %-38s %08X\n", what, got); }
    else { printf("  FAIL %-38s got %08X want %08X\n", what, got, want); fails++; }
}

/* ── The position-coded pattern ─────────────────────────────────────────────
 * Red depends only on x, green only on y, blue on both with different weights.
 * So a transposed result, a result shifted by one pixel and a result shifted by
 * one BYTE are each distinguishable from the correct one, which a flat or
 * gradient fixture would not be.  Valid for x < 8 and y < 4, the largest coded
 * image below; the 4097-wide limit fixture is never read back. */
static uint32_t coded(int x, int y) {
    unsigned r = 0x11u + 0x10u * (unsigned)x;
    unsigned g = 0x22u + 0x20u * (unsigned)y;
    unsigned b = 0x33u + (unsigned)x + 4u * (unsigned)y;
    return 0xFF000000u | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
}

#define FIX_PATH    "build/ppm_test_fixture.ppm"
#define ABSENT_PATH "build/ppm_test_absent.ppm"

/* header text, then `pixels` position-coded RGB triples of a w-wide image.
 * `pixels` < w*h is how a truncated file is built. */
static int write_p6(const char *header, int w, int pixels) {
    static unsigned char buf[16384];
    size_t n = strlen(header);
    if (n > sizeof buf) return -1;
    memcpy(buf, header, n);
    for (int i = 0; i < pixels; i++) {
        uint32_t c = coded(i % w, i / w);
        if (n + 3 > sizeof buf) return -1;
        buf[n++] = (unsigned char)(c >> 16);
        buf[n++] = (unsigned char)(c >> 8);
        buf[n++] = (unsigned char)c;
    }
    FILE *f = fopen(FIX_PATH, "wb");
    if (!f) return -1;
    size_t wrote = fwrite(buf, 1, n, f);
    fclose(f);
    return (wrote == n) ? 0 : -1;
}

/* Load FIX_PATH after writing `header` + `pixels` coded triples.  Reports a
 * fixture that could not be written as a failure of its own, so a read-only
 * build/ cannot read as the loader refusing a file it should have accepted. */
static uint32_t *load_fixture(const char *label, const char *header,
                              int w, int pixels, int *lw, int *lh) {
    *lw = -12345; *lh = -12345;
    if (write_p6(header, w, pixels) != 0) {
        printf("  FAIL %-38s cannot write " FIX_PATH "\n", label);
        fails++;
        return NULL;
    }
    return ppm_load(FIX_PATH, lw, lh);
}

/* Every pixel of a w*h image equals coded(x, y). */
static void check_coded_grid(const char *label, const uint32_t *px, int w, int h) {
    int bad = 0, bx = -1, by = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (px[y * w + x] != coded(x, y)) {
                if (!bad) { bx = x; by = y; }
                bad++;
            }
    if (!bad) { printf("  ok   %-38s %d px\n", label, w * h); }
    else {
        printf("  FAIL %-38s %d/%d wrong, first (%d,%d) got %08X want %08X\n",
               label, bad, w * h, bx, by,
               px[by * w + bx], coded(bx, by));
        fails++;
    }
}

/* ── Group H's guarded source ───────────────────────────────────────────────
 * GUARD_PX cannot be produced by coded() (its alpha byte is not 0xFF), so a
 * GUARD_PX in the OUTPUT is proof of a read outside the payload and nothing
 * else.  GUARD_PIXELS is wide enough for every negative index the unfixed
 * arithmetic can reach from a 4x2 payload: the worst is src_h = -2, which
 * clamps sy to -3 and reads src[-12 .. -9]. */
#define GUARD_PX      0x5A5A5A5Au
#define GUARD_PIXELS  32
#define PAY_W 4
#define PAY_H 2

static uint32_t *guard_block;      /* guard | payload | guard */
static const uint32_t *guard_src;  /* the payload, what ppm_scale is handed */

static void guard_init(void) {
    size_t total = (size_t)GUARD_PIXELS * 2 + (size_t)PAY_W * PAY_H;
    free(guard_block);
    guard_block = malloc(total * sizeof(uint32_t));
    if (!guard_block) { printf("  FAIL %-38s\n", "H: cannot allocate"); fails++; exit(1); }
    for (size_t i = 0; i < total; i++) guard_block[i] = GUARD_PX;
    guard_src = guard_block + GUARD_PIXELS;
    for (int y = 0; y < PAY_H; y++)
        for (int x = 0; x < PAY_W; x++)
            guard_block[GUARD_PIXELS + y * PAY_W + x] = coded(x, y);
}

/* A non-positive source dimension must be REFUSED.  If it is not, say whether
 * the returned buffer carries the tracer, because "returned something" and
 * "returned bytes from before the allocation" are different bug reports. */
static void expect_refused_oob(const char *what, int src_w, int src_h,
                               int dst_w, int dst_h) {
    guard_init();
    uint32_t *dst = ppm_scale(guard_src, src_w, src_h, dst_w, dst_h);
    if (!dst) { printf("  ok   %-38s refused\n", what); return; }
    int traced = 0;
    for (int i = 0; i < dst_w * dst_h; i++) if (dst[i] == GUARD_PX) traced++;
    if (traced)
        printf("  FAIL %-38s not refused, and %d/%d output px came from OUTSIDE "
               "the source buffer\n", what, traced, dst_w * dst_h);
    else
        printf("  FAIL %-38s not refused (returned %d px)\n",
               what, dst_w * dst_h);
    fails++;
    free(dst);
}

int main(void) {
    int w, h;
    uint32_t *px;

    printf("ppm_test — P6 header parsing, pixel conversion, nearest-neighbour scale\n");

    /* ── A: a well-formed P6 round-trips ─────────────────────────────────── */
    printf("\nA  well-formed P6: dimensions and pixel bytes\n");
    px = load_fixture("A: load 3x2", "P6\n3 2\n255\n", 3, 6, &w, &h);
    check(px != NULL, "A: a well-formed P6 loads");
    check(w == 3 && h == 2, "A: dimensions are 3 wide, 2 high");
    if (px) {
        check_coded_grid("A: every pixel, in order", px, 3, 2);
        expect_px("A: pixel (0,0) is R,G,B in that order", px[0], coded(0, 0));
        expect_px("A: alpha byte is opaque", px[5] >> 24, 0xFFu);
        free(px);
    } else {
        check(0, "A: every pixel, in order");
        check(0, "A: pixel (0,0) is R,G,B in that order");
        check(0, "A: alpha byte is opaque");
    }

    /* ── B: header lexing ────────────────────────────────────────────────── */
    printf("\nB  header lexing: #-comments and whitespace runs\n");
    px = load_fixture("B: comment", "P6\n# an icon\n3 2\n255\n", 3, 6, &w, &h);
    check(px != NULL && w == 3 && h == 2, "B: a comment line after the magic");
    if (px) check_coded_grid("B: comment case, pixels intact", px, 3, 2);
    free(px);

    px = load_fixture("B: comments everywhere",
                      "P6 # after magic\n3 # after width\n2\n# own line\n255\n",
                      3, 6, &w, &h);
    check(px != NULL && w == 3 && h == 2, "B: a comment between every field");
    if (px) check_coded_grid("B: comments case, pixels intact", px, 3, 2);
    free(px);

    px = load_fixture("B: whitespace", "P6 \t\n 3 \t 2\n\n\n255\n", 3, 6, &w, &h);
    check(px != NULL && w == 3 && h == 2, "B: tabs and blank lines between fields");
    if (px) check_coded_grid("B: whitespace case, pixels intact", px, 3, 2);
    free(px);

    /* ── C: the documented limits are enforced, not ignored ──────────────── */
    printf("\nC  maxval and dimension limits (the header says 255-only, 4096 cap)\n");
    px = load_fixture("C: maxval 200", "P6\n3 2\n200\n", 3, 6, &w, &h);
    check(px == NULL, "C: maxval != 255 is REFUSED, not scaled");
    free(px);

    px = load_fixture("C: w 0", "P6\n0 2\n255\n", 1, 0, &w, &h);
    check(px == NULL, "C: width 0 is refused");
    free(px);

    px = load_fixture("C: h 0", "P6\n3 0\n255\n", 3, 0, &w, &h);
    check(px == NULL, "C: height 0 is refused");
    free(px);

    px = load_fixture("C: w -3", "P6\n-3 2\n255\n", 1, 0, &w, &h);
    check(px == NULL, "C: a negative width is refused");
    free(px);

    /* Enough data that ONLY the 4096 cap can refuse these — a fixture short of
     * its own pixel count would be refused by the read loop instead, and the
     * cap could be lifted with the assertion still green. */
    px = load_fixture("C: w 4097", "P6\n4097 1\n255\n", 4097, 4097, &w, &h);
    check(px == NULL, "C: width 4097 exceeds the cap");
    free(px);

    px = load_fixture("C: h 4097", "P6\n1 4097\n255\n", 1, 4097, &w, &h);
    check(px == NULL, "C: height 4097 exceeds the cap");
    free(px);

    /* ── D: malformed input is refused ───────────────────────────────────── */
    printf("\nD  malformed input: truncation, wrong magic, absent file\n");
    px = load_fixture("D: truncated", "P6\n3 2\n255\n", 3, 5, &w, &h);
    check(px == NULL, "D: 5 pixels of a 6-pixel image is refused");
    check(w == -12345 && h == -12345, "D: out_width/out_height untouched");
    free(px);

    px = load_fixture("D: no data", "P6\n3 2\n255\n", 3, 0, &w, &h);
    check(px == NULL, "D: a header with no pixel data is refused");
    free(px);

    px = load_fixture("D: P3", "P3\n3 2\n255\n17 34 51 ", 3, 0, &w, &h);
    check(px == NULL, "D: ASCII P3 is refused");
    free(px);

    px = load_fixture("D: P5", "P5\n3 2\n255\n", 3, 6, &w, &h);
    check(px == NULL, "D: greyscale P5 is refused");
    free(px);

    px = load_fixture("D: empty", "", 1, 0, &w, &h);
    check(px == NULL, "D: an empty file is refused");
    free(px);

    unlink(ABSENT_PATH);
    w = -12345; h = -12345;
    px = ppm_load(ABSENT_PATH, &w, &h);
    check(px == NULL, "D: a file that does not exist is refused");
    check(w == -12345 && h == -12345, "D: absent file leaves out_* untouched");
    free(px);

    /* ── E: a CRLF header line — PINNING A DEFECT, not correct behaviour ───
     * The byte between the header and the data is consumed by one unchecked
     * fgetc().  fscanf("%d") stops at the CR, that fgetc eats the CR, and the
     * LF becomes the first byte of pixel data — so EVERY pixel is shifted one
     * byte and the last one is built from two of its own bytes plus the byte
     * before it.  The count works out because CRLF contributes exactly the one
     * extra byte the shifted read needs, so the load SUCCEEDS and reports the
     * right dimensions: a Windows-authored icon renders in wrong colours with
     * nothing logged.  This is a real defect in ppm.c and these assertions
     * describe it rather than endorse it; when it is fixed, group E is what
     * fails, and the fix is to replace the bare fgetc() with one that consumes
     * a CR-LF pair.  Until then the shift is pinned so nobody "fixes" the
     * loader in a way that leaves the shift and changes something else. */
    printf("\nE  CRLF after maxval: the one-byte shift, PINNED AS A DEFECT\n");
    px = load_fixture("E: CRLF", "P6\n3 2\n255\r\n", 3, 6, &w, &h);
    check(px != NULL, "E: DEFECT: a CRLF header still loads");
    check(w == 3 && h == 2, "E: DEFECT: dimensions look correct");
    if (px) {
        uint32_t p0 = coded(0, 0), p1 = coded(1, 0);
        uint32_t p4 = coded(1, 1), p5 = coded(2, 1);
        /* first pixel = LF, then pixel 0's red and green */
        expect_px("E: DEFECT: pixel 0 is LF,R0,G0",
                  px[0], 0xFF000000u | ((uint32_t)'\n' << 16)
                         | (((p0 >> 16) & 0xFFu) << 8) | ((p0 >> 8) & 0xFFu));
        expect_px("E: DEFECT: pixel 1 is blue0,R1,G1",
                  px[1], 0xFF000000u | ((p0 & 0xFFu) << 16)
                         | (((p1 >> 16) & 0xFFu) << 8) | ((p1 >> 8) & 0xFFu));
        expect_px("E: DEFECT: pixel 5 is blue4,R5,G5",
                  px[5], 0xFF000000u | ((p4 & 0xFFu) << 16)
                         | (((p5 >> 16) & 0xFFu) << 8) | ((p5 >> 8) & 0xFFu));
        free(px);
    } else {
        check(0, "E: DEFECT: pixel 0 is LF,R0,G0");
        check(0, "E: DEFECT: pixel 1 is blue0,R1,G1");
        check(0, "E: DEFECT: pixel 5 is blue4,R5,G5");
    }

    /* ── F: an exact 1:1 scale is byte-identical ─────────────────────────── */
    printf("\nF  1:1 scale is byte-identical\n");
    uint32_t src[PAY_W * PAY_H];
    for (int y = 0; y < PAY_H; y++)
        for (int x = 0; x < PAY_W; x++) src[y * PAY_W + x] = coded(x, y);

    uint32_t *id = ppm_scale(src, PAY_W, PAY_H, PAY_W, PAY_H);
    check(id != NULL, "F: 4x2 -> 4x2 succeeds");
    if (id) {
        check(memcmp(id, src, sizeof src) == 0, "F: 4x2 -> 4x2 is byte-identical");
        free(id);
    } else {
        check(0, "F: 4x2 -> 4x2 is byte-identical");
    }

    /* ── G: the sampling rule, read out of the source ─────────────────────
     * sx = x * src_w / dst_w with integer division: FLOOR, not centre-
     * sampling and not rounding.  A 4x2 source is deliberately non-square, so
     * a transposed implementation cannot pass, and the cells called out by
     * name are the ones where floor and round disagree. */
    printf("\nG  nearest-neighbour sampling: floor(x * src_w / dst_w)\n");
    uint32_t *up = ppm_scale(src, PAY_W, PAY_H, 8, 4);
    check(up != NULL, "G: 4x2 -> 8x4 succeeds");
    if (up) {
        int bad = 0;
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 8; x++)
                if (up[y * 8 + x] != coded(x / 2, y / 2)) bad++;
        check(bad == 0, "G: 2x up-scale duplicates 2x2 blocks");
        expect_px("G: up(1,0) FLOORS to src(0,0)", up[1], coded(0, 0));
        expect_px("G: up(7,3) is the last source px", up[3 * 8 + 7], coded(3, 1));
        free(up);
    } else {
        check(0, "G: 2x up-scale duplicates 2x2 blocks");
        check(0, "G: up(1,0) FLOORS to src(0,0)");
        check(0, "G: up(7,3) is the last source px");
    }

    uint32_t *down = ppm_scale(src, PAY_W, PAY_H, 2, 1);
    check(down != NULL, "G: 4x2 -> 2x1 succeeds");
    if (down) {
        expect_px("G: down(0,0) samples src(0,0)", down[0], coded(0, 0));
        expect_px("G: down(1,0) samples src(2,0)", down[1], coded(2, 0));
        free(down);
    } else {
        check(0, "G: down(0,0) samples src(0,0)");
        check(0, "G: down(1,0) samples src(2,0)");
    }

    /* A ratio that divides evenly in neither axis: sx = 0,1,2 and sy = 0,0,1. */
    uint32_t *odd = ppm_scale(src, PAY_W, PAY_H, 3, 3);
    check(odd != NULL, "G: 4x2 -> 3x3 succeeds");
    if (odd) {
        int bad = 0;
        for (int y = 0; y < 3; y++)
            for (int x = 0; x < 3; x++)
                if (odd[y * 3 + x] != coded(x * PAY_W / 3, y * PAY_H / 3)) bad++;
        check(bad == 0, "G: non-integer ratio floors per axis");
        expect_px("G: odd(2,2) is src(2,1)", odd[2 * 3 + 2], coded(2, 1));
        free(odd);
    } else {
        check(0, "G: non-integer ratio floors per axis");
        check(0, "G: odd(2,2) is src(2,1)");
    }

    /* ── H: MEMORY SAFETY — a non-positive SOURCE dimension ──────────────── */
    printf("\nH  memory safety: a non-positive source dimension must be refused\n");
    expect_refused_oob("H: src_h == 0 (reads src[-src_w])", PAY_W, 0, 4, 4);
    expect_refused_oob("H: src_w == 0 (reads src[-1])", 0, PAY_H, 4, 4);
    expect_refused_oob("H: both zero", 0, 0, 4, 4);
    expect_refused_oob("H: src_w negative", -PAY_W, PAY_H, 4, 4);
    expect_refused_oob("H: src_h negative", PAY_W, -PAY_H, 4, 4);

    /* ── I: the destination and pointer arguments ────────────────────────── */
    printf("\nI  destination and pointer validation\n");
    uint32_t *r;
    r = ppm_scale(NULL, PAY_W, PAY_H, 4, 4);  check(r == NULL, "I: a NULL source is refused");   free(r);
    r = ppm_scale(src, PAY_W, PAY_H, 0, 4);   check(r == NULL, "I: dst_w 0 is refused");         free(r);
    r = ppm_scale(src, PAY_W, PAY_H, 4, 0);   check(r == NULL, "I: dst_h 0 is refused");         free(r);
    r = ppm_scale(src, PAY_W, PAY_H, -1, 4);  check(r == NULL, "I: a negative dst_w is refused"); free(r);
    r = ppm_scale(src, PAY_W, PAY_H, 4, -1);  check(r == NULL, "I: a negative dst_h is refused"); free(r);

    free(guard_block);
    guard_block = NULL;
    unlink(FIX_PATH);
    unlink(ABSENT_PATH);

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
