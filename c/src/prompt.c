/*
 * prompt.c — line prompts with a no-echo variant (see prompt.h).
 *
 * implements: REQ-TOOL-028
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L   /* fileno, isatty, termios, sigaction */
#endif
#include "prompt.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>                   /* _isatty, _fileno */
#else
#include <signal.h>
#include <termios.h>
#include <unistd.h>
#endif

/* --- echo control: one hidden read at a time (hem-tool is single-threaded) */

#ifdef _WIN32

static HANDLE        g_con;
static DWORD         g_con_mode;
static volatile LONG g_hidden;

/* Ctrl-C / Ctrl-Break / close: put the echo back, then let the default
 * handler end the process. */
static BOOL WINAPI restore_on_ctrl(DWORD type)
{
    (void)type;
    if (g_hidden) {
        SetConsoleMode(g_con, g_con_mode);
        g_hidden = 0;
    }
    return FALSE;
}

static int echo_off(FILE *in)
{
    DWORD mode;
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);

    if (_fileno(in) != _fileno(stdin) || h == NULL || h == INVALID_HANDLE_VALUE ||
        !GetConsoleMode(h, &mode)) {
        return 0;
    }
    g_con = h;
    g_con_mode = mode;
    g_hidden = 1;
    SetConsoleCtrlHandler(restore_on_ctrl, TRUE);
    if (!SetConsoleMode(h, mode & ~(DWORD)ENABLE_ECHO_INPUT)) {
        g_hidden = 0;
        SetConsoleCtrlHandler(restore_on_ctrl, FALSE);
        return 0;
    }
    return 1;
}

static void echo_on(void)
{
    if (g_hidden) {
        SetConsoleMode(g_con, g_con_mode);
        g_hidden = 0;
        SetConsoleCtrlHandler(restore_on_ctrl, FALSE);
    }
}

#else /* POSIX */

static struct termios        g_saved;
static int                   g_fd = -1;
static volatile sig_atomic_t g_hidden;
static struct sigaction      g_old_int;
static struct sigaction      g_old_term;

/* SIGINT / SIGTERM while the echo is off: put it back (tcsetattr and
 * sigaction are async-signal-safe), reinstate the previous action and
 * re-raise, so the process ends exactly as it would have. */
static void restore_on_signal(int sig)
{
    if (g_hidden) {
        (void)tcsetattr(g_fd, TCSANOW, &g_saved);
        g_hidden = 0;
    }
    (void)sigaction(sig, sig == SIGINT ? &g_old_int : &g_old_term, NULL);
    (void)raise(sig);
}

static int echo_off(FILE *in)
{
    struct termios t;
    struct sigaction sa;
    int fd = fileno(in);

    if (fd < 0 || !isatty(fd) || tcgetattr(fd, &g_saved) != 0) {
        return 0;
    }
    g_fd = fd;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = restore_on_signal;
    sigemptyset(&sa.sa_mask);
    (void)sigaction(SIGINT, &sa, &g_old_int);
    (void)sigaction(SIGTERM, &sa, &g_old_term);
    g_hidden = 1;
    t = g_saved;
    t.c_lflag &= ~(tcflag_t)ECHO;
    if (tcsetattr(fd, TCSAFLUSH, &t) != 0) {
        g_hidden = 0;
        (void)sigaction(SIGINT, &g_old_int, NULL);
        (void)sigaction(SIGTERM, &g_old_term, NULL);
        return 0;
    }
    return 1;
}

static void echo_on(void)
{
    if (g_hidden) {
        (void)tcsetattr(g_fd, TCSAFLUSH, &g_saved);
        g_hidden = 0;
        (void)sigaction(SIGINT, &g_old_int, NULL);
        (void)sigaction(SIGTERM, &g_old_term, NULL);
    }
}

#endif

/* --- prompts -------------------------------------------------------------- */

void hem_prompt_init(hem_prompt *p, FILE *in, FILE *out, FILE *err)
{
    p->in = (in != NULL) ? in : stdin;
    p->out = (out != NULL) ? out : stdout;
    p->err = (err != NULL) ? err : stderr;
    p->noticed = 0;
}

int hem_prompt_is_tty(const hem_prompt *p)
{
#ifdef _WIN32
    return _isatty(_fileno(p->in)) != 0;
#else
    int fd = fileno(p->in);
    return fd >= 0 && isatty(fd);
#endif
}

/* One line, the ending removed; a line longer than the buffer is consumed
 * to its end and reported (the partial text wiped — it may be a secret). */
static int read_line(FILE *in, char *buf, size_t cap)
{
    size_t len;

    if (cap < 2 || cap > 65536u) {
        return HEM_PROMPT_TOO_LONG;
    }
    if (fgets(buf, (int)cap, in) == NULL) {
        buf[0] = '\0';
        return HEM_PROMPT_EOF;
    }
    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[--len] = '\0';
    } else if (!feof(in)) {
        /* No line ending in the buffer: the text either filled it exactly
         * (the ending is next) or is longer. */
        int c = fgetc(in);
        if (c != '\n' && c != EOF) {
            while ((c = fgetc(in)) != EOF && c != '\n') {
            }
            memset(buf, 0, cap);
            return HEM_PROMPT_TOO_LONG;
        }
    }
    if (len > 0 && buf[len - 1] == '\r') {
        buf[--len] = '\0';
    }
    return HEM_PROMPT_OK;
}

static void ask(hem_prompt *p, const char *question)
{
    if (!p->noticed && !hem_prompt_is_tty(p)) {
        fprintf(p->err, "notice: input is not a terminal - answers are read "
                        "as plain lines\n");
        p->noticed = 1;
    }
    fputs(question, p->out);
    fflush(p->out);
}

/* Nothing echoes an answer that does not come from a terminal: end the
 * prompt's line ourselves so a transcript keeps one prompt per line. */
static void end_line(hem_prompt *p, int rc)
{
    if (rc != HEM_PROMPT_EOF && !hem_prompt_is_tty(p)) {
        fputc('\n', p->out);
        fflush(p->out);
    }
}

int hem_prompt_line(hem_prompt *p, const char *question, char *buf, size_t cap)
{
    int rc;

    ask(p, question);
    rc = read_line(p->in, buf, cap);
    end_line(p, rc);
    return rc;
}

int hem_prompt_secret(hem_prompt *p, const char *question, char *buf, size_t cap)
{
    int hidden, rc;

    ask(p, question);
    hidden = echo_off(p->in);
    rc = read_line(p->in, buf, cap);
    if (hidden) {
        echo_on();
        fputc('\n', p->out);      /* the Enter was not echoed either */
        fflush(p->out);
    } else {
        end_line(p, rc);
    }
    return rc;
}
