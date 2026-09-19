/*
 * test_common.h — Minimal unit test harness
 *
 * Self-contained assertion framework. No external dependencies.
 * Provides: TEST_OK / TEST_ASSERT / TEST_ASSERT_UINT_EQ / etc.
 * Each test returns 0 on pass, non-zero on failure.
 */

#ifndef HYPERSINV_TEST_COMMON_H
#define HYPERSINV_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*===========================================================================
 * Colors (optional — only if stdout supports ANSI)
 *===========================================================================*/

#ifndef TEST_NO_COLOR
#define TEST_COLOR_RED     "\x1b[31m"
#define TEST_COLOR_GREEN   "\x1b[32m"
#define TEST_COLOR_RESET   "\x1b[0m"
#else
#define TEST_COLOR_RED     ""
#define TEST_COLOR_GREEN   ""
#define TEST_COLOR_RESET   ""
#endif

/*===========================================================================
 * Assertion Macros
 *===========================================================================*/

static int _test_failures = 0;
static int _test_total    = 0;
static const char* _test_current_name = "";

#define TEST_ASSERT(cond, msg) do {                              \
    _test_total++;                                               \
    if (!(cond)) {                                               \
        fprintf(stderr, TEST_COLOR_RED                           \
                "  FAIL: %s:%d — %s — " msg "\n"                 \
                TEST_COLOR_RESET,                                \
                __FILE__, __LINE__, _test_current_name);         \
        _test_failures++;                                        \
    }                                                            \
} while(0)

#define TEST_ASSERT_UINT_EQ(actual, expected, label) do {        \
    _test_total++;                                               \
    uintmax_t _act = (uintmax_t)(actual);                        \
    uintmax_t _exp = (uintmax_t)(expected);                      \
    if (_act != _exp) {                                          \
        fprintf(stderr, TEST_COLOR_RED                           \
                "  FAIL: %s:%d — %s — %s: expected %llu, "      \
                "got %llu\n"                                     \
                TEST_COLOR_RESET,                                \
                __FILE__, __LINE__, _test_current_name,          \
                label, (unsigned long long)_exp,                 \
                (unsigned long long)_act);                       \
        _test_failures++;                                        \
    }                                                            \
} while(0)

#define TEST_ASSERT_INT_EQ(actual, expected, label) do {         \
    _test_total++;                                               \
    intmax_t _act = (intmax_t)(actual);                          \
    intmax_t _exp = (intmax_t)(expected);                        \
    if (_act != _exp) {                                          \
        fprintf(stderr, TEST_COLOR_RED                           \
                "  FAIL: %s:%d — %s — %s: expected %lld, "      \
                "got %lld\n"                                     \
                TEST_COLOR_RESET,                                \
                __FILE__, __LINE__, _test_current_name,          \
                label, (long long)_exp,                          \
                (long long)_act);                                \
        _test_failures++;                                        \
    }                                                            \
} while(0)

#define TEST_ASSERT_TRUE(cond, label) do {                       \
    _test_total++;                                               \
    if (!(cond)) {                                               \
        fprintf(stderr, TEST_COLOR_RED                           \
                "  FAIL: %s:%d — %s — %s: expected true\n"      \
                TEST_COLOR_RESET,                                \
                __FILE__, __LINE__, _test_current_name, label);  \
        _test_failures++;                                        \
    }                                                            \
} while(0)

#define TEST_ASSERT_FALSE(cond, label) do {                      \
    _test_total++;                                               \
    if (cond) {                                                  \
        fprintf(stderr, TEST_COLOR_RED                           \
                "  FAIL: %s:%d — %s — %s: expected false\n"     \
                TEST_COLOR_RESET,                                \
                __FILE__, __LINE__, _test_current_name, label);  \
        _test_failures++;                                        \
    }                                                            \
} while(0)

#define TEST_ASSERT_NULL(ptr, label) do {                        \
    _test_total++;                                               \
    if ((ptr) != NULL) {                                         \
        fprintf(stderr, TEST_COLOR_RED                           \
                "  FAIL: %s:%d — %s — %s: expected NULL, "      \
                "got %p\n"                                       \
                TEST_COLOR_RESET,                                \
                __FILE__, __LINE__, _test_current_name,          \
                label, (void*)(ptr));                            \
        _test_failures++;                                        \
    }                                                            \
} while(0)

#define TEST_ASSERT_NOT_NULL(ptr, label) do {                    \
    _test_total++;                                               \
    if ((ptr) == NULL) {                                         \
        fprintf(stderr, TEST_COLOR_RED                           \
                "  FAIL: %s:%d — %s — %s: expected non-NULL\n"  \
                TEST_COLOR_RESET,                                \
                __FILE__, __LINE__, _test_current_name, label);  \
        _test_failures++;                                        \
    }                                                            \
} while(0)

/*===========================================================================
 * Test Runner
 *===========================================================================*/

typedef int (*test_fn_t)(void);

#define TEST_RUN(name) do {                                      \
    _test_current_name = #name;                                  \
    int _ret = name();                                           \
    if (_ret != 0) {                                             \
        _test_failures++;                                        \
        fprintf(stderr, TEST_COLOR_RED                           \
                "  FAIL: test function '%s' returned %d\n"       \
                TEST_COLOR_RESET, #name, _ret);                  \
    }                                                            \
} while(0)

#define TEST_MAIN_BEGIN() \
    int main(void) {                                      \
        fprintf(stdout, TEST_COLOR_GREEN);                \
        fflush(stdout);

#define TEST_MAIN_END() \
        fprintf(stdout, TEST_COLOR_RESET                  \
                "\nTotal: %d assertions, %d failures\n",  \
                _test_total, _test_failures);             \
        return _test_failures > 0 ? 1 : 0;               \
    }

#endif /* HYPERSINV_TEST_COMMON_H */
