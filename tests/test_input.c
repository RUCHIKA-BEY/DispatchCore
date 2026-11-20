/*
 * test_input.c — tests for the safe input module (Phase 9).
 *
 * Input is written to small files under build/test_data/ and read back in
 * BINARY mode ("rb"), so that "\r\n" line endings reach the code unchanged
 * on Windows too (text mode would silently turn them into "\n").
 *
 * Build and run:   make test
 */
#include "input.h"
#include "test_util.h"

#include <stdlib.h>
#include <string.h>

#define IN_PATH  "build/test_data/input_in.txt"
#define OUT_PATH "build/test_data/input_out.txt"

/* ---------- helpers ---------- */

static int write_bytes(const char *path, const char *bytes, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return 0;
    int ok = fwrite(bytes, 1, len, f) == len;
    if (fclose(f) != 0)
        ok = 0;
    return ok;
}

static FILE *open_input(const char *text)
{
    if (!write_bytes(IN_PATH, text, strlen(text)))
        return NULL;
    return fopen(IN_PATH, "rb");
}

/* Reads the whole output file into a malloc'd string (NULL on failure). */
static char *read_all(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL)
        return NULL;
    size_t cap = 16 * 1024;
    char *buf = malloc(cap + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

/* Counts how many times piece occurs in text. */
static int count_of(const char *text, const char *piece)
{
    int n = 0;
    size_t len = strlen(piece);
    for (const char *p = text; p != NULL && (p = strstr(p, piece)) != NULL; p += len)
        n++;
    return n;
}

/* ---------- tests ---------- */

static void test_parse_int(void)
{
    TEST_SECTION("input_parse_int");
    int v = -1;
    CHECK_EQ_INT(input_parse_int("42", 0, 100, &v), PARSE_OK);
    CHECK_EQ_INT(v, 42);
    CHECK_EQ_INT(input_parse_int("  7  ", 0, 100, &v), PARSE_OK);   /* spaces around: fine */
    CHECK_EQ_INT(v, 7);
    CHECK_EQ_INT(input_parse_int("+5", 0, 100, &v), PARSE_OK);
    CHECK_EQ_INT(v, 5);
    CHECK_EQ_INT(input_parse_int("-3", -5, 5, &v), PARSE_OK);
    CHECK_EQ_INT(v, -3);

    v = 123;
    CHECK_EQ_INT(input_parse_int("", 0, 100, &v), PARSE_EMPTY);
    CHECK_EQ_INT(input_parse_int("   \t", 0, 100, &v), PARSE_EMPTY);
    CHECK_EQ_INT(input_parse_int("abc", 0, 100, &v), PARSE_NOT_NUMBER);
    CHECK_EQ_INT(input_parse_int("12abc", 0, 100, &v), PARSE_NOT_NUMBER);
    CHECK_EQ_INT(input_parse_int("1.5", 0, 100, &v), PARSE_NOT_NUMBER);
    CHECK_EQ_INT(input_parse_int("1 2", 0, 100, &v), PARSE_NOT_NUMBER);
    CHECK_EQ_INT(input_parse_int("0x10", 0, 100, &v), PARSE_NOT_NUMBER);  /* base 10 only */
    CHECK_EQ_INT(input_parse_int("-", 0, 100, &v), PARSE_NOT_NUMBER);
    CHECK_EQ_INT(input_parse_int("0", 1, 100, &v), PARSE_OUT_OF_RANGE);
    CHECK_EQ_INT(input_parse_int("101", 1, 100, &v), PARSE_OUT_OF_RANGE);
    CHECK_EQ_INT(input_parse_int("99999999999999999999999", 1, 100, &v), PARSE_OUT_OF_RANGE);
    CHECK_EQ_INT(input_parse_int("-99999999999999999999999", 1, 100, &v), PARSE_OUT_OF_RANGE);
    CHECK_EQ_INT(v, 123);                         /* never written on failure */
    CHECK_EQ_INT(input_parse_int(NULL, 0, 1, &v), PARSE_EMPTY);
    CHECK_EQ_INT(input_parse_int("1", 0, 1, NULL), PARSE_EMPTY);
}

static void test_read_line(void)
{
    TEST_SECTION("input_read_line: endings, last line, end of input");
    char buf[INPUT_LINE_MAX];
    FILE *in = open_input("hello\nwin\r\n\nlast");
    CHECK(in != NULL);
    if (in == NULL)
        return;
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_OK);
    CHECK_STR_EQ(buf, "hello");
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_OK);
    CHECK_STR_EQ(buf, "win");                     /* "\r\n" removed */
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_OK);
    CHECK_STR_EQ(buf, "");                        /* an empty line is still a line */
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_OK);
    CHECK_STR_EQ(buf, "last");                    /* no final '\n': accepted */
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_EOF);
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_EOF);   /* stays at EOF */
    fclose(in);

    CHECK_EQ_INT(input_read_line(NULL, buf, sizeof buf), INPUT_EOF);
    CHECK_EQ_INT(input_read_line(stdin, buf, 1), INPUT_EOF);         /* buffer too small */
}

