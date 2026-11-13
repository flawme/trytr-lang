#include "test.h"

#include "limits.h"
#include "plot.h"
#include "svg_render.h"
#include "text_render.h"
#include "trytr/document.h"

#include <stdlib.h>

static bool contains(const char *text, const char *needle)
{
    return strstr(text, needle) != NULL;
}

static bool contains_non_ascii(const char *text)
{
    for (const unsigned char *cursor = (const unsigned char *)text;
         *cursor != 0u; ++cursor) {
        if (*cursor >= 0x80u) return true;
    }
    return false;
}

static bool diagnostic_present(const TrytrDocument *document, const char *code)
{
    for (size_t index = 0u; index < trytr_document_diagnostic_count(document); ++index) {
        const TrytrDiagnostic *diagnostic = trytr_document_diagnostic_at(document, index);
        if (diagnostic != NULL && strcmp(diagnostic->code, code) == 0) return true;
    }
    return false;
}

typedef struct PlotCapture {
    bool seen;
    bool matches;
} PlotCapture;

static bool capture_plot(const TrytrDocumentElement *element, void *user_data)
{
    PlotCapture *capture = user_data;
    if (element->kind == TRYTR_ELEMENT_PLOT) {
        capture->seen = true;
        capture->matches = element->point_count == 3u &&
            element->min_x == 0 && element->max_x == 10 &&
            element->min_y == 0 && element->max_y == 10 &&
            element->x == 10 && element->y == 10 &&
            element->width == 100 && element->height == 60 &&
            element->text_length == strlen("0,0 5,10 10,5");
    }
    return true;
}

static bool test_plot_data_analysis_and_bounds(void)
{
    static const char data[] = " 0, 2  5,10\n-5, 4 ";
    TrytrPlotAnalysis analysis;
    size_t error_offset = 0u;
    TEST_ASSERT_INT(TRYTR_PLOT_OK,
                    trytr_plot_analyze(data, sizeof(data) - 1u,
                                       &analysis, &error_offset));
    TEST_ASSERT_SIZE(3u, analysis.point_count);
    TEST_ASSERT_INT(-5, analysis.min_x);
    TEST_ASSERT_INT(2, analysis.min_y);
    TEST_ASSERT_INT(5, analysis.max_x);
    TEST_ASSERT_INT(10, analysis.max_y);
    return true;
}

static bool test_plot_data_rejects_bad_pairs_and_out_of_range_values(void)
{
    static const char *const invalid[] = {
        "", "0,0", "0,0 1", "0,,1 2,3", "0,0 1.5,2", "0,0 1 2 3"
    };
    TrytrPlotAnalysis analysis;
    size_t error_offset = 0u;
    for (size_t index = 0u; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        TEST_ASSERT_INT(TRYTR_PLOT_INVALID,
                        trytr_plot_analyze(invalid[index], strlen(invalid[index]),
                                           &analysis, &error_offset));
        TEST_ASSERT(error_offset <= strlen(invalid[index]));
    }
    static const char outside[] = "0,0 1000001,1";
    TEST_ASSERT_INT(TRYTR_PLOT_COORDINATE_RANGE,
                    trytr_plot_analyze(outside, sizeof(outside) - 1u,
                                       &analysis, &error_offset));
    return true;
}

static bool test_plot_data_limits(void)
{
    TrytrPlotAnalysis analysis;
    size_t error_offset = 0u;
    char *data = malloc(TRYTR_DEFAULT_PLOT_BYTE_LIMIT + 1u);
    TEST_ASSERT(data != NULL);
    memset(data, '0', TRYTR_DEFAULT_PLOT_BYTE_LIMIT + 1u);
    TEST_ASSERT_INT(TRYTR_PLOT_LIMIT_EXCEEDED,
                    trytr_plot_analyze(data, TRYTR_DEFAULT_PLOT_BYTE_LIMIT + 1u,
                                       &analysis, &error_offset));
    free(data);

    const size_t point_count = TRYTR_DEFAULT_PLOT_POINT_LIMIT + 1u;
    const size_t capacity = point_count * 8u + 1u;
    data = malloc(capacity);
    TEST_ASSERT(data != NULL);
    size_t length = 0u;
    for (size_t index = 0u; index < point_count; ++index) {
        const int written = snprintf(data + length, capacity - length,
                                     index == 0u ? "0,0" : " 0,0");
        TEST_ASSERT(written > 0);
        length += (size_t)written;
    }
    TEST_ASSERT_INT(TRYTR_PLOT_LIMIT_EXCEEDED,
                    trytr_plot_analyze(data, length, &analysis, &error_offset));
    free(data);
    return true;
}

