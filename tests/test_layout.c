#include "test.h"
#include "trytr/layout.h"

#include <stdlib.h>

static bool create(const char *source, const TrytrLayoutOptions *options,
                   TrytrDocument **document, TrytrLayout **layout)
{
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(source, strlen(source), document));
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_layout_create(*document, options, layout));
    return true;
}

static bool test_reading_order_and_block_separation(void)
{
    static const char source[] =
        "@document(version:1){@heading{Title}@text{Before}@math{\\frac{1}{2}}"
        "@line(0,0,10,10)@circle(5,5,2)@text{After}@path{M 1 2 L 3 4}}";
    TrytrDocument *document = NULL;
    TrytrLayout *layout = NULL;
    TEST_ASSERT(create(source, NULL, &document, &layout));
    TEST_ASSERT_SIZE(6u, trytr_layout_block_count(layout));
    const TrytrLayoutBlockKind expected[] = {TRYTR_LAYOUT_HEADING, TRYTR_LAYOUT_TEXT,
        TRYTR_LAYOUT_MATH, TRYTR_LAYOUT_CANVAS, TRYTR_LAYOUT_TEXT, TRYTR_LAYOUT_CANVAS};
    int64_t bottom = 0;
    for (size_t index = 0u; index < 6u; ++index) {
        const TrytrLayoutBlock *block = trytr_layout_block_at(layout, index);
        TEST_ASSERT(block != NULL);
        TEST_ASSERT_INT(expected[index], block->kind);
        TEST_ASSERT(block->y >= bottom);
        TEST_ASSERT(block->width > 0 && block->height > 0);
        bottom = block->y + block->height + 8;
    }
    const TrytrLayoutBlock *canvas = trytr_layout_block_at(layout, 3u);
    TEST_ASSERT_SIZE(2u, canvas->element_count);
    TEST_ASSERT_INT(TRYTR_ELEMENT_LINE,
        trytr_layout_element_at(layout, canvas->first_element)->element.kind);
    TEST_ASSERT_INT(TRYTR_ELEMENT_CIRCLE,
        trytr_layout_element_at(layout, canvas->first_element + 1u)->element.kind);
    TEST_ASSERT_INT(16, trytr_layout_block_at(layout, 0u)->y);
    TEST_ASSERT_INT(60, trytr_layout_block_at(layout, 1u)->y);
    trytr_layout_destroy(layout);
    trytr_document_destroy(document);
    return true;
}

static bool test_wrapping_unicode_crlf_and_unbreakable_words(void)
{
    static const char source[] =
        "@document(version:1){@text(font-size:10,width:35){one two three}"
        "@text(font-size:10,width:20){αβ γδ\r\n\r\nz}"
        "@text{}@math{x + y + z}}";
    TrytrDocument *document = NULL;
    TrytrLayout *layout = NULL;
    TEST_ASSERT(create(source, NULL, &document, &layout));
    const TrytrLayoutBlock *first = trytr_layout_block_at(layout, 0u);
    TEST_ASSERT_SIZE(3u, first->line_count);
    TEST_ASSERT_INT(50, first->width);
    TEST_ASSERT_INT(48, first->height);
    const size_t offsets[] = {0u, 4u, 8u};
    const size_t lengths[] = {3u, 3u, 5u};
    for (size_t index = 0u; index < 3u; ++index) {
        const TrytrLayoutLine *line = trytr_layout_line_at(layout, first->first_line + index);
        TEST_ASSERT_SIZE(offsets[index], line->start);
        TEST_ASSERT_SIZE(lengths[index], line->length);
    }
    const TrytrLayoutBlock *unicode = trytr_layout_block_at(layout, 1u);
    TEST_ASSERT_SIZE(4u, unicode->line_count);
    TEST_ASSERT_SIZE(4u, trytr_layout_line_at(layout, unicode->first_line)->length);
    TEST_ASSERT_INT(20, trytr_layout_line_at(layout, unicode->first_line)->width);
    TEST_ASSERT_SIZE(0u, trytr_layout_line_at(layout, unicode->first_line + 2u)->length);
    TEST_ASSERT_SIZE(1u, trytr_layout_block_at(layout, 2u)->line_count);
    TEST_ASSERT_SIZE(1u, trytr_layout_block_at(layout, 3u)->line_count);
    trytr_layout_destroy(layout);
    const TrytrLayoutOptions options = {.width = 10, .gap = 0};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_layout_create(document, &options, &layout));
    TEST_ASSERT_SIZE(1u, trytr_layout_block_at(layout, 3u)->line_count);
    trytr_layout_destroy(layout);
    trytr_document_destroy(document);
    return true;
}

