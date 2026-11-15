#include "test.h"

#include "limits.h"
#include "plot.h"
#include "svg_render.h"
#include "text_render.h"
#include "trytr/document.h"

#include <math.h>
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
    bool grid;
    bool markers;
    bool has_stroke;
    uint32_t stroke_rgb;
    int64_t stroke_width;
    bool formula;
    size_t point_count;
    double plot_min_x;
    double plot_min_y;
    double plot_max_x;
    double plot_max_y;
    char data[2048];
    size_t data_length;
} PlotCapture;

static bool capture_plot(const TrytrDocumentElement *element, void *user_data)
{
    PlotCapture *capture = user_data;
    if (element->kind == TRYTR_ELEMENT_PLOT) {
        capture->seen = true;
        capture->grid = element->plot_grid;
        capture->markers = element->plot_markers;
        capture->has_stroke = element->has_stroke;
        capture->stroke_rgb = element->stroke_rgb;
        capture->stroke_width = element->stroke_width;
        capture->formula = element->formula_plot;
        capture->point_count = element->point_count;
        capture->plot_min_x = element->plot_min_x;
        capture->plot_min_y = element->plot_min_y;
        capture->plot_max_x = element->plot_max_x;
        capture->plot_max_y = element->plot_max_y;
        capture->data_length = element->text_length;
        if (element->text_length < sizeof(capture->data)) {
            memcpy(capture->data, element->text, element->text_length);
            capture->data[element->text_length] = '\0';
        }
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
        "", "0,0", "0,0 1", "0,,1 2,3", "0,0 1e,2", "0,0 1 2 3"
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

static bool test_plot_data_accepts_finite_decimal_coordinates(void)
{
    static const char data[] = "-2.5,-0.75 3.25,1e-2";
    TrytrPlotAnalysis analysis;
    size_t error_offset = 0u;
    TEST_ASSERT_INT(TRYTR_PLOT_OK,
        trytr_plot_analyze(data, sizeof(data) - 1u, &analysis, &error_offset));
    TEST_ASSERT_SIZE(2u, analysis.point_count);
    TEST_ASSERT(fabs(analysis.min_x - -2.5) < 1e-12);
    TEST_ASSERT(fabs(analysis.min_y - -0.75) < 1e-12);
    TEST_ASSERT(fabs(analysis.max_x - 3.25) < 1e-12);
    TEST_ASSERT(fabs(analysis.max_y - 0.01) < 1e-12);
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
    TEST_ASSERT(capture.grid && capture.markers);

    char *svg = NULL;
    size_t svg_length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_svg_render(document, &svg, &svg_length));
    TEST_ASSERT(contains(svg, "<g id=\"sales\">"));
    TEST_ASSERT(contains(svg, "x1=\"10\" y1=\"10\" x2=\"10\" y2=\"70\""));
    TEST_ASSERT(contains(svg, "class=\"x-tick\""));
    TEST_ASSERT(contains(svg, "class=\"y-tick\""));
    TEST_ASSERT(contains(svg, ">10</text>"));
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

static bool test_plot_options_control_grid_markers_and_stroke(void)
{
    static const char source[] =
        "@document(version:1,width:120,height:90){"
        "@plot #sales(10,10,100,60)(stroke:rgb(12,34,56),stroke-width:4,"
        "grid:false,markers:false){0,0 5,10 10,5}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    PlotCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_execute(document, capture_plot, &capture));
    TEST_ASSERT(capture.seen);
    TEST_ASSERT(!capture.grid && !capture.markers);
    TEST_ASSERT(capture.has_stroke);
    TEST_ASSERT_INT(0x0c2238, capture.stroke_rgb);
    TEST_ASSERT_INT(4, capture.stroke_width);

    char *svg = NULL;
    size_t svg_length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_svg_render(document, &svg, &svg_length));
    TEST_ASSERT(contains(svg, "stroke=\"#0c2238\" stroke-width=\"4\""));
    TEST_ASSERT(!contains(svg, "class=\"grid\""));
    TEST_ASSERT(!contains(svg, "class=\"data-point\""));
    TEST_ASSERT_SIZE(strlen(svg), svg_length);
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_formula_plot_samples_math_expression_and_renders_smoothly(void)
{
    static const char source[] =
        "@document(version:1,width:180,height:120){"
        "@plot #parabola(20,20,140,80)(x-min:-4,x-max:4,samples:9,"
        "grid:false,markers:false){y = 0.25*x^2 - 1}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_document_parse(source, sizeof(source) - 1u, &document));
    PlotCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_document_execute(document, capture_plot, &capture));
    TEST_ASSERT(capture.seen && capture.formula);
    TEST_ASSERT(capture.point_count > 9u);
    TEST_ASSERT(capture.point_count <= TRYTR_DEFAULT_PLOT_POINT_LIMIT);
    TEST_ASSERT(capture.plot_min_x == -4.0 && capture.plot_max_x == 4.0);
    TEST_ASSERT(capture.plot_min_y == -1.0 && capture.plot_max_y == 3.0);
    TEST_ASSERT(contains(capture.data, "-4,3"));
    TEST_ASSERT(contains(capture.data, "0,-1"));
    TEST_ASSERT(contains(capture.data, "4,3"));

    char *svg = NULL;
    size_t svg_length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_svg_render(document, &svg, &svg_length));
    TEST_ASSERT(contains(svg, "<path d=\"M 20.000"));
    TEST_ASSERT(contains(svg, " C "));
    TEST_ASSERT(!contains(svg, "<polyline"));
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_formula_plot_preserves_fractional_x_samples_in_both_renderers(void)
{
    static const char source[] =
        "@document(version:1,width:120,height:80){"
        "@plot(10,10,100,50)(x-min:-1,x-max:1,samples:4,"
        "grid:false,markers:false){y=x}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_document_parse(source, sizeof(source) - 1u, &document));
    PlotCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_document_execute(document, capture_plot, &capture));
    TEST_ASSERT(capture.formula);
    TEST_ASSERT_SIZE(4u, capture.point_count);
    TEST_ASSERT(fabs(capture.plot_min_x + 1.0) < 1e-14);
    TEST_ASSERT(fabs(capture.plot_max_x - 1.0) < 1e-14);
    TEST_ASSERT(contains(capture.data, "-1,-1"));
    TEST_ASSERT(contains(capture.data, "-0.333333333333333"));
    TEST_ASSERT(contains(capture.data, "0.333333333333333"));
    TEST_ASSERT(contains(capture.data, "1,1"));

    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_svg_render(document, &svg, &length));
    TEST_ASSERT(contains(svg, "<path d=\"M 10.000 60.000 C "));
    free(svg);
    char *text = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_text_render(document, &text, &length));
    TEST_ASSERT(contains_non_ascii(text));
    free(text);
    trytr_document_destroy(document);
    return true;
}

