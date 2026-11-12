#include "test.h"

#include "limits.h"
#include "path.h"

#include <stdlib.h>

static bool analyze(const char *data, TrytrPathStatus expected_status,
                    TrytrPathAnalysis *analysis, size_t *error_offset)
{
    const TrytrPathStatus status = trytr_path_analyze(
        data, strlen(data), analysis, error_offset);
    TEST_ASSERT_INT(expected_status, status);
    return true;
}

static bool test_supported_commands_and_conservative_bounds(void)
{
    static const char source[] = "M 10 20 L 30 40 H 50 V 60 Q 70 80 90 100 C 1 2 3 4 5 6 Z";
    TrytrPathAnalysis analysis;
    size_t error_offset;
    TEST_ASSERT(analyze(source, TRYTR_PATH_OK, &analysis, &error_offset));
    TEST_ASSERT_SIZE(7u, analysis.segment_count);
    TEST_ASSERT_INT(1, analysis.min_x);
    TEST_ASSERT_INT(2, analysis.min_y);
    TEST_ASSERT_INT(90, analysis.max_x);
    TEST_ASSERT_INT(100, analysis.max_y);
    TEST_ASSERT_SIZE(0u, error_offset);
    return true;
}

static bool test_malformed_and_unsupported_path_data(void)
{
    static const char *const cases[] = {
        "",
        "L 1 2",
        "M 1",
        "M 1 2 L 3",
        "M 1 2 A 3 4 5 6 7",
        "M 1 2 l 3 4",
        "M 1 2 3 4",
        "M 1,,2",
        "M 1 2 Z 3"
    };
    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        TrytrPathAnalysis analysis;
        size_t error_offset = 0u;
        TEST_ASSERT(analyze(cases[index], TRYTR_PATH_INVALID, &analysis, &error_offset));
        TEST_ASSERT(error_offset <= strlen(cases[index]));
    }
    return true;
}

static bool test_coordinate_range_is_enforced(void)
{
    static const char source[] = "M 1000001 0";
    TrytrPathAnalysis analysis;
    size_t error_offset = 0u;
    const TrytrPathStatus status = trytr_path_analyze(
        source, sizeof(source) - 1u, &analysis, &error_offset);
    TEST_ASSERT_INT(TRYTR_PATH_COORDINATE_RANGE, status);
    TEST_ASSERT_SIZE(2u, error_offset);
    return true;
}

static bool test_byte_and_segment_limits_are_enforced(void)
{
    char *large = malloc(TRYTR_DEFAULT_PATH_BYTE_LIMIT + 1u);
    TEST_ASSERT(large != NULL);
    memset(large, 'x', TRYTR_DEFAULT_PATH_BYTE_LIMIT + 1u);
    TrytrPathAnalysis analysis;
    size_t error_offset = 0u;
    TEST_ASSERT_INT(TRYTR_PATH_LIMIT_EXCEEDED,
                    trytr_path_analyze(large, TRYTR_DEFAULT_PATH_BYTE_LIMIT + 1u,
                                       &analysis, &error_offset));
    free(large);

    const size_t command_count = TRYTR_DEFAULT_PATH_SEGMENT_LIMIT + 1u;
    const size_t length = sizeof("M 0 0") - 1u +
                          (command_count - 1u) * (sizeof(" L 0 0") - 1u);
    large = malloc(length + 1u);
    TEST_ASSERT(large != NULL);
    char *cursor = large;
    memcpy(cursor, "M 0 0", sizeof("M 0 0") - 1u);
    cursor += sizeof("M 0 0") - 1u;
    for (size_t index = 1u; index < command_count; ++index) {
        memcpy(cursor, " L 0 0", sizeof(" L 0 0") - 1u);
        cursor += sizeof(" L 0 0") - 1u;
    }
    *cursor = '\0';
    TEST_ASSERT_SIZE(length, (size_t)(cursor - large));
    TEST_ASSERT_INT(TRYTR_PATH_LIMIT_EXCEEDED,
                    trytr_path_analyze(large, length, &analysis, &error_offset));
    TEST_ASSERT(error_offset < length);
    free(large);
    return true;
}

static bool test_invalid_arguments(void)
{
    TrytrPathAnalysis analysis;
    size_t error_offset;
    TEST_ASSERT_INT(TRYTR_PATH_INVALID,
                    trytr_path_analyze(NULL, 1u, &analysis, &error_offset));
    TEST_ASSERT_INT(TRYTR_PATH_INVALID,
                    trytr_path_analyze("M 0 0", 5u, NULL, &error_offset));
    TEST_ASSERT_INT(TRYTR_PATH_INVALID,
                    trytr_path_analyze("M 0 0", 5u, &analysis, NULL));
    return true;
}

static const TrytrTestCase tests[] = {
    {"supported commands and bounds", test_supported_commands_and_conservative_bounds},
    {"malformed path data", test_malformed_and_unsupported_path_data},
    {"coordinate range", test_coordinate_range_is_enforced},
    {"byte and segment limits", test_byte_and_segment_limits_are_enforced},
    {"invalid arguments", test_invalid_arguments}
};

TEST_MAIN(tests)
