#include "test.h"

#include "svg_render.h"
#include "trytr/document.h"

#include <stdlib.h>

static TrytrStatus render(const char *source, char **svg, size_t *length,
                          TrytrDocument **out_document)
{
    TrytrStatus status = trytr_document_parse(source, strlen(source), out_document);
    if (status != TRYTR_STATUS_OK) return status;
    return trytr_svg_render(*out_document, svg, length);
}

static bool contains(const char *text, const char *needle)
{
    return strstr(text, needle) != NULL;
}

static bool document_has_diagnostic(const TrytrDocument *document, const char *code)
{
    for (size_t index = 0u; index < trytr_document_diagnostic_count(document); ++index) {
        const TrytrDiagnostic *diagnostic = trytr_document_diagnostic_at(document, index);
        if (diagnostic != NULL && strcmp(diagnostic->code, code) == 0) return true;
    }
    return false;
}

static bool test_svg_renders_all_shapes_and_escapes_text(void)
{
    static const char source[] =
        "@document(version:1){"
        "@heading #title{A & <B>}"
        "@rect #box(10,20,30,40)"
        "@roundrect #card(15,25,40,30,10)"
        "@line #edge(1,2,3,4)"
        "@circle #dot(8,9,2)"
        "@ellipse #oval(12,13,4,5)"
        "@polygon #triangle(fill:rgb(9,8,7),stroke-width:3){10,10 20,10 15,20}"
        "@path #curve(stroke:rgb(6,5,4),stroke-width:2){M 1 2 L 5 6 Z}"
        "}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(svg != NULL);
    TEST_ASSERT_SIZE(strlen(svg), length);
    TEST_ASSERT(contains(svg, "<svg xmlns=\"http://www.w3.org/2000/svg\""));
    TEST_ASSERT(contains(svg, "<title>trytr document</title>"));
    TEST_ASSERT(contains(svg, "<text x=\"16\""));
    TEST_ASSERT(contains(svg, "A &amp; &lt;B&gt;"));
    TEST_ASSERT(contains(svg, "<rect id=\"box\" x=\"10\" y=\"20\" width=\"30\" height=\"40\""));
    TEST_ASSERT(contains(svg, "<rect id=\"card\" x=\"15\" y=\"25\" width=\"40\" height=\"30\" rx=\"10\" ry=\"10\""));
    TEST_ASSERT(contains(svg, "<line id=\"edge\" x1=\"1\" y1=\"2\" x2=\"3\" y2=\"4\""));
    TEST_ASSERT(contains(svg, "<circle id=\"dot\" cx=\"8\" cy=\"9\" r=\"2\""));
    TEST_ASSERT(contains(svg, "<ellipse id=\"oval\" cx=\"12\" cy=\"13\" rx=\"4\" ry=\"5\""));
    TEST_ASSERT(contains(svg,
                        "<polygon id=\"triangle\" points=\"10,10 20,10 15,20\""
                        " fill=\"#090807\" stroke=\"#222\" stroke-width=\"3\"/>"));
    TEST_ASSERT(contains(svg,
                        "<path id=\"curve\" d=\"M 1 2 L 5 6 Z\""
                        " fill=\"none\" stroke=\"#060504\" stroke-width=\"2\"/>"));
    TEST_ASSERT(contains(svg, "</svg>\n"));
    int64_t canvas_width = 1;
    int64_t canvas_height = 1;
    TEST_ASSERT(!trytr_document_get_canvas_size(document, &canvas_width, &canvas_height));
    TEST_ASSERT_INT(0, canvas_width);
    TEST_ASSERT_INT(0, canvas_height);
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_svg_receives_only_folded_path_coordinates(void)
{
    static const char source[] =
        "@document(version:1){"
        "@param #scale(min:1,max:10,value:3,step:1){Scale}"
        "@path #expr{M (#scale * 2) -20 L #scale + 1 (#scale * 2) Z}}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_svg_render(document, &svg, &length));
    TEST_ASSERT(contains(svg,
        "<path id=\"expr\" d=\"M 6 -20 L 4 6 Z\""
        " fill=\"none\" stroke=\"#222\" stroke-width=\"1\"/>"));
    TEST_ASSERT(!contains(svg, "#scale"));
    TEST_ASSERT_SIZE(strlen(svg), length);
    free(svg);
    trytr_document_destroy(document);

    const TrytrParameterOverride override = {.id = "scale", .value = 7};
    document = NULL;
    svg = NULL;
    length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse_with_parameters(
                        source, sizeof(source) - 1u, &override, 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_svg_render(document, &svg, &length));
    TEST_ASSERT(contains(svg,
        "<path id=\"expr\" d=\"M 14 -20 L 8 14 Z\""
        " fill=\"none\" stroke=\"#222\" stroke-width=\"1\"/>"));
    TEST_ASSERT(!contains(svg, "#scale"));
    TEST_ASSERT_SIZE(strlen(svg), length);
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_math_scripts_render_as_tspans(void)
{
    static const char source[] =
        "@document(version:1){"
        "@math #formula{x_{i+1} + y^2 + a_{b^c} + A\\_B + C\\^D "
        "+ z_α + m_{A & B} + \\{literal\\} + √2 + "
        "\\sqrt{a^2 + b_i} + \\frac{p_{i+1} & r}{2+q^2} & <E>}"
        "@math #literal{\\frac{missing}}"
        "}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(contains(svg,
                        "x<tspan baseline-shift=\"sub\" font-size=\"70%\">i+1</tspan>"));
    TEST_ASSERT(contains(svg,
                        "y<tspan baseline-shift=\"super\" font-size=\"70%\">2</tspan>"));
    TEST_ASSERT(contains(svg,
                        "a<tspan baseline-shift=\"sub\" font-size=\"70%\">b"
                        "<tspan baseline-shift=\"super\" font-size=\"70%\">c</tspan>"
                        "</tspan>"));
    TEST_ASSERT(contains(svg,
                        "z<tspan baseline-shift=\"sub\" font-size=\"70%\">α</tspan>"));
    TEST_ASSERT(contains(svg,
                        "m<tspan baseline-shift=\"sub\" font-size=\"70%\">A &amp; B</tspan>"));
    TEST_ASSERT(contains(svg,
                        "<tspan>√</tspan><tspan text-decoration=\"overline\">a"
                        "<tspan baseline-shift=\"super\" font-size=\"70%\">2</tspan>"
                        " + b<tspan baseline-shift=\"sub\" font-size=\"70%\">i</tspan>"
                        "</tspan>"));
    TEST_ASSERT(contains(svg,
                        "<tspan baseline-shift=\"super\" font-size=\"70%\">p"
                        "<tspan baseline-shift=\"sub\" font-size=\"70%\">i+1</tspan>"
                        " &amp; r</tspan>⁄"
                        "<tspan baseline-shift=\"sub\" font-size=\"70%\">2+q"
                        "<tspan baseline-shift=\"super\" font-size=\"70%\">2</tspan>"
                        "</tspan>"));
    TEST_ASSERT(contains(svg, "\\frac{missing}"));
    TEST_ASSERT(contains(svg, "A_B + C^D + z<tspan baseline-shift=\"sub\""));
    TEST_ASSERT(contains(svg, "+ {literal} + √2 + <tspan>√</tspan>"));
    TEST_ASSERT(contains(svg, "</tspan> &amp; &lt;E&gt;"));
    TEST_ASSERT(!contains(svg, "A\\_B"));
    TEST_ASSERT_SIZE(strlen(svg), length);
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_math_can_be_deferred_to_a_native_host_renderer(void)
{
    static const char source[] =
        "@document(version:1){@heading{Title}"
        "@math #formula{NativeFormulaUnique}"
        "@text{After equation}@circle #dot(5,5,2)}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, sizeof(source) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_svg_render_without_math(document, &svg, &length));
    TEST_ASSERT_SIZE(strlen(svg), length);
    TEST_ASSERT(!contains(svg, "NativeFormulaUnique"));
    TEST_ASSERT(contains(svg, "Title"));
    TEST_ASSERT(contains(svg, "After equation"));
    TEST_ASSERT(contains(svg, "<circle id=\"dot\""));
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_text_font_size_color_and_alignment(void)
{
    static const char source[] =
        "@document(version:1){"
        "@text #label(font-size:20,color:rgb(12,34,56),align:center){AB\nC}"
        "@text #right(align:right){AB\nC}"
        "}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(contains(svg,
                        "<text x=\"36\" y=\"36\" font-family=\"sans-serif\" "
                        "font-size=\"20\" font-weight=\"400\" font-style=\"normal\" "
                        "text-anchor=\"middle\" fill=\"#0c2238\" id=\"label\">"));
    TEST_ASSERT(contains(svg, "<tspan x=\"36\" dy=\"0\">AB</tspan>"));
    TEST_ASSERT(contains(svg, "<tspan x=\"36\" dy=\"26\">C</tspan>"));
    TEST_ASSERT(contains(svg,
                        "<text x=\"48\" y=\"92\" font-family=\"sans-serif\" "
                        "font-size=\"16\" font-weight=\"400\" font-style=\"normal\" "
                        "text-anchor=\"end\" id=\"right\">"));
    TEST_ASSERT_SIZE(strlen(svg), length);
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_text_width_wraps_at_words_without_splitting_long_words(void)
{
    static const char source[] =
        "@document(version:1){"
        "@text #wrap(font-size:10,width:35,align:center){one two three}"
        "}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(contains(svg,
                        "<text x=\"33\" y=\"26\" font-family=\"sans-serif\" "
                        "font-size=\"10\" font-weight=\"400\" font-style=\"normal\" "
                        "text-anchor=\"middle\" id=\"wrap\">"));
    TEST_ASSERT(contains(svg, "<tspan x=\"33\" dy=\"0\">one</tspan>"));
    TEST_ASSERT(contains(svg, "<tspan x=\"33\" dy=\"16\">two</tspan>"));
    TEST_ASSERT(contains(svg, "<tspan x=\"33\" dy=\"16\">three</tspan>"));
    TEST_ASSERT_SIZE(strlen(svg), length);
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_shape_fill_and_stroke_colors(void)
{
    static const char source[] =
        "@document(version:1){"
        "@rect #card{x:1 y:2 width:30 height:40 "
        "fill:rgb(230,57,70) stroke:rgb(20,40,60) stroke-width:4}"
        "@line #edge{x1:0 y1:0 x2:10 y2:10 stroke:rgb(12,34,56) stroke-width:2}"
        "@circle #dot{cx:5 cy:6 radius:2 fill:rgb(255,0,128)}"
        "}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(contains(svg,
                        "<rect id=\"card\" x=\"1\" y=\"2\" width=\"30\" height=\"40\""
                        " fill=\"#e63946\" stroke=\"#14283c\" stroke-width=\"4\"/>"));
    TEST_ASSERT(contains(svg,
                        "<line id=\"edge\" x1=\"0\" y1=\"0\" x2=\"10\" y2=\"10\""
                        " stroke=\"#0c2238\" stroke-width=\"2\"/>"));
    TEST_ASSERT(contains(svg,
                        "<circle id=\"dot\" cx=\"5\" cy=\"6\" r=\"2\""
                        " fill=\"#ff0080\" stroke=\"#222\" stroke-width=\"1\"/>"));
    TEST_ASSERT_SIZE(strlen(svg), length);
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_deep_math_scripts_are_bounded(void)
{
    enum { nesting = 48 };
    static const char prefix[] = "@document(version:1){@math{";
    static const char suffix[] = "}}";
    const size_t source_length = sizeof(prefix) - 1u + nesting * 3u + 1u +
                                 nesting + sizeof(suffix) - 1u;
    char *source = malloc(source_length + 1u);
    TEST_ASSERT(source != NULL);
    char *cursor = source;
    memcpy(cursor, prefix, sizeof(prefix) - 1u);
    cursor += sizeof(prefix) - 1u;
    for (size_t index = 0u; index < nesting; ++index) {
        memcpy(cursor, "a_{", 3u);
        cursor += 3u;
    }
    *cursor++ = 'x';
    for (size_t index = 0u; index < nesting; ++index) *cursor++ = '}';
    memcpy(cursor, suffix, sizeof(suffix) - 1u);
    cursor += sizeof(suffix) - 1u;
    *cursor = '\0';

    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    const TrytrStatus status = render(source, &svg, &length, &document);
    const bool rendered = status == TRYTR_STATUS_OK && svg != NULL && length > 0u;
    const bool deep_source_is_visible = rendered && contains(svg, "a_{");
    free(svg);
    trytr_document_destroy(document);
    free(source);
    TEST_ASSERT_INT(TRYTR_STATUS_OK, status);
    TEST_ASSERT(rendered);
    TEST_ASSERT(deep_source_is_visible);
    return true;
}

