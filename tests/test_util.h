/* Minimal assert-based test runner (no external framework, no network fetch). */
#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include <stdint.h>
#include <stdio.h>

static int g_checks;
static int g_failures;
static int g_tests;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(cond)) {                                                                             \
            g_failures++;                                                                          \
            printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
        }                                                                                          \
    } while (0)

#define CHECK_EQ(a, b)                                                                             \
    do {                                                                                           \
        const unsigned long long a_ = (unsigned long long)(a);                                     \
        const unsigned long long b_ = (unsigned long long)(b);                                     \
        g_checks++;                                                                                \
        if (a_ != b_) {                                                                            \
            g_failures++;                                                                          \
            printf("    FAIL %s:%d: %s == %s (0x%llx vs 0x%llx)\n", __FILE__, __LINE__, #a, #b,    \
                   a_, b_);                                                                        \
        }                                                                                          \
    } while (0)

#define RUN(fn)                                                                                    \
    do {                                                                                           \
        g_tests++;                                                                                 \
        printf("  %s\n", #fn);                                                                     \
        fn();                                                                                      \
    } while (0)

#define TEST_SUMMARY(name)                                                                         \
    do {                                                                                           \
        printf("%s: %d tests, %d checks, %d failures\n", name, g_tests, g_checks, g_failures);     \
        return (g_failures == 0) ? 0 : 1;                                                          \
    } while (0)

#endif /* TEST_UTIL_H */
