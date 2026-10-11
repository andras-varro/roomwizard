/* cp_exec.c — see cp_exec.h. */
#include "cp_exec.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>

/* One captured stream: fd -1 once it hit EOF. */
typedef struct { int fd; char *buf; size_t cap, len; } Sink;

static void sink_read(Sink *s) {
    char tmp[256];
    ssize_t n = read(s->fd, tmp, sizeof(tmp));
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (n <= 0) { close(s->fd); s->fd = -1; return; }
    for (ssize_t i = 0; i < n && s->buf && s->len + 1 < s->cap; i++)
        s->buf[s->len++] = tmp[i];
}

/* Unblocks SIGPIPE, first discarding one a failed write left pending: delivered
 * on unblock it would kill the panel. */
static void restore_mask(const sigset_t *block, const sigset_t *old) {
    struct timespec none = { 0, 0 };
    while (sigtimedwait(block, NULL, &none) > 0)
        ;
    sigprocmask(SIG_SETMASK, old, NULL);
}

int cp_exec(char *const argv[], const char *in, char *out, size_t out_n,
            char *err, size_t err_n) {
    if (out && out_n) out[0] = '\0';
    if (err && err_n) err[0] = '\0';
    if (out && !out_n) out = NULL;
    if (err && !err_n) err = NULL;

    int pin[2] = { -1, -1 }, pout[2] = { -1, -1 }, perr[2] = { -1, -1 };
    if ((in && pipe(pin) != 0) || (out && pipe(pout) != 0) || (err && pipe(perr) != 0))
        goto fail_pipes;

    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGPIPE);
    sigprocmask(SIG_BLOCK, &block, &old);

    pid_t pid = fork();
    if (pid < 0) {
        restore_mask(&block, &old);
        goto fail_pipes;
    }
    if (pid == 0) {
        sigprocmask(SIG_SETMASK, &old, NULL);
        int nul = open("/dev/null", O_RDWR);
        dup2(in ? pin[0] : nul, 0);
        dup2(out ? pout[1] : nul, 1);
        dup2(err ? perr[1] : nul, 2);
        int fds[6] = { pin[0], pin[1], pout[0], pout[1], perr[0], perr[1] };
        for (int i = 0; i < 6; i++) if (fds[i] > 2) close(fds[i]);
        if (nul > 2) close(nul);
        /* Close all fds >= 3 to avoid inheriting panel's fd (/dev/fb0, evdev, logs) */
        for (int fd = 3; fd < 1024; fd++) close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }

    if (pin[0] >= 0) close(pin[0]);
    if (pout[1] >= 0) close(pout[1]);
    if (perr[1] >= 0) close(perr[1]);

    /* stdin: small, written whole before reading (far below a pipe's buffer);
     * a child that never reads it just makes write() fail. */
    if (in) {
        size_t left = strlen(in);
        const char *p = in;
        while (left) {
            ssize_t w = write(pin[1], p, left);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) break;
            p += w; left -= (size_t)w;
        }
        close(pin[1]);
    }

    Sink so = { out ? pout[0] : -1, out, out_n, 0 };
    Sink se = { err ? perr[0] : -1, err, err_n, 0 };
    while (so.fd >= 0 || se.fd >= 0) {
        struct pollfd pf[2];
        int n = 0;
        if (so.fd >= 0) { pf[n].fd = so.fd; pf[n].events = POLLIN; pf[n].revents = 0; n++; }
        if (se.fd >= 0) { pf[n].fd = se.fd; pf[n].events = POLLIN; pf[n].revents = 0; n++; }
        if (poll(pf, (nfds_t)n, -1) < 0 && errno != EINTR) break;
        int i = 0;
        if (so.fd >= 0) { if (pf[i].revents) sink_read(&so); i++; }
        if (se.fd >= 0) { if (pf[i].revents) sink_read(&se); }
    }
    if (so.fd >= 0) close(so.fd);
    if (se.fd >= 0) close(se.fd);
    if (out) out[so.len] = '\0';
    if (err) err[se.len] = '\0';

    int st = 0;
    while (waitpid(pid, &st, 0) < 0)
        if (errno != EINTR) { restore_mask(&block, &old); return -1; }
    restore_mask(&block, &old);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;

fail_pipes:
    {
        int fds[6] = { pin[0], pin[1], pout[0], pout[1], perr[0], perr[1] };
        for (int i = 0; i < 6; i++) if (fds[i] >= 0) close(fds[i]);
    }
    return -1;
}