static bool test_empty_document_has_minimal_valid_svg(void)
{
    static const char source[] = "@document(version:1){}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(contains(svg, "width=\"1\" height=\"1\" viewBox=\"0 0 1 1\""));
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_unicode_text_bounds_count_characters_not_bytes(void)
{
    static const char source[] = "@document(version:1){@text{é}}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(contains(svg, "viewBox=\"0 0 48 54\""));
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_nested_transforms_apply_in_svg_and_viewbox(void)
{
    static const char source[] =
        "@document(version:1){"
        "@group #outer(10,20){"
        "@group #inner(-2,3){@rect #box(1,2,3,4)}"
        "}"
        "}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(contains(svg, "<g id=\"outer\" transform=\"translate(10 20)\">"));
    TEST_ASSERT(contains(svg, "<g id=\"inner\" transform=\"translate(-2 3)\">"));
    TEST_ASSERT(contains(svg, "<rect id=\"box\" x=\"1\" y=\"2\" width=\"3\" height=\"4\""));
    TEST_ASSERT(contains(svg, "viewBox=\"-7 9 35 36\""));
    TEST_ASSERT(contains(svg, "</g>\n</g>\n</svg>"));
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_fixed_canvas_sets_svg_viewport_and_clips_overflow(void)
{
    static const char source[] =
        "@document(version:1,width:100,height:80){@rect #box(-10,-10,30,30)}";
    TrytrDocument *document = NULL;
    char *svg = NULL;
    size_t length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, render(source, &svg, &length, &document));
    TEST_ASSERT(contains(svg,
        "width=\"100\" height=\"80\" viewBox=\"0 0 100 80\" overflow=\"hidden\""));
    TEST_ASSERT(contains(svg, "<rect id=\"box\" x=\"-10\" y=\"-10\""));
    int64_t canvas_width = 0;
    int64_t canvas_height = 0;
    TEST_ASSERT(trytr_document_get_canvas_size(document, &canvas_width, &canvas_height));
    TEST_ASSERT_INT(100, canvas_width);
    TEST_ASSERT_INT(80, canvas_height);
    TEST_ASSERT_SIZE(strlen(svg), length);
    free(svg);
    trytr_document_destroy(document);
    return true;
}