static bool test_plot_parses_and_renders_in_svg_and_chat_text(void)
{
    static const char source[] =
        "@document(version:1,width:120,height:90){"
        "@plot #sales(10,10,100,60){0,0 5,10 10,5}"
        "}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    PlotCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_execute(document, capture_plot, &capture));
    TEST_ASSERT(capture.seen && capture.matches);

    char *svg = NULL;
    size_t svg_length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_svg_render(document, &svg, &svg_length));
    TEST_ASSERT(contains(svg, "<g id=\"sales\">"));
    TEST_ASSERT(contains(svg, "x1=\"10\" y1=\"10\" x2=\"10\" y2=\"70\""));
    TEST_ASSERT(contains(svg, "<path d=\"M 10.000 70.000 C "));
    TEST_ASSERT(!contains(svg, "<polyline"));
    TEST_ASSERT_SIZE(strlen(svg), svg_length);
    free(svg);

    char *text = NULL;
    size_t text_length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_text_render(document, &text, &text_length));
    TEST_ASSERT(contains(text, "\xf0\x9f\xac"));
    TEST_ASSERT(!contains(text, "⣀"));
    TEST_ASSERT(contains_non_ascii(text));
    TEST_ASSERT_SIZE(strlen(text), text_length);
    free(text);
    trytr_document_destroy(document);
    return true;
}

static bool test_plot_bounds_must_be_positive(void)
{
    static const char source[] = "@document(version:1){@plot(0,0,0,12){0,0 1,1}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_document_validate(document));
    TEST_ASSERT(diagnostic_present(document, "TRYTR-E-PLOT-SIZE"));
    trytr_document_destroy(document);
    return true;
}

static bool test_plot_parser_reports_data_errors(void)
{
    static const struct {
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"@document(version:1){@plot(0,0,10,10){0,0}}", "TRYTR-E-PLOT"},
        {"@document(version:1){@plot(0,0,10,10){0,0 1000001,1}}",
         "TRYTR-E-COORDINATE"}
    };
    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        TrytrDocument *document = NULL;
        TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                        trytr_document_parse(cases[index].source,
                                             strlen(cases[index].source), &document));
        TEST_ASSERT(diagnostic_present(document, cases[index].diagnostic));
        trytr_document_destroy(document);
    }
    return true;
}

static bool test_constant_plot_axis_is_centered(void)
{
    static const char source[] =
        "@document(version:1,width:40,height:30){@plot(0,0,20,20){5,0 5,10}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_svg_render(document, &svg, &length));
    TEST_ASSERT(contains(svg,
                         "<path d=\"M 10.000 20.000 C 10.000 13.333 10.000 6.667 10.000 0.000\""));
    free(svg);
    char *text = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_text_render(document, &text, &length));
    TEST_ASSERT(contains(text, "\xf0\x9f\xac"));
    free(text);
    trytr_document_destroy(document);
    return true;
}

static const TrytrTestCase tests[] = {
    {"plot analysis", test_plot_data_analysis_and_bounds},
    {"plot data errors", test_plot_data_rejects_bad_pairs_and_out_of_range_values},
    {"plot data limits", test_plot_data_limits},
    {"plot SVG and text rendering", test_plot_parses_and_renders_in_svg_and_chat_text},
    {"positive plot size", test_plot_bounds_must_be_positive},
    {"plot parser diagnostics", test_plot_parser_reports_data_errors},
    {"constant plot axis", test_constant_plot_axis_is_centered}
};

TEST_MAIN(tests)
