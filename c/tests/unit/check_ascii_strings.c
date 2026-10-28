/*
 * check_ascii_strings.c — the REQ-TOOL-029 gate: every string and character
 * literal in the tool sources (c/src/; vendored code excluded by the caller)
 * is 7-bit ASCII. Copied from the SDK's REQ-API-009 gate at 1.2.0. A UTF-8 em dash in a message prints
 * as mojibake in a Windows console that uses a legacy code page (found
 * 2026-10-10 on the first Windows test of the release binary). Comments are
 * not checked — they never reach a user. The gate is the implementation of
 * the rule as well as its proof (the sources themselves carry no tag for it).
 *
 * implements: REQ-TOOL-029
 * verifies: REQ-TOOL-029
 *
 * Usage: check_ascii_strings <file>...
 * Exit 0 when clean; 1 with one "file:line" report per offending line; 2 when
 * a file cannot be read.
 */
#include <stdio.h>

enum lex_state { CODE, LINE_COMMENT, BLOCK_COMMENT, STRING_LIT, CHAR_LIT };

/* Returns the number of offending lines, or -1 if the file cannot be read. */
static int scan(const char *path)
{
    FILE *f = fopen(path, "rb");
    enum lex_state st = CODE;
    long line = 1, reported = 0;
    int c, next, bad = 0;

    if (f == NULL) {
        fprintf(stderr, "%s: cannot open\n", path);
        return -1;
    }
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') {
            line++;
        }
        switch (st) {
        case CODE:
            if (c == '/') {
                next = fgetc(f);
                if (next == '/') {
                    st = LINE_COMMENT;
                } else if (next == '*') {
                    st = BLOCK_COMMENT;
                } else if (next != EOF) {
                    ungetc(next, f);
                }
            } else if (c == '"') {
                st = STRING_LIT;
            } else if (c == '\'') {
                st = CHAR_LIT;
            }
            break;
        case LINE_COMMENT:
            if (c == '\n') {
                st = CODE;
            }
            break;
        case BLOCK_COMMENT:
            if (c == '*') {
                next = fgetc(f);
                if (next == '/') {
                    st = CODE;
                } else if (next != EOF) {
                    ungetc(next, f);
                }
            }
            break;
        case STRING_LIT:
        case CHAR_LIT:
            if (c == '\\') {                /* skip the escaped character */
                next = fgetc(f);
                if (next == '\n') {
                    line++;
                }
            } else if ((st == STRING_LIT && c == '"') || (st == CHAR_LIT && c == '\'')) {
                st = CODE;
            } else if (c > 0x7f && line != reported) {
                printf("%s:%ld: non-ASCII byte in a %s literal\n", path, line,
                       st == STRING_LIT ? "string" : "character");
                reported = line;
                bad++;
            }
            break;
        }
    }
    fclose(f);
    return bad;
}

int main(int argc, char **argv)
{
    int i, n, bad = 0, unreadable = 0;

    for (i = 1; i < argc; i++) {
        n = scan(argv[i]);
        if (n < 0) {
            unreadable = 1;
        } else {
            bad += n;
        }
    }
    if (unreadable) {
        return 2;
    }
    if (bad > 0) {
        printf("%d line(s) with non-ASCII text in literals (REQ-API-009): "
               "use ASCII, e.g. \" - \" for an em dash\n", bad);
        return 1;
    }
    printf("%d file(s): every string/character literal is ASCII\n", argc - 1);
    return 0;
}