static bool test_invalid_fixed_canvas_sizes_are_rejected(void)
{
    static const char *const sources[] = {
        "@document(version:1,width:100){}",
        "@document(version:1,width:0,height:80){}",
        "@document(version:1,width:100,height:1000001){}"
    };
    for (size_t index = 0u; index < sizeof(sources) / sizeof(sources[0]); ++index) {
        TrytrDocument *document = NULL;
        TEST_ASSERT_INT(TRYTR_STATUS_OK,
                        trytr_document_parse(sources[index], strlen(sources[index]), &document));
        TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                        trytr_document_validate(document));
        TEST_ASSERT(document_has_diagnostic(document, "TRYTR-E-CANVAS-SIZE"));
        trytr_document_destroy(document);
    }
    return true;
}

static bool test_invalid_document_and_arguments(void)
{
    static const char invalid_source[] = "@document(version:1){@circle(0,0,-1)}";
    TrytrDocument *document = NULL;
    char *svg = (char *)1;
    size_t length = 99u;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(invalid_source, sizeof(invalid_source) - 1u, &document));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT,
                    trytr_svg_render(document, &svg, &length));
    TEST_ASSERT(svg == NULL);
    TEST_ASSERT_SIZE(0u, length);
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_svg_render(NULL, &svg, &length));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_svg_render(document, NULL, &length));
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_svg_render(document, &svg, NULL));
    trytr_document_destroy(document);
    return true;
}

