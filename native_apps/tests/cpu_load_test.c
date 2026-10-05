/* Host-side regression for the Monitor page's CPU graph (control_panel/cpu_load.c).
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -o build/cpu_load_test \
 *       tests/cpu_load_test.c control_panel/cpu_load.c && ./build/cpu_load_test
 *
 * What it asserts, and why:
 *   A  the share divides by WALL time, not /proc/stat's grand total.  The
 *      fixture is the shape .188 shows (2026-10-05: idle 47940 ticks over 581 s
 *      of uptime, i.e. ~1/6 of idle never sampled): 30 busy ticks in a 1.00 s
 *      window whose idle column advanced only 20.  The honest share is 30 %;
 *      busy/(busy+idle) says 60 %.
 *   B  the panel's own ticks are taken out of the graphed figure and reported
 *      on their own, and a panel tick that lands ahead of the system's floors
 *      the rest at 0 rather than wrapping an unsigned subtraction;
 *   C  a backwards counter, an empty window or an invalid snap is no sample;
 *      a saturated window clamps at 1000 permille;
 *   D  /proc/self/stat is parsed after the LAST ')' — a comm holding ") 9 9"
 *      must not shift utime/stime;
 *   E  /proc/uptime parses to centiseconds, including one decimal digit;
 *   F  the ring keeps the last CPU_HIST_N oldest-first across wrap-around.
 */
#include "control_panel/cpu_load.h"

#include <stdio.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    unsigned long long b = 0, s = 0, cs = 0;
    int o = -1, me = -1;

    /* A — parse and the wall-time denominator. */
    CHECK(cpu_parse_stat("cpu  1653 0 6758 47940 919 0 44 0 0 0\ncpu0 1 2 3 4\n", &b) == 0
          && b == 1653 + 6758 + 44, "A: busy from .188's line, got %llu", b);
    CpuSnap p = { 0, 0, 0, 1 }, c = { 0, 0, 0, 1 };
    cpu_parse_stat("cpu  1000 0 500 40000 10 0 5 0 0 0", &p.busy);
    cpu_parse_stat("cpu  1020 0 510 40020 10 0 5 0 0 0", &c.busy);
    cpu_parse_uptime("581.75 487.42", &p.wall_cs);
    cpu_parse_uptime("582.75 487.60", &c.wall_cs);
    CHECK(cpu_share(&p, &c, 100, &o, &me) == 0 && o == 300 && me == 0,
          "A: 30 busy ticks over 1.00 s wall must be 300 permille, got %d (self %d)", o, me);
    CHECK(cpu_parse_stat("cpu0 1 2 3 4", &b) == -1, "A: a per-core line is not the total");
    CHECK(cpu_parse_stat("cpu  1 2", &b) == -1, "A: a short line is refused");

    /* B — own cost out of the graph, reported beside it. */
    p.self = 100; c.self = 105;
    CHECK(cpu_share(&p, &c, 100, &o, &me) == 0 && o == 250 && me == 50,
          "B: others 250 / self 50, got %d / %d", o, me);
    c.self = 100 + 31;    /* one tick more than the system saw */
    CHECK(cpu_share(&p, &c, 100, &o, &me) == 0 && o == 0 && me == 310,
          "B: others floors at 0, got %d / %d", o, me);

    /* C — no sample rather than a wrong one; clamp. */
    c.self = 105;
    CpuSnap back = c; back.busy = p.busy - 1;
    CHECK(cpu_share(&p, &back, 100, &o, &me) == -1, "C: busy ran backwards");
    CpuSnap same = c; same.wall_cs = p.wall_cs;
    CHECK(cpu_share(&p, &same, 100, &o, &me) == -1, "C: empty window");
    CpuSnap bad = c; bad.valid = 0;
    CHECK(cpu_share(&p, &bad, 100, &o, &me) == -1, "C: invalid snap");
    CpuSnap hot = c; hot.busy = p.busy + 150;
    CHECK(cpu_share(&p, &hot, 100, &o, &me) == 0 && o == 1000, "C: clamps at 1000, got %d", o);

    /* D — comm with spaces and a ')'. */
    CHECK(cpu_parse_self("2292 (control_panel) S 1 2292 2292 0 -1 4194560 300 0 0 0 77 23 0 0 20 0 1 0 1000",
                         &s) == 0 && s == 100, "D: plain comm, got %llu", s);
    CHECK(cpu_parse_self("7 (a) 9 9 (b)) R 1 7 7 0 -1 0 0 0 0 0 5 6 0 0", &s) == 0 && s == 11,
          "D: last ')' rule, got %llu", s);
    CHECK(cpu_parse_self("no paren here", &s) == -1, "D: no ')' is refused");

    /* E — uptime. */
    CHECK(cpu_parse_uptime("12.5 3.0", &cs) == 0 && cs == 1250, "E: one decimal, got %llu", cs);
    CHECK(cpu_parse_uptime("7 1", &cs) == 0 && cs == 700, "E: no decimal, got %llu", cs);
    CHECK(cpu_parse_uptime("x", &cs) == -1, "E: garbage refused");

    /* F — ring. */
    CpuHistory h;
    cpu_hist_clear(&h);
    CHECK(h.count == 0 && cpu_hist_get(&h, 0) == -1, "F: empty ring");
    cpu_hist_push(&h, 5); cpu_hist_push(&h, 6);
    CHECK(h.count == 2 && cpu_hist_get(&h, 0) == 5 && cpu_hist_get(&h, 1) == 6,
          "F: two pushes oldest-first");
    for (int i = 0; i < CPU_HIST_N + 7; i++) cpu_hist_push(&h, i);
    CHECK(h.count == CPU_HIST_N, "F: count saturates, got %d", h.count);
    CHECK(cpu_hist_get(&h, 0) == 7 && cpu_hist_get(&h, CPU_HIST_N - 1) == CPU_HIST_N + 6,
          "F: after wrap oldest %d newest %d", cpu_hist_get(&h, 0), cpu_hist_get(&h, CPU_HIST_N - 1));
    cpu_hist_push(&h, 5000);
    CHECK(cpu_hist_get(&h, CPU_HIST_N - 1) == 1000, "F: push clamps to 1000");

    printf("cpu_load_test: %s (%d failure(s))\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