static bool test_formula_plot_refines_curvature_in_screen_space(void)
{
    static const char source[] =
        "@document(version:1,width:900,height:900){"
        "@plot(20,20,800,800)(x-min:-2,x-max:2,samples:3,"
        "grid:false,markers:false){y=x^2}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_document_parse(source, sizeof(source) - 1u, &document));
    PlotCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_document_execute(document, capture_plot, &capture));
    TEST_ASSERT(capture.formula);
    TEST_ASSERT(capture.point_count > 3u);
    TEST_ASSERT(capture.point_count <= TRYTR_DEFAULT_PLOT_POINT_LIMIT);
    TEST_ASSERT(contains(capture.data, "-2,4"));
    TEST_ASSERT(contains(capture.data, "0,0"));
    TEST_ASSERT(contains(capture.data, "2,4"));
    TEST_ASSERT(capture.plot_min_y == 0.0 && capture.plot_max_y == 4.0);
    trytr_document_destroy(document);

    static const char small_plot[] =
        "@document(version:1,width:900,height:90){"
        "@plot(20,20,800,40)(x-min:-2,x-max:2,samples:3,"
        "grid:false,markers:false){y=x^2}}";
    document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_document_parse(small_plot, sizeof(small_plot) - 1u, &document));
    PlotCapture small_capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_document_execute(document, capture_plot, &small_capture));
    TEST_ASSERT(small_capture.point_count < capture.point_count);
    trytr_document_destroy(document);
    return true;
}

