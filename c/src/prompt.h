/*
 * prompt.h — line prompts for hem-tool's interactive commands, with a
 * no-echo variant for secrets (passphrases, mnemonics).
 *
 * implements: REQ-TOOL-028 (the shared no-echo prompt helper)
 *
 * Echo is switched off only when the input is a terminal: termios ECHO on
 * POSIX, SetConsoleMode without ENABLE_ECHO_INPUT on Windows — and restored
 * on every return path, and on Ctrl-C (a SIGINT handler / console control
 * handler restores it before the default action ends the process). When the
 * input is NOT a terminal (a pipe, a file, the unit tests' scripted streams)
 * every answer is read as a plain line and the helper says so once on the
 * error stream.
 */
#ifndef HEM_PROMPT_H
#define HEM_PROMPT_H

#include <stddef.h>
#include <stdio.h>

/* Results. */
#define HEM_PROMPT_OK       0
#define HEM_PROMPT_EOF      1   /* input closed: the caller cancels */
#define HEM_PROMPT_TOO_LONG 2   /* the line did not fit; the rest was discarded */

typedef struct {
    FILE *in;            /* answers (NULL → stdin) */
    FILE *out;           /* questions (NULL → stdout) */
    FILE *err;           /* the one-time non-terminal notice (NULL → stderr) */
    int   noticed;       /* internal: the notice was printed */
} hem_prompt;

void hem_prompt_init(hem_prompt *p, FILE *in, FILE *out, FILE *err);

/* Nonzero when p->in is an interactive terminal. */
int hem_prompt_is_tty(const hem_prompt *p);

/*
 * Print `question` (no newline added) and read one line into buf (capacity
 * `cap`), the line ending (\n or \r\n) removed, everything else verbatim.
 */
int hem_prompt_line(hem_prompt *p, const char *question, char *buf, size_t cap);

/* As hem_prompt_line with the echo off on a terminal (a newline is printed
 * after the hidden answer). */
int hem_prompt_secret(hem_prompt *p, const char *question, char *buf, size_t cap);

#endif /* HEM_PROMPT_H */
