/* cp_exec.h — run a program for a control_panel page: fork/exec with no shell,
 * an optional string on its stdin, its stdout and stderr captured or dropped.
 *
 * The one implementation under every page that runs a command (date, hwclock,
 * sshd, set-hostname, mkpasswd).  Standalone (libc only) so a host test drives it
 * (tests/cp_exec_test.c).
 */
#ifndef CP_EXEC_H
#define CP_EXEC_H

#include <stddef.h>

/* Runs argv[0] (found on PATH when it has no '/') with argv as its arguments.
 * `in`, if not NULL, is written to its stdin, which is then closed; NULL gives it
 * /dev/null.  Its stdout goes to out (at most out_n - 1 bytes kept, NUL-terminated,
 * the rest read and dropped) and its stderr to err likewise; a NULL buffer sends
 * that stream to /dev/null.  SIGPIPE is blocked around the run, so a child that
 * exits without reading `in` costs a short write, not the process.
 *
 * Returns the child's exit status (0..255; 127 when exec failed), or -1 when it
 * could not be started or was killed by a signal.  Blocks until the child exits.
 * `in` is never copied anywhere but the pipe. */
int cp_exec(char *const argv[], const char *in, char *out, size_t out_n,
            char *err, size_t err_n);

#endif