static bool test_formula_plot_rejects_underresolved_curves_at_budget(void)
{
    static const char source[] =
        "@document(version:1,width:1100,height:1100){"
        "@plot(20,20,1000,1000)(x-min:-1,x-max:1,samples:3,"
        "grid:false,markers:false){y=sin(1000*x)}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
        trytr_document_parse(source, sizeof(source) - 1u, &document));
    TEST_ASSERT(diagnostic_present(document, "TRYTR-E-PLOT-RESOLUTION"));
    trytr_document_destroy(document);
    return true;
}

static bool test_formula_plot_sample_limit_is_enforced_at_both_ends(void)
{
    static const char maximum[] =
        "@document(version:1){@plot(0,0,10,10)(samples:1024){y=cos(x)}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_document_parse(maximum, sizeof(maximum) - 1u, &document));
    PlotCapture capture = {0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
        trytr_document_execute(document, capture_plot, &capture));
    TEST_ASSERT_SIZE(1024u, capture.point_count);
    trytr_document_destroy(document);

    static const char over_limit[] =
        "@document(version:1){@plot(0,0,10,10)(samples:1025){y=cos(x)}}";
    document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
        trytr_document_parse(over_limit, sizeof(over_limit) - 1u, &document));
    TEST_ASSERT(diagnostic_present(document, "TRYTR-E-PLOT-SAMPLES"));
    trytr_document_destroy(document);
    return true;
}

static bool test_formula_plot_rejects_invalid_domain_and_math_domain(void)
{
    static const struct {
        const char *source;
        const char *diagnostic;
    } cases[] = {
        {"@document(version:1){@plot(0,0,20,20)(x-min:2,x-max:2){y=x}}",
         "TRYTR-E-PLOT-DOMAIN"},
        {"@document(version:1){@plot(0,0,20,20)(x-min:-1,x-max:1,samples:5){y=sqrt(x)}}",
         "TRYTR-E-PLOT-FORMULA"},
        {"@document(version:1){@plot(0,0,20,20)(samples:1){y=x}}",
         "TRYTR-E-PLOT-SAMPLES"},
        {"@document(version:1){@plot(0,0,20,20)(samples:1025){y=x}}",
         "TRYTR-E-PLOT-SAMPLES"},
        {"@document(version:1){@plot(0,0,20,20){y=secret(x)}}",
         "TRYTR-E-PLOT-FORMULA"}
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

static bool test_plot_boolean_options_reject_other_values(void)
{
    static const char source[] =
        "@document(version:1){@plot(0,0,10,10)(grid:2){0,0 1,1}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_document_validate(document));
    TEST_ASSERT(diagnostic_present(document, "TRYTR-E-PLOT-OPTION"));
    trytr_document_destroy(document);

    static const char data_option[] =
        "@document(version:1){@plot(0,0,10,10)(x-min:-2){0,0 1,1}}";
    document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(data_option, sizeof(data_option) - 1u,
                                         &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_document_validate(document));
    TEST_ASSERT(diagnostic_present(document, "TRYTR-E-PLOT-OPTION"));
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
    {"plot decimal data", test_plot_data_accepts_finite_decimal_coordinates},
    {"plot data errors", test_plot_data_rejects_bad_pairs_and_out_of_range_values},
    {"plot data limits", test_plot_data_limits},
    {"plot SVG and text rendering", test_plot_parses_and_renders_in_svg_and_chat_text},
    {"plot visual options", test_plot_options_control_grid_markers_and_stroke},
    {"formula plot sampling and rendering",
     test_formula_plot_samples_math_expression_and_renders_smoothly},
    {"formula plot fractional samples",
     test_formula_plot_preserves_fractional_x_samples_in_both_renderers},
    {"formula plot screen-space adaptive sampling",
     test_formula_plot_refines_curvature_in_screen_space},
    {"formula plot adaptive point budget",
     test_formula_plot_rejects_underresolved_curves_at_budget},
    {"formula plot sample limits",
     test_formula_plot_sample_limit_is_enforced_at_both_ends},
    {"formula plot failures", test_formula_plot_rejects_invalid_domain_and_math_domain},
    {"positive plot size", test_plot_bounds_must_be_positive},
    {"plot boolean options", test_plot_boolean_options_reject_other_values},
    {"plot parser diagnostics", test_plot_parser_reports_data_errors},
    {"constant plot axis", test_constant_plot_axis_is_centered}
};

TEST_MAIN(tests)