static bool test_nested_canvas_transforms_and_fixed_viewport(void)
{
    static const char source[] =
        "@document(version:1){@group(20,-10){@rect(0,0,10,10)"
        "@group(-5,15){@line(0,0,10,10)}}}";
    TrytrDocument *document = NULL;
    TrytrLayout *layout = NULL;
    TEST_ASSERT(create(source, NULL, &document, &layout));
    const TrytrLayoutBlock *block = trytr_layout_block_at(layout, 0u);
    TEST_ASSERT_SIZE(1u, trytr_layout_block_count(layout));
    TEST_ASSERT_SIZE(2u, block->element_count);
    TEST_ASSERT_INT(-1, block->canvas_x);
    TEST_ASSERT_INT(-26, block->canvas_y);
    TEST_ASSERT_INT(47, block->width);
    TEST_ASSERT_INT(57, block->height);
    const TrytrLayoutElement *line = trytr_layout_element_at(layout, 1u);
    TEST_ASSERT_INT(15, line->translate_x);
    TEST_ASSERT_INT(5, line->translate_y);
    TEST_ASSERT_INT(0, line->element.x);
    trytr_layout_destroy(layout);
    trytr_document_destroy(document);

    static const char fixed[] =
        "@document(version:1,width:100,height:60){@circle(-50,80,5)"
        "@text{Break}@rect(500,500,2,2)}";
    TEST_ASSERT(create(fixed, NULL, &document, &layout));
    for (size_t index = 0u; index <= 2u; index += 2u) {
        block = trytr_layout_block_at(layout, index);
        TEST_ASSERT_INT(TRYTR_LAYOUT_CANVAS, block->kind);
        TEST_ASSERT_INT(0, block->canvas_x);
        TEST_ASSERT_INT(0, block->canvas_y);
        TEST_ASSERT_INT(100, block->width);
        TEST_ASSERT_INT(60, block->height);
    }
    trytr_layout_destroy(layout);
    trytr_document_destroy(document);
    return true;
}

static bool test_host_width_overrides_and_parameter_snapshots(void)
{
    static const char source[] = "@document(version:1){"
        "@param #n(min:1,max:10,value:2,step:1){N}"
        "@text(font-size:10,width:100){a b c}@path{M #n 0 L (#n * 2) 10}}";
    TrytrDocument *document = NULL;
    TrytrLayout *layout = NULL;
    const TrytrLayoutOptions options = {.width = 20, .gap = 12};
    TEST_ASSERT(create(source, &options, &document, &layout));
    TEST_ASSERT_SIZE(3u, trytr_layout_block_at(layout, 0u)->line_count);
    TEST_ASSERT_STRING("M 2 0 L 4 10", trytr_layout_element_at(layout, 1u)->element.text);
    TrytrDocument *changed = NULL;
    const TrytrParameterOverride override = {.id = "n", .value = 7};
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse_with_parameters(
        source, sizeof(source) - 1u, &override, 1u, &changed));
    TrytrLayout *updated = NULL;
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_layout_create(changed, &options, &updated));
    TEST_ASSERT_STRING("M 7 0 L 14 10", trytr_layout_element_at(updated, 1u)->element.text);
    TEST_ASSERT_STRING("M 2 0 L 4 10", trytr_layout_element_at(layout, 1u)->element.text);
    trytr_layout_destroy(updated);
    trytr_document_destroy(changed);
    trytr_layout_destroy(layout);
    trytr_document_destroy(document);
    return true;
}