static void test_long_lines(void)
{
    TEST_SECTION("input_read_line: the longest accepted line and longer ones");
    /* INPUT_LINE_MAX = 128: fgets reads at most 127 characters. A line of
     * 126 characters plus '\n' fits; 127 characters do not. */
    char text[600];
    size_t pos = 0;
    memset(text + pos, 'a', 126);
    pos += 126;
    text[pos++] = '\n';
    memset(text + pos, 'b', 127);
    pos += 127;
    text[pos++] = '\n';
    memset(text + pos, 'c', 300);
    pos += 300;
    text[pos++] = '\n';
    memcpy(text + pos, "ok\n", 4);                /* includes the '\0' */

    FILE *in = open_input(text);
    CHECK(in != NULL);
    if (in == NULL)
        return;
    char buf[INPUT_LINE_MAX];
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_OK);
    CHECK_EQ_INT((int)strlen(buf), 126);
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_TOO_LONG);
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_TOO_LONG);
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_OK);   /* the tail was discarded */
    CHECK_STR_EQ(buf, "ok");
    CHECK_EQ_INT(input_read_line(in, buf, sizeof buf), INPUT_EOF);
    fclose(in);
}

static void test_read_int(void)
{
    TEST_SECTION("input_read_int re-prompts until a valid number");
    FILE *in = open_input("\nabc\n12abc\n99\n  7  \n");
    FILE *out = fopen(OUT_PATH, "wb");
    CHECK(in != NULL && out != NULL);
    if (in == NULL || out == NULL) {
        if (in != NULL)
            fclose(in);
        if (out != NULL)
            fclose(out);
        return;
    }
    Console con = {in, out, 0};
    int v = 0;
    CHECK_EQ_INT(input_read_int(&con, "Pick", 1, 10, &v), INPUT_OK);
    CHECK_EQ_INT(v, 7);
    CHECK_EQ_INT(input_read_int(&con, "Again", 1, 10, &v), INPUT_EOF);   /* input ended */
    CHECK_EQ_INT(v, 7);
    fclose(in);
    fclose(out);

    char *text = read_all(OUT_PATH);
    CHECK(text != NULL);
    if (text != NULL) {
        CHECK_EQ_INT(count_of(text, "Pick (1-10): "), 5);     /* 4 rejected + 1 accepted */
        CHECK_EQ_INT(count_of(text, "Please type a number.\n"), 1);
        CHECK_EQ_INT(count_of(text, "\"abc\" is not a whole number.\n"), 1);
        CHECK_EQ_INT(count_of(text, "\"12abc\" is not a whole number.\n"), 1);
        CHECK_EQ_INT(count_of(text, "Please type a number from 1 to 10.\n"), 1);
        CHECK_EQ_INT(count_of(text, "Again (1-10): \n"), 1);  /* EOF ends the prompt line */
        free(text);
    }

    /* Silent console (out == NULL), and an over-long line before a good one. */
    char longtext[400];
    memset(longtext, '9', 300);
    memcpy(longtext + 300, "\n3\n", 4);
    in = open_input(longtext);
    CHECK(in != NULL);
    if (in != NULL) {
        Console quiet = {in, NULL, 0};
        CHECK_EQ_INT(input_read_int(&quiet, "Pick", 1, 10, &v), INPUT_OK);
        CHECK_EQ_INT(v, 3);
        fclose(in);
    }
    CHECK_EQ_INT(input_read_int(NULL, "x", 0, 1, &v), INPUT_EOF);
}

static void test_echo(void)
{
    TEST_SECTION("echo mode prints each answer");
    FILE *in = open_input("5\n");
    FILE *out = fopen(OUT_PATH, "wb");
    CHECK(in != NULL && out != NULL);
    if (in == NULL || out == NULL) {
        if (in != NULL)
            fclose(in);
        if (out != NULL)
            fclose(out);
        return;
    }
    Console con = {in, out, 1};
    int v = 0;
    CHECK_EQ_INT(input_read_int(&con, "Choice", 0, 9, &v), INPUT_OK);
    fclose(in);
    fclose(out);
    char *text = read_all(OUT_PATH);
    CHECK(text != NULL && strcmp(text, "Choice (0-9): 5\n") == 0);
    free(text);
}

static void test_yes_no(void)
{
    TEST_SECTION("input_read_yes_no");
    FILE *in = open_input("maybe\nYES\n n \nNo\ny");
    CHECK(in != NULL);
    if (in == NULL)
        return;
    Console con = {in, NULL, 0};
    int yes = -1;
    CHECK_EQ_INT(input_read_yes_no(&con, "Sure?", &yes), INPUT_OK);   /* "maybe" rejected */
    CHECK_EQ_INT(yes, 1);
    CHECK_EQ_INT(input_read_yes_no(&con, "Sure?", &yes), INPUT_OK);
    CHECK_EQ_INT(yes, 0);
    CHECK_EQ_INT(input_read_yes_no(&con, "Sure?", &yes), INPUT_OK);
    CHECK_EQ_INT(yes, 0);
    CHECK_EQ_INT(input_read_yes_no(&con, "Sure?", &yes), INPUT_OK);   /* last line, no '\n' */
    CHECK_EQ_INT(yes, 1);
    CHECK_EQ_INT(input_read_yes_no(&con, "Sure?", &yes), INPUT_EOF);
    fclose(in);
}

int main(void)
{
    test_parse_int();
    test_read_line();
    test_long_lines();
    test_read_int();
    test_echo();
    test_yes_no();
    return TEST_SUMMARY();
}
