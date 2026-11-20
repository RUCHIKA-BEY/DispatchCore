/*
 * input.h — safe, line-based console input (Phase 9).
 *
 * Every value the user types is read as ONE WHOLE LINE with fgets and then
 * parsed with strtol. We never use scanf("%d"): on input like "abc" it
 * leaves the characters in the buffer, and a menu loop would then spin
 * forever on the same bad input.
 *
 * All functions work on a Console (an input stream, an output stream and an
 * echo flag) instead of stdin/stdout directly, so that tests can drive them
 * from files.
 *
 * End of input (Ctrl+Z then Enter on Windows, Ctrl+D on Linux, or the end
 * of a scripted input file) is reported as INPUT_EOF, never as an error
 * loop, so the program can always exit cleanly.
 *
 * Depends on: nothing but the C library.
 */
#ifndef DC_INPUT_H
#define DC_INPUT_H

#include <stddef.h>
#include <stdio.h>

/* Longest accepted line is INPUT_LINE_MAX - 2 characters (the buffer also
 * needs room for '\n' and '\0'). Longer lines are rejected as a whole. */
#define INPUT_LINE_MAX 128

typedef struct {
    FILE *in;     /* where answers are read from (stdin, or a test file) */
    FILE *out;    /* where prompts and messages go (may be NULL = silent) */
    int   echo;   /* 1: print each line read, so scripted runs are readable */
} Console;

typedef enum {
    INPUT_OK = 0,
    INPUT_EOF,        /* end of input (or a read error): stop asking */
    INPUT_TOO_LONG    /* the line did not fit; the rest was discarded */
} InputResult;

typedef enum {
    PARSE_OK = 0,
    PARSE_EMPTY,          /* nothing but spaces                     */
    PARSE_NOT_NUMBER,     /* "abc", "12abc", "1.5", "1 2"           */
    PARSE_OUT_OF_RANGE    /* a whole number, but outside min..max   */
} ParseResult;

/* Reads one line into buf (size >= 2), removing the line ending ("\n" or
 * "\r\n"). A last line without '\n' is accepted. A line that does not fit
 * is consumed up to its '\n' and INPUT_TOO_LONG is returned. */
InputResult input_read_line(FILE *in, char *buf, size_t size);

/* Parses a whole line as a base-10 integer in min..max. Leading and
 * trailing spaces are allowed; anything else around the number is not.
 * *out is written only on PARSE_OK. */
ParseResult input_parse_int(const char *text, int min, int max, int *out);

/* Prompts "<prompt> (min-max): " and re-prompts with a reason until a valid
 * number is typed. Returns INPUT_OK (value in *value) or INPUT_EOF. */
InputResult input_read_int(Console *con, const char *prompt, int min, int max, int *value);

/* Prompts "<prompt> (y/n): " and accepts y, yes, n, no in any letter case.
 * *yes is 1 or 0. Returns INPUT_OK or INPUT_EOF. */
InputResult input_read_yes_no(Console *con, const char *prompt, int *yes);

#endif /* DC_INPUT_H */
