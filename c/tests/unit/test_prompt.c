/*
 * test_prompt.c — hem-tool's prompt helper (hem-tool-core prompt.c): plain
 * lines when the input is not a terminal, echo off / restored on a terminal.
 *
 * verifies: REQ-TOOL-028 (the no-echo helper: the non-terminal path reads
 *           lines verbatim — only the line ending removed — says so ONCE on
 *           the error stream, reports end of input and over-long lines; on a
 *           terminal the echo is off during a hidden read and restored after
 *           a normal return, after an early return (end of input) and after
 *           Ctrl-C, which still ends the process)
 *
 * The terminal cases run on a POSIX pseudo-terminal pair (skipped where none
 * can be opened, and on Windows, where the console path is the M12 gate's
 * attended check).
 */
#ifndef _WIN32
#define _XOPEN_SOURCE 600         /* posix_openpt, grantpt, unlockpt, ptsname */
#endif
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cmocka.h>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include "prompt.h"

static char *slurp(FILE *f)
{
    long n;
    char *buf;
    assert_int_equal(fseek(f, 0, SEEK_END), 0);
    n = ftell(f);
    assert_true(n >= 0);
    rewind(f);
    buf = malloc((size_t)n + 1);
    assert_non_null(buf);
    assert_int_equal(fread(buf, 1, (size_t)n, f), (size_t)n);
    buf[n] = '\0';
    return buf;
}

static size_t count(const char *hay, const char *needle)
{
    size_t n = 0;
    const char *p = hay;
    while ((p = strstr(p, needle)) != NULL) {
        n++;
        p += strlen(needle);
    }
    return n;
}

/* Not a terminal: verbatim lines, CRLF tolerated, end of input, an over-long
 * line consumed and reported, the notice exactly once. */
static void test_plain_lines_verbatim(void **state)
{
    (void)state;
    FILE *in = tmpfile(), *out = tmpfile(), *err = tmpfile();
    hem_prompt p;
    char buf[16];
    char *o, *e;

    assert_non_null(in);
    assert_non_null(out);
    assert_non_null(err);
    fputs("  two  spaces  \nwin\r\n0123456789abcdefXYZ\n\nlast", in);
    rewind(in);
    hem_prompt_init(&p, in, out, err);
    assert_false(hem_prompt_is_tty(&p));

    assert_int_equal(hem_prompt_line(&p, "q1: ", buf, sizeof buf), HEM_PROMPT_OK);
    assert_string_equal(buf, "  two  spaces  ");          /* verbatim */
    assert_int_equal(hem_prompt_secret(&p, "q2: ", buf, sizeof buf), HEM_PROMPT_OK);
    assert_string_equal(buf, "win");                      /* \r\n removed */
    assert_int_equal(hem_prompt_line(&p, "q3: ", buf, sizeof buf), HEM_PROMPT_TOO_LONG);
    assert_string_equal(buf, "");                         /* partial text wiped */
    assert_int_equal(hem_prompt_line(&p, "q4: ", buf, sizeof buf), HEM_PROMPT_OK);
    assert_string_equal(buf, "");                         /* an empty answer */
    assert_int_equal(hem_prompt_line(&p, "q5: ", buf, sizeof buf), HEM_PROMPT_OK);
    assert_string_equal(buf, "last");                     /* no final newline */
    assert_int_equal(hem_prompt_secret(&p, "q6: ", buf, sizeof buf), HEM_PROMPT_EOF);

    o = slurp(out);
    e = slurp(err);
    assert_non_null(strstr(o, "q1: \nq2: \nq3: \nq4: \nq5: \nq6: "));
    assert_null(strstr(o, "spaces"));                     /* answers never echoed */
    assert_null(strstr(o, "win"));
    assert_int_equal(count(e, "not a terminal"), 1);      /* said ONCE */
    free(o);
    free(e);
    fclose(in);
    fclose(out);
    fclose(err);
}

#ifndef _WIN32

