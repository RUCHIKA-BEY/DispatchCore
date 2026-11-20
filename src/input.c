/*
 * input.c — safe line-based input (see input.h).
 */
#include "input.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

InputResult input_read_line(FILE *in, char *buf, size_t size)
{
    if (in == NULL || buf == NULL || size < 2)
        return INPUT_EOF;          /* nothing sensible can be read */

    if (fgets(buf, (int)size, in) == NULL) {
        buf[0] = '\0';
        return INPUT_EOF;          /* end of input, or a read error */
    }

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[--len] = '\0';                     /* complete line: drop '\n' */
        if (len > 0 && buf[len - 1] == '\r')
            buf[--len] = '\0';                 /* Windows "\r\n" */
        return INPUT_OK;
    }

    /* No '\n' in the buffer. Either this is the last line of the input and
     * it has no line ending (fgets stopped because of end of file), or the
     * line was longer than the buffer. */
    if (feof(in)) {
        if (len > 0 && buf[len - 1] == '\r')
            buf[--len] = '\0';
        return INPUT_OK;
    }

    /* Too long: throw away the rest of the line, so that its tail is not
     * mistaken for the NEXT answer. */
    int c;
    while ((c = fgetc(in)) != '\n' && c != EOF)
        ;
    buf[0] = '\0';
    return INPUT_TOO_LONG;
}

ParseResult input_parse_int(const char *text, int min, int max, int *out)
{
    if (text == NULL || out == NULL)
        return PARSE_EMPTY;

    const char *p = text;
    while (isspace((unsigned char)*p))   /* cast: isspace needs a non-negative value */
        p++;
    if (*p == '\0')
        return PARSE_EMPTY;

    char *end = NULL;
    errno = 0;
    long value = strtol(p, &end, 10);
    if (end == p)
        return PARSE_NOT_NUMBER;         /* no digits at all, e.g. "abc" */

    const char *rest = end;
    while (isspace((unsigned char)*rest))
        rest++;
    if (*rest != '\0')
        return PARSE_NOT_NUMBER;         /* trailing garbage, e.g. "12abc", "1.5" */

    /* ERANGE: the number does not even fit in a long. Comparing with
     * min/max as long values is safe because int always fits in long. */
    if (errno == ERANGE || value < (long)min || value > (long)max)
        return PARSE_OUT_OF_RANGE;

    *out = (int)value;
    return PARSE_OK;
}

/* Reads one answer line, echoing it when asked. Prints a message for an
 * over-long line. Returns INPUT_OK, INPUT_EOF or INPUT_TOO_LONG. */
static InputResult read_answer(Console *con, char *line, size_t size)
{
    InputResult r = input_read_line(con->in, line, size);
    if (con->out == NULL)
        return r;
    if (r == INPUT_EOF) {
        fprintf(con->out, "\n");        /* finish the prompt line neatly */
    } else if (r == INPUT_TOO_LONG) {
        if (con->echo)
            fprintf(con->out, "(too long)\n");
        fprintf(con->out, "  Input too long (at most %d characters). Try again.\n",
                INPUT_LINE_MAX - 2);
    } else if (con->echo) {
        fprintf(con->out, "%s\n", line);
    }
    return r;
}

InputResult input_read_int(Console *con, const char *prompt, int min, int max, int *value)
{
    if (con == NULL || prompt == NULL || value == NULL)
        return INPUT_EOF;

    char line[INPUT_LINE_MAX];
    for (;;) {
        if (con->out != NULL) {
            fprintf(con->out, "%s (%d-%d): ", prompt, min, max);
            fflush(con->out);           /* show the prompt before waiting */
        }
        InputResult r = read_answer(con, line, sizeof line);
        if (r == INPUT_EOF)
            return INPUT_EOF;
        if (r == INPUT_TOO_LONG)
            continue;

        ParseResult p = input_parse_int(line, min, max, value);
        if (p == PARSE_OK)
            return INPUT_OK;
        if (con->out == NULL)
            continue;
        if (p == PARSE_EMPTY)
            fprintf(con->out, "  Please type a number.\n");
        else if (p == PARSE_NOT_NUMBER)
            fprintf(con->out, "  \"%s\" is not a whole number.\n", line);
        else
            fprintf(con->out, "  Please type a number from %d to %d.\n", min, max);
    }
}

/* Case-insensitive comparison of a whole word, without trailing spaces. */
static int word_equals(const char *text, const char *word)
{
    while (isspace((unsigned char)*text))
        text++;
    while (*word != '\0') {
        if (tolower((unsigned char)*text) != *word)
            return 0;
        text++;
        word++;
    }
    while (isspace((unsigned char)*text))
        text++;
    return *text == '\0';
}

InputResult input_read_yes_no(Console *con, const char *prompt, int *yes)
{
    if (con == NULL || prompt == NULL || yes == NULL)
        return INPUT_EOF;

    char line[INPUT_LINE_MAX];
    for (;;) {
        if (con->out != NULL) {
            fprintf(con->out, "%s (y/n): ", prompt);
            fflush(con->out);
        }
        InputResult r = read_answer(con, line, sizeof line);
        if (r == INPUT_EOF)
            return INPUT_EOF;
        if (r == INPUT_TOO_LONG)
            continue;
        if (word_equals(line, "y") || word_equals(line, "yes")) {
            *yes = 1;
            return INPUT_OK;
        }
        if (word_equals(line, "n") || word_equals(line, "no")) {
            *yes = 0;
            return INPUT_OK;
        }
        if (con->out != NULL)
            fprintf(con->out, "  Please answer y or n.\n");
    }
}