static bool test_svg_output_limit(void)
{
    const size_t lines = 300000u;
    const char prefix[] = "@document(version:1){@text{";
    const char suffix[] = "}}";
    const size_t content_length = lines * 2u;
    const size_t source_length = sizeof(prefix) - 1u + content_length + sizeof(suffix) - 1u;
    char *source = malloc(source_length + 1u);
    TEST_ASSERT(source != NULL);
    char *cursor = source;
    memcpy(cursor, prefix, sizeof(prefix) - 1u);
    cursor += sizeof(prefix) - 1u;
    for (size_t index = 0u; index < lines; ++index) {
        *cursor++ = 'x';
        *cursor++ = '\n';
    }
    memcpy(cursor, suffix, sizeof(suffix) - 1u);

    TrytrDocument *document = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK,
                    trytr_document_parse(source, source_length, &document));
    char *svg = NULL;
    size_t svg_length = 0u;
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED,
                    trytr_svg_render(document, &svg, &svg_length));
    TEST_ASSERT(svg == NULL);
    TEST_ASSERT_SIZE(0u, svg_length);
    trytr_document_destroy(document);
    free(source);
    return true;
}

static const TrytrTestCase tests[] = {
    {"shapes and escaped text", test_svg_renders_all_shapes_and_escapes_text},
    {"folded path coordinates", test_svg_receives_only_folded_path_coordinates},
    {"shape paint colors", test_shape_fill_and_stroke_colors},
    {"text styling", test_text_font_size_color_and_alignment},
    {"word wrapping", test_text_width_wraps_at_words_without_splitting_long_words},
    {"math scripts", test_math_scripts_render_as_tspans},
    {"native host math handoff", test_math_can_be_deferred_to_a_native_host_renderer},
    {"bounded math script depth", test_deep_math_scripts_are_bounded},
    {"empty document", test_empty_document_has_minimal_valid_svg},
    {"Unicode text bounds", test_unicode_text_bounds_count_characters_not_bytes},
    {"nested group transforms", test_nested_transforms_apply_in_svg_and_viewbox},
    {"fixed canvas viewport", test_fixed_canvas_sets_svg_viewport_and_clips_overflow},
    {"invalid fixed canvas sizes", test_invalid_fixed_canvas_sizes_are_rejected},
    {"invalid document and arguments", test_invalid_document_and_arguments},
    {"SVG output limit", test_svg_output_limit}
};

TEST_MAIN(tests)
