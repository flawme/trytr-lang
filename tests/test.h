#ifndef TRYTR_TEST_H
#define TRYTR_TEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define TEST_ASSERT(condition)                                                     \
    do {                                                                         \
        if (!(condition)) {                                                       \
            fprintf(stderr, "%s:%d: assertion failed: %s\n",                   \
                    __FILE__, __LINE__, #condition);                              \
            return false;                                                        \
        }                                                                        \
    } while (false)

#define TEST_ASSERT_SIZE(expected, actual)                                         \
    do {                                                                         \
        const size_t expected_value = (size_t)(expected);                          \
        const size_t actual_value = (size_t)(actual);                              \
        if (expected_value != actual_value) {                                      \
            fprintf(stderr, "%s:%d: expected %zu, got %zu\n",                   \
                    __FILE__, __LINE__, expected_value, actual_value);             \
            return false;                                                        \
        }                                                                        \
    } while (false)

#define TEST_ASSERT_INT(expected, actual)                                          \
    do {                                                                         \
        const long long expected_value = (long long)(expected);                    \
        const long long actual_value = (long long)(actual);                        \
        if (expected_value != actual_value) {                                      \
            fprintf(stderr, "%s:%d: expected %lld, got %lld\n",                 \
                    __FILE__, __LINE__, expected_value, actual_value);             \
            return false;                                                        \
        }                                                                        \
    } while (false)

#define TEST_ASSERT_STRING(expected, actual)                                       \
    do {                                                                         \
        const char *expected_value = (expected);                                   \
        const char *actual_value = (actual);                                       \
        if (actual_value == NULL || strcmp(expected_value, actual_value) != 0) {   \
            fprintf(stderr, "%s:%d: expected \"%s\", got \"%s\"\n",             \
                    __FILE__, __LINE__, expected_value,                            \
                    actual_value == NULL ? "(null)" : actual_value);               \
            return false;                                                        \
        }                                                                        \
    } while (false)

#define TRYTR_ASSERT(condition) TEST_ASSERT(condition)

#define TRYTR_RUN(function)                                                        \
    do {                                                                           \
        if (!(function)()) {                                                       \
            fprintf(stderr, "[FAIL] %s\n", #function);                           \
            return 1;                                                              \
        }                                                                          \
    } while (false)

typedef bool (*TrytrTestFunction)(void);

typedef struct TrytrTestCase {
    const char *name;
    TrytrTestFunction function;
} TrytrTestCase;

static inline int trytr_test_run(const TrytrTestCase *tests, size_t count)
{
    size_t passed = 0;

    for (size_t index = 0; index < count; ++index) {
        if (!tests[index].function()) {
            fprintf(stderr, "[FAIL] %s\n", tests[index].name);
            return 1;
        }
        ++passed;
    }

    printf("%zu tests passed\n", passed);
    return 0;
}

#define TEST_MAIN(tests)                                                           \
    int main(void)                                                                 \
    {                                                                              \
        return trytr_test_run((tests), sizeof(tests) / sizeof((tests)[0]));         \
    }

#endif
