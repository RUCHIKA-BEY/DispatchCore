/*
 * test_util.h — a minimal unit-test helper (no external framework).
 *
 * Each test program includes this header exactly once. The two counters are
 * `static`, so every test executable has its own private copy; they are the
 * only file-level variables in the project and exist only in test code.
 *
 * Usage:
 *     CHECK(ptr != NULL);
 *     CHECK_EQ_INT(graph_degree(g, 0), 2);
 *     return TEST_SUMMARY();   // at the end of main(): 0 = all passed
 */
#ifndef DC_TEST_UTIL_H
#define DC_TEST_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_tests_run    = 0;
static int g_tests_failed = 0;

/* do { ... } while (0) makes a multi-statement macro behave like one
 * statement, so it is safe inside an if/else without braces. */
#define CHECK(cond)                                                        \
    do {                                                                   \
        g_tests_run++;                                                     \
        if (!(cond)) {                                                     \
            g_tests_failed++;                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                  \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                     \
    do {                                                                   \
        long dc_actual_   = (long)(actual);                                \
        long dc_expected_ = (long)(expected);                              \
        g_tests_run++;                                                     \
        if (dc_actual_ != dc_expected_) {                                  \
            g_tests_failed++;                                              \
            printf("  FAIL %s:%d: %s == %ld, expected %ld\n",              \
                   __FILE__, __LINE__, #actual, dc_actual_, dc_expected_); \
        }                                                                  \
    } while (0)

#define CHECK_STR_EQ(actual, expected)                                     \
    do {                                                                   \
        const char *dc_a_ = (actual);                                      \
        const char *dc_e_ = (expected);                                    \
        g_tests_run++;                                                     \
        if (dc_a_ == NULL || strcmp(dc_a_, dc_e_) != 0) {                  \
            g_tests_failed++;                                              \
            printf("  FAIL %s:%d: %s == \"%s\", expected \"%s\"\n",        \
                   __FILE__, __LINE__, #actual,                            \
                   dc_a_ ? dc_a_ : "(null)", dc_e_);                       \
        }                                                                  \
    } while (0)

/* Deterministic pseudo-random numbers for tests (a linear congruential
 * generator). We avoid rand() because its sequence differs between C
 * libraries, so a failure seen on one machine could not be reproduced on
 * another. uint32_t is exactly 32 bits everywhere (unsigned long is 32 bits
 * on Windows but 64 on Linux); unsigned overflow wraps modulo 2^32, which is
 * defined behaviour. The caller owns the state, so no hidden globals.
 * Returns a value in 0 .. bound-1 (bound must be >= 1). */
static inline int test_random(uint32_t *state, int bound)
{
    *state = *state * 1103515245u + 12345u;
    return (int)((*state >> 16) % (uint32_t)bound);   /* high bits are more random */
}

/* Prints a section header so failures are easy to locate in the output. */
#define TEST_SECTION(title) printf("[test] %s\n", (title))

/* Prints totals; returns 0 if every check passed, 1 otherwise. */
#define TEST_SUMMARY()                                                     \
    (printf("\n%d checks, %d failed -> %s\n", g_tests_run, g_tests_failed, \
            g_tests_failed == 0 ? "ALL PASSED" : "FAILURES"),              \
     g_tests_failed == 0 ? 0 : 1)

#endif /* DC_TEST_UTIL_H */
