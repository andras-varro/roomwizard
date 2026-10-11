/* Host regression for control_panel/cp_exec.c, the one fork/exec every
 * control_panel page that runs a command goes through.
 *
 *   cd native_apps && gcc -Wall -Wextra -I. -o build/cp_exec_test \
 *       tests/cp_exec_test.c control_panel/cp_exec.c && ./build/cp_exec_test
 *
 * A  exit status: 0, a non-zero code, exec failure (127), a signal (-1);
 * B  stdin reaches the child and nothing else does (a password is fed this way);
 * C  stdout and stderr are captured apart, cut to the buffer, and dropped when NULL;
 * D  a child that never reads stdin costs a failed write, not the process (SIGPIPE),
 *    and a pending SIGPIPE does not fire when the mask is restored;
 * E  no descriptor leaks into the child, none stays open in the parent.
 */
#include "control_panel/cp_exec.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fails;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int count_fds(void) {
    int n = 0;
    DIR *d = opendir("/proc/self/fd");
    if (!d) return -1;
    while (readdir(d)) n++;
    closedir(d);
    return n;
}

int main(void) {
    char out[64], err[64];

    /* A */
    { char *const a[] = { "sh", "-c", "exit 0", NULL };        CHECK(cp_exec(a, NULL, NULL, 0, NULL, 0) == 0, "A1 exit 0"); }
    { char *const a[] = { "sh", "-c", "exit 3", NULL };        CHECK(cp_exec(a, NULL, NULL, 0, NULL, 0) == 3, "A2 exit 3"); }
    { char *const a[] = { "/nonexistent/prog", NULL };         CHECK(cp_exec(a, NULL, NULL, 0, NULL, 0) == 127, "A3 exec failure"); }
    { char *const a[] = { "sh", "-c", "kill -9 $$", NULL };    CHECK(cp_exec(a, NULL, NULL, 0, NULL, 0) == -1, "A4 killed by a signal"); }

    /* B  stdin is delivered, and only to the child */
    { char *const a[] = { "cat", NULL };
      int r = cp_exec(a, "hunter2\n", out, sizeof(out), NULL, 0);
      CHECK(r == 0 && strcmp(out, "hunter2\n") == 0, "B1 stdin echoed back: r=%d [%s]", r, out); }
    { char *const a[] = { "sh", "-c", "cat >/dev/null; echo done", NULL };
      cp_exec(a, "secret", out, sizeof(out), err, sizeof(err));
      CHECK(strcmp(out, "done\n") == 0 && err[0] == '\0', "B2 the secret is not in the output: [%s][%s]", out, err); }
    { char *const a[] = { "sh", "-c", "read x; echo got:$x", NULL };
      cp_exec(a, "abc\n", out, sizeof(out), NULL, 0);
      CHECK(strcmp(out, "got:abc\n") == 0, "B3 read a line: [%s]", out); }
    { char *const a[] = { "sh", "-c", "if read x; then echo data; else echo eof; fi", NULL };
      cp_exec(a, NULL, out, sizeof(out), NULL, 0);
      CHECK(strcmp(out, "eof\n") == 0, "B4 no input means /dev/null, not the panel's stdin: [%s]", out); }

    /* C  captures */
    { char *const a[] = { "sh", "-c", "echo to-out; echo to-err >&2", NULL };
      int r = cp_exec(a, NULL, out, sizeof(out), err, sizeof(err));
      CHECK(r == 0 && strcmp(out, "to-out\n") == 0 && strcmp(err, "to-err\n") == 0, "C1 apart: [%s][%s]", out, err); }
    { char *const a[] = { "sh", "-c", "echo to-out; echo to-err >&2", NULL };
      int r = cp_exec(a, NULL, NULL, 0, err, sizeof(err));
      CHECK(r == 0 && strcmp(err, "to-err\n") == 0, "C2 stderr only: [%s]", err); }
    { char *const a[] = { "sh", "-c", "echo 0123456789abcdef", NULL };
      char small[8];
      int r = cp_exec(a, NULL, small, sizeof(small), NULL, 0);
      CHECK(r == 0 && strcmp(small, "0123456") == 0, "C3 cut to the buffer, NUL-terminated: [%s]", small); }
    { char *const a[] = { "sh", "-c", "i=0; while [ $i -lt 3000 ]; do echo 0123456789012345678901234567890123456789; echo e >&2; i=$((i+1)); done", NULL };
      int r = cp_exec(a, NULL, out, sizeof(out), err, sizeof(err));
      CHECK(r == 0 && strlen(out) == sizeof(out) - 1, "C4 a chatty child on both streams neither deadlocks nor overruns: r=%d len=%zu", r, strlen(out)); }
    { char *const a[] = { "sh", "-c", "echo x", NULL };
      out[0] = 'Z';
      cp_exec(a, NULL, out, 0, NULL, 0);
      CHECK(out[0] == 'Z', "C5 out_n 0 touches nothing"); }
    { char *const a[] = { "/nonexistent/prog", NULL };
      cp_exec(a, NULL, out, sizeof(out), err, sizeof(err));
      CHECK(out[0] == '\0' && err[0] == '\0', "C6 buffers are empty strings after a failed exec"); }

    /* D  a child that ignores its stdin */
    { char *const a[] = { "true", NULL };
      char big[200000];
      memset(big, 'x', sizeof(big) - 1);
      big[sizeof(big) - 1] = '\0';
      int r = cp_exec(a, big, NULL, 0, NULL, 0);
      CHECK(r == 0, "D1 survived a write to a child that never reads, r=%d", r);
      sigset_t pend;
      sigpending(&pend);
      CHECK(!sigismember(&pend, SIGPIPE), "D2 no SIGPIPE left pending"); }

    /* E  descriptors */
    { int before = count_fds();
      char *const a[] = { "sh", "-c", "ls /proc/self/fd | wc -l", NULL };
      for (int i = 0; i < 20; i++) cp_exec(a, "x", out, sizeof(out), err, sizeof(err));
      CHECK(count_fds() == before, "E1 parent fds: %d before, %d after", before, count_fds());
      int r = cp_exec(a, "x", out, sizeof(out), NULL, 0);
      int n = 0;
      sscanf(out, "%d", &n);
      /* 0, 1, 2 and the ls's own directory descriptor, plus whatever this shell has */
      CHECK(r == 0 && n <= 5, "E2 child sees %d fds", n); }

    if (fails) { printf("%d failure(s)\n", fails); return 1; }
    printf("cp_exec_test: all passed\n");
    return 0;
}