static void sleep_ms(long ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static int echo_is_on(int fd)
{
    struct termios t;
    return tcgetattr(fd, &t) == 0 && (t.c_lflag & ECHO) != 0;
}

/* Wait (≤ 5 s) until the terminal's echo is `on`. */
static int wait_echo(int fd, int on)
{
    int i;
    for (i = 0; i < 500; i++) {
        if (echo_is_on(fd) == on) {
            return 1;
        }
        sleep_ms(10);
    }
    return 0;
}

static int open_pty(int *master, int *slave)
{
    const char *name;
    *master = posix_openpt(O_RDWR | O_NOCTTY);
    if (*master < 0) {
        return 0;
    }
    if (grantpt(*master) != 0 || unlockpt(*master) != 0 ||
        (name = ptsname(*master)) == NULL ||
        (*slave = open(name, O_RDWR | O_NOCTTY)) < 0) {
        close(*master);
        return 0;
    }
    return 1;
}

/* A terminal: the hidden answer is read with the echo off (the "keyboard"
 * side sees nothing of it echoed), and the echo is back after the normal
 * return and after end of input. The typist is a child process: it types
 * once it sees the echo off, and the parent tells it (a pipe) when the first
 * read is done — the echo is on only for an instant between the two reads.
 * alarm() turns a hang into a failure. */
static void test_tty_echo_restored(void **state)
{
    (void)state;
    int master, slave, sync_fd[2];
    pid_t child;
    FILE *in, *out, *err;
    hem_prompt p;
    char buf[64], seen[256];
    ssize_t n;
    int status = 0;

    if (!open_pty(&master, &slave)) {
        skip();
        return;       /* cmocka < 1.1.7 does not mark skip() noreturn */
    }
    assert_true(echo_is_on(slave));
    assert_int_equal(pipe(sync_fd), 0);
    alarm(20);
    child = fork();
    assert_true(child >= 0);
    if (child == 0) {
        struct termios t;
        char eof, go;
        close(sync_fd[1]);
        if (!wait_echo(slave, 0) || write(master, "s3cret\n", 7) != 7 ||
            read(sync_fd[0], &go, 1) != 1 || !wait_echo(slave, 0) ||
            tcgetattr(slave, &t) != 0) {
            _exit(1);
        }
        eof = (char)t.c_cc[VEOF];             /* Ctrl-D on an empty line */
        if (write(master, &eof, 1) != 1) {
            _exit(1);
        }
        _exit(0);
    }
    close(sync_fd[0]);
    in = fdopen(slave, "r");
    out = tmpfile();
    err = tmpfile();
    assert_non_null(in);
    assert_non_null(out);
    assert_non_null(err);
    hem_prompt_init(&p, in, out, err);
    assert_true(hem_prompt_is_tty(&p));

    assert_int_equal(hem_prompt_secret(&p, "pw: ", buf, sizeof buf), HEM_PROMPT_OK);
    assert_string_equal(buf, "s3cret");
    assert_true(echo_is_on(slave));                       /* normal return */
    assert_int_equal(write(sync_fd[1], "x", 1), 1);
    assert_int_equal(hem_prompt_secret(&p, "pw: ", buf, sizeof buf), HEM_PROMPT_EOF);
    assert_true(echo_is_on(slave));                       /* early return */
    assert_int_equal(waitpid(child, &status, 0), child);
    assert_true(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    alarm(0);
    close(sync_fd[1]);

    /* Nothing of the secret came back on the keyboard side. */
    assert_int_equal(fcntl(master, F_SETFL, O_NONBLOCK), 0);
    n = read(master, seen, sizeof seen - 1);
    seen[n > 0 ? n : 0] = '\0';
    assert_null(strstr(seen, "s3cret"));
    {
        char *e = slurp(err);
        assert_null(strstr(e, "not a terminal"));          /* it IS one */
        free(e);
    }
    fclose(in);
    fclose(out);
    fclose(err);
    close(master);
}

/* Ctrl-C during a hidden read: the echo is put back and the process still
 * dies of SIGINT (the previous action re-raised). */
static void test_tty_ctrl_c_restores(void **state)
{
    (void)state;
    int master, slave;
    pid_t child;
    int status = 0;

    if (!open_pty(&master, &slave)) {
        skip();
        return;       /* cmocka < 1.1.7 does not mark skip() noreturn */
    }
    alarm(20);
    child = fork();
    assert_true(child >= 0);
    if (child == 0) {
        FILE *in = fdopen(slave, "r");
        hem_prompt p;
        char buf[64];
        signal(SIGINT, SIG_DFL);
        hem_prompt_init(&p, in, stdout, stderr);
        (void)hem_prompt_secret(&p, "", buf, sizeof buf);   /* blocks */
        _exit(0);
    }
    assert_true(wait_echo(slave, 0));                     /* the read is hidden */
    assert_int_equal(kill(child, SIGINT), 0);
    assert_int_equal(waitpid(child, &status, 0), child);
    assert_true(WIFSIGNALED(status));
    assert_int_equal(WTERMSIG(status), SIGINT);
    assert_true(echo_is_on(slave));                       /* restored */
    alarm(0);
    close(slave);
    close(master);
}

#endif /* !_WIN32 */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_plain_lines_verbatim),
#ifndef _WIN32
        cmocka_unit_test(test_tty_echo_restored),
        cmocka_unit_test(test_tty_ctrl_c_restores),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
