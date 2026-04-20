/**
 * @file unity_lite.h
 * @brief Miniaturni unit-test framework inspirovany Unity (ThrowTheSwitch).
 *
 * Zamerne velmi jednoducha implementace — prehledne, jedno zavisi na
 * stdlib/stdio. Pro pokrocilejsi funkce (parametrized tests, tear-down
 * hooks, test groups) doporucuji prejit na plne Unity nebo cmocka.
 *
 * Pouziti:
 *
 *   void test_my_feature(void) {
 *       TEST_ASSERT_EQUAL_INT(42, my_function());
 *       TEST_ASSERT_STRING_EQUAL("hello", my_string());
 *   }
 *
 *   int main(void) {
 *       UNITY_BEGIN();
 *       RUN_TEST(test_my_feature);
 *       return UNITY_END();
 *   }
 */
#ifndef UNITY_LITE_H
#define UNITY_LITE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Citace celkoveho pruběhu (jedna instance na bezici binarku). */
extern int _unity_tests_run;
extern int _unity_tests_failed;
extern int _unity_current_test_failed;
extern const char *_unity_current_test_name;

/* ========================================================================= */
/*  Makra pro kontrolu predpokladu v testu                                   */
/* ========================================================================= */

#define _UNITY_FAIL(fmt, ...) \
    do { \
        printf("  FAIL %s:%d: " fmt "\n", __FILE__, __LINE__, ##__VA_ARGS__); \
        _unity_current_test_failed = 1; \
        return; \
    } while (0)

#define TEST_ASSERT(cond) \
    do { if (!(cond)) _UNITY_FAIL("assert failed: %s", #cond); } while (0)

#define TEST_ASSERT_TRUE(cond)   TEST_ASSERT(cond)
#define TEST_ASSERT_FALSE(cond)  TEST_ASSERT(!(cond))

#define TEST_ASSERT_EQUAL_INT(expected, actual) \
    do { \
        long _e = (long)(expected), _a = (long)(actual); \
        if (_e != _a) _UNITY_FAIL("expected %ld, got %ld (%s)", _e, _a, #actual); \
    } while (0)

#define TEST_ASSERT_EQUAL_UINT(expected, actual) \
    do { \
        unsigned long _e = (unsigned long)(expected), _a = (unsigned long)(actual); \
        if (_e != _a) _UNITY_FAIL("expected %lu, got %lu (%s)", _e, _a, #actual); \
    } while (0)

#define TEST_ASSERT_EQUAL_HEX(expected, actual) \
    do { \
        unsigned long _e = (unsigned long)(expected), _a = (unsigned long)(actual); \
        if (_e != _a) _UNITY_FAIL("expected 0x%lX, got 0x%lX (%s)", _e, _a, #actual); \
    } while (0)

#define TEST_ASSERT_EQUAL_PTR(expected, actual) \
    do { \
        const void *_e = (expected), *_a = (actual); \
        if (_e != _a) _UNITY_FAIL("expected pointer %p, got %p", _e, _a); \
    } while (0)

#define TEST_ASSERT_NULL(ptr) \
    do { if ((ptr) != NULL) _UNITY_FAIL("expected NULL, got non-NULL (%s)", #ptr); } while (0)

#define TEST_ASSERT_NOT_NULL(ptr) \
    do { if ((ptr) == NULL) _UNITY_FAIL("expected non-NULL (%s)", #ptr); } while (0)

#define TEST_ASSERT_STRING_EQUAL(expected, actual) \
    do { \
        const char *_e = (expected), *_a = (actual); \
        if (_a == NULL || strcmp(_e, _a) != 0) \
            _UNITY_FAIL("expected \"%s\", got \"%s\"", _e, _a ? _a : "(null)"); \
    } while (0)

#define TEST_ASSERT_EQUAL_FLOAT(expected, actual, tolerance) \
    do { \
        double _e = (double)(expected), _a = (double)(actual); \
        double _d = _a - _e; if (_d < 0) _d = -_d; \
        if (_d > (double)(tolerance)) \
            _UNITY_FAIL("expected %f ± %f, got %f", _e, (double)(tolerance), _a); \
    } while (0)

#define TEST_ASSERT_NAN(actual) \
    do { if (!isnan((double)(actual))) _UNITY_FAIL("expected NaN (%s)", #actual); } while (0)

#define TEST_ASSERT_EQUAL_MEMORY(expected, actual, len) \
    do { \
        const uint8_t *_e = (const uint8_t*)(expected); \
        const uint8_t *_a = (const uint8_t*)(actual); \
        unsigned long _n = (unsigned long)(len); \
        if (memcmp(_e, _a, (size_t)_n) != 0) { \
            printf("  FAIL %s:%d: memory mismatch (%lu bytes)\n", \
                   __FILE__, __LINE__, _n); \
            printf("    expected: "); \
            for (unsigned long i = 0; i < _n; i++) printf("%02X ", _e[i]); \
            printf("\n    got:      "); \
            for (unsigned long i = 0; i < _n; i++) printf("%02X ", _a[i]); \
            printf("\n"); \
            _unity_current_test_failed = 1; return; \
        } \
    } while (0)

/* ========================================================================= */
/*  Runner                                                                   */
/* ========================================================================= */

#define UNITY_BEGIN() \
    do { _unity_tests_run = 0; _unity_tests_failed = 0; \
         printf("=== Running tests ===\n"); } while (0)

#define RUN_TEST(fn) \
    do { \
        _unity_current_test_name = #fn; \
        _unity_current_test_failed = 0; \
        _unity_tests_run++; \
        printf("- %s ... ", #fn); fflush(stdout); \
        fn(); \
        if (_unity_current_test_failed) _unity_tests_failed++; \
        else printf("OK\n"); \
    } while (0)

#define UNITY_END() \
    (printf("\n=== %d tests, %d failed ===\n", \
            _unity_tests_run, _unity_tests_failed), _unity_tests_failed)

/* Deklarace pro test suity, ktere jsou v jinych .c souborech. */
#define DECLARE_TEST(fn) void fn(void)

#endif /* UNITY_LITE_H */
