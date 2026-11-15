#include "test.h"

#include "limits.h"
#include "polygon.h"
#include "parser.h"

#include <stdlib.h>

static bool test_valid_points_and_bounds(void)
{
    static const char points[] = " 1, 2  30,4\n-5, 6 ";
    TrytrPolygonAnalysis analysis;
    size_t error_offset = 0u;
    TEST_ASSERT_INT(TRYTR_POLYGON_OK,
                    trytr_polygon_analyze(points, sizeof(points) - 1u,
                                          &analysis, &error_offset));
    TEST_ASSERT_SIZE(3u, analysis.point_count);
    TEST_ASSERT_INT(-5, analysis.min_x);
    TEST_ASSERT_INT(2, analysis.min_y);
    TEST_ASSERT_INT(30, analysis.max_x);
    TEST_ASSERT_INT(6, analysis.max_y);
    return true;
}

static bool test_invalid_point_lists(void)
{
    static const char *const cases[] = {
        "",
        "1 2 3 4",
        "1 2 3 4 5",
        "1,,2 3,4 5,6",
        "1 2 3.5 4 5 6",
        "1 2 3 4 5 6 7"
    };
    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        TrytrPolygonAnalysis analysis;
        size_t error_offset = 0u;
        TEST_ASSERT_INT(TRYTR_POLYGON_INVALID,
                        trytr_polygon_analyze(cases[index], strlen(cases[index]),
                                              &analysis, &error_offset));
        TEST_ASSERT(error_offset <= strlen(cases[index]));
    }
    return true;
}

static bool test_coordinate_byte_and_point_limits(void)
{
    static const char out_of_range[] = "0 0 1000001 1 2 2";
    TrytrPolygonAnalysis analysis;
    size_t error_offset = 0u;
    TEST_ASSERT_INT(TRYTR_POLYGON_COORDINATE_RANGE,
                    trytr_polygon_analyze(out_of_range, sizeof(out_of_range) - 1u,
                                          &analysis, &error_offset));

    char *large = malloc(TRYTR_DEFAULT_POLYGON_BYTE_LIMIT + 1u);
    TEST_ASSERT(large != NULL);
    memset(large, '0', TRYTR_DEFAULT_POLYGON_BYTE_LIMIT + 1u);
    TEST_ASSERT_INT(TRYTR_POLYGON_LIMIT_EXCEEDED,
                    trytr_polygon_analyze(large, TRYTR_DEFAULT_POLYGON_BYTE_LIMIT + 1u,
                                          &analysis, &error_offset));
    free(large);

    const size_t points = TRYTR_DEFAULT_POLYGON_POINT_LIMIT + 1u;
    const size_t length = points * 5u;
    large = malloc(length + 1u);
    TEST_ASSERT(large != NULL);
    char *cursor = large;
    for (size_t index = 0u; index < points; ++index) {
        memcpy(cursor, index == 0u ? "0 0 " : " 0 0 ", index == 0u ? 4u : 5u);
        cursor += index == 0u ? 4u : 5u;
    }
    *cursor = '\0';
    TEST_ASSERT_INT(TRYTR_POLYGON_LIMIT_EXCEEDED,
                    trytr_polygon_analyze(large, (size_t)(cursor - large),
                                          &analysis, &error_offset));
    free(large);
    return true;
}

static bool test_invalid_arguments(void)
{
    TrytrPolygonAnalysis analysis;
    size_t error_offset;
    TEST_ASSERT_INT(TRYTR_POLYGON_INVALID,
                    trytr_polygon_analyze(NULL, 1u, &analysis, &error_offset));
    TEST_ASSERT_INT(TRYTR_POLYGON_INVALID,
                    trytr_polygon_analyze("0 0 1 1 2 2", 11u, NULL, &error_offset));
    TEST_ASSERT_INT(TRYTR_POLYGON_INVALID,
                    trytr_polygon_analyze("0 0 1 1 2 2", 11u, &analysis, NULL));
    return true;
}

static bool test_parser_stores_polygon_bounds_and_source_locations(void)
{
    static const char source[] =
        "@document(version:1){@polygon #triangle{ 1,2 30,4 -5,6 }}";
    TrytrParserOutput output;
    TEST_ASSERT(trytr_parser_parse(source, sizeof(source) - 1u, &output));
    const TrytrNode *polygon = output.root->children;
    TEST_ASSERT_INT(TRYTR_NODE_POLYGON, polygon->kind);
    TEST_ASSERT_STRING("triangle", polygon->id);
    TEST_ASSERT_SIZE(3u, polygon->polygon_point_count);
    TEST_ASSERT_INT(-5, polygon->polygon_min_x);
    TEST_ASSERT_INT(2, polygon->polygon_min_y);
    TEST_ASSERT_INT(30, polygon->polygon_max_x);
    TEST_ASSERT_INT(6, polygon->polygon_max_y);
    trytr_parser_output_destroy(&output);

    static const char invalid[] =
        "@document(version:1){@polygon{\n  0 0 1 1 1000001 6}}";
    TEST_ASSERT(!trytr_parser_parse(invalid, sizeof(invalid) - 1u, &output));
    const TrytrDiagnostic *diagnostic = NULL;
    for (size_t index = 0u; index < output.diagnostics.count; ++index) {
        if (strcmp(output.diagnostics.items[index].code, "TRYTR-E-COORDINATE") == 0) {
            diagnostic = &output.diagnostics.items[index];
            break;
        }
    }
    TEST_ASSERT(diagnostic != NULL);
    TEST_ASSERT_SIZE((size_t)(strstr(invalid, "1000001") - invalid),
                     diagnostic->location.offset);
    trytr_parser_output_destroy(&output);
    return true;
}

static bool test_path_expression_support_does_not_change_polygon_grammar(void)
{
    static const char source[] =
        "@document(version:1){@polygon #legacy{1,2 30,4 -5,6}}";
    TrytrParserOutput output;
    TEST_ASSERT(trytr_parser_parse(source, sizeof(source) - 1u, &output));
    const TrytrNode *polygon = output.root->children;
    TEST_ASSERT_INT(TRYTR_NODE_POLYGON, polygon->kind);
    TEST_ASSERT_STRING("1,2 30,4 -5,6", polygon->text);
    TEST_ASSERT_SIZE(3u, polygon->polygon_point_count);
    TEST_ASSERT_INT(-5, polygon->polygon_min_x);
    TEST_ASSERT_INT(30, polygon->polygon_max_x);
    trytr_parser_output_destroy(&output);

    static const char expression[] =
        "@document(version:1){@polygon{(1 + 2),2 3,4 5,6}}";
    TEST_ASSERT(!trytr_parser_parse(expression, sizeof(expression) - 1u, &output));
    bool found_polygon_diagnostic = false;
    for (size_t index = 0u; index < output.diagnostics.count; ++index) {
        if (strcmp(output.diagnostics.items[index].code, "TRYTR-E-POLYGON") == 0) {
            found_polygon_diagnostic = true;
            break;
        }
    }
    TEST_ASSERT(found_polygon_diagnostic);
    trytr_parser_output_destroy(&output);
    return true;
}

static const TrytrTestCase tests[] = {
    {"valid points and bounds", test_valid_points_and_bounds},
    {"invalid point lists", test_invalid_point_lists},
    {"coordinate, byte, and point limits", test_coordinate_byte_and_point_limits},
    {"invalid arguments", test_invalid_arguments},
    {"parser bounds and locations", test_parser_stores_polygon_bounds_and_source_locations},
    {"polygon grammar regression", test_path_expression_support_does_not_change_polygon_grammar}
};

TEST_MAIN(tests)