static bool test_atomic_failure_limits_and_empty_documents(void)
{
    TrytrDocument *document = NULL;
    TrytrLayout *layout = NULL;
    TEST_ASSERT(create("@document(version:1){}", NULL, &document, &layout));
    TEST_ASSERT_SIZE(0u, trytr_layout_block_count(layout));
    TEST_ASSERT(trytr_layout_block_at(layout, 0u) == NULL);
    TEST_ASSERT(trytr_layout_line_at(layout, 0u) == NULL);
    TEST_ASSERT(trytr_layout_element_at(layout, 0u) == NULL);
    trytr_layout_destroy(layout);
    const TrytrLayoutOptions invalid[] = {{.width = -1}, {.width = 1000001},
                                         {.gap = -1}, {.gap = 1000001}};
    for (size_t index = 0u; index < 4u; ++index) {
        layout = (TrytrLayout *)1;
        TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT,
                        trytr_layout_create(document, &invalid[index], &layout));
        TEST_ASSERT(layout == NULL);
    }
    trytr_document_destroy(document);

    const size_t lines = 65537u;
    const char prefix[] = "@document(version:1){@text{";
    char *source = malloc(sizeof(prefix) + lines * 2u + 3u);
    TEST_ASSERT(source != NULL);
    memcpy(source, prefix, sizeof(prefix) - 1u);
    char *cursor = source + sizeof(prefix) - 1u;
    for (size_t index = 0u; index < lines; ++index) { *cursor++ = 'a'; *cursor++ = ' '; }
    memcpy(cursor, "}}", 3u);
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(source, strlen(source), &document));
    const TrytrLayoutOptions narrow = {.width = 1};
    layout = (TrytrLayout *)1;
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED, trytr_layout_create(document, &narrow, &layout));
    TEST_ASSERT(layout == NULL);
    trytr_document_destroy(document);
    free(source);

    const char *element = "@text{x}";
    const char *document_start = "@document(version:1){";
    source = malloc(strlen(document_start) + 1100u * strlen(element) + 2u);
    TEST_ASSERT(source != NULL);
    strcpy(source, document_start);
    cursor = source + strlen(document_start);
    for (size_t index = 0u; index < 1100u; ++index) {
        memcpy(cursor, element, strlen(element));
        cursor += strlen(element);
    }
    memcpy(cursor, "}", 2u);
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(source, strlen(source), &document));
    const TrytrLayoutOptions huge_gaps = {.gap = 1000000};
    TEST_ASSERT_INT(TRYTR_STATUS_LIMIT_EXCEEDED, trytr_layout_create(document, &huge_gaps, &layout));
    TEST_ASSERT(layout == NULL);
    trytr_document_destroy(document);
    free(source);

    const char invalid_source[] = "@document(version:1){@rect(0,0,-1,5)}";
    TEST_ASSERT_INT(TRYTR_STATUS_OK, trytr_document_parse(
        invalid_source, sizeof(invalid_source) - 1u, &document));
    layout = (TrytrLayout *)1;
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_DOCUMENT, trytr_layout_create(document, NULL, &layout));
    TEST_ASSERT(layout == NULL);
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_layout_create(document, NULL, NULL));
    trytr_document_destroy(document);
    TEST_ASSERT_INT(TRYTR_STATUS_INVALID_ARGUMENT, trytr_layout_create(NULL, NULL, &layout));
    TEST_ASSERT(layout == NULL);
    trytr_layout_destroy(NULL);
    return true;
}

static const TrytrTestCase tests[] = {
    {"reading order and separation", test_reading_order_and_block_separation},
    {"wrapping Unicode and line breaks", test_wrapping_unicode_crlf_and_unbreakable_words},
    {"nested transforms and fixed viewports", test_nested_canvas_transforms_and_fixed_viewport},
    {"host widths and immutable parameter snapshots", test_host_width_overrides_and_parameter_snapshots},
    {"atomic failure limits and empty documents", test_atomic_failure_limits_and_empty_documents}
};
TEST_MAIN(tests)
