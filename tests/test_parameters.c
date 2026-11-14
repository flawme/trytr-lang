#include "trytr/document.h"

#include "test.h"

#include <string.h>

static const char source[] =
    "@document(version:1,width:300,height:180){"
    "@param #lift(min:0,max:20,value:8,step:2){Lift height}"
    "@text #caption{Current peak: #lift}"
    "@math #rule{y = #lift x}"
    "@rect(#lift,0,#lift*2,5)"
    "@plot #response(20,20,240,130){0,0 1,#lift 2,0}"
    "}";

typedef struct RenderCapture {
    int64_t rect_x;
    int64_t rect_width;
    char text[64];
    char math[64];
    char plot[64];
    size_t text_length;
    size_t math_length;
    size_t plot_length;
} RenderCapture;

static bool capture_element(const TrytrDocumentElement *element, void *user_data)
{
    RenderCapture *capture = user_data;
    if (element->kind == TRYTR_ELEMENT_RECT) {
        capture->rect_x = element->x;
        capture->rect_width = element->width;
    } else if (element->kind == TRYTR_ELEMENT_TEXT) {
        capture->text_length = element->text_length;
        if (element->text_length < sizeof(capture->text)) {
            memcpy(capture->text, element->text, element->text_length);
            capture->text[element->text_length] = '\0';
        }
    } else if (element->kind == TRYTR_ELEMENT_MATH) {
        capture->math_length = element->text_length;
        if (element->text_length < sizeof(capture->math)) {
            memcpy(capture->math, element->text, element->text_length);
            capture->math[element->text_length] = '\0';
        }
    } else if (element->kind == TRYTR_ELEMENT_PLOT) {
        capture->plot_length = element->text_length;
        if (element->text_length < sizeof(capture->plot)) {
            memcpy(capture->plot, element->text, element->text_length);
            capture->plot[element->text_length] = '\0';
        }
    }
    return true;
}

static bool has_code(const TrytrDocument *document, const char *code)
{
    for (size_t index = 0u; index < trytr_document_diagnostic_count(document); ++index) {
        const TrytrDiagnostic *diagnostic = trytr_document_diagnostic_at(document, index);
        if (diagnostic != NULL && strcmp(diagnostic->code, code) == 0) return true;
    }
    return false;
}

static bool test_default_parameter_value_reaches_multiple_content_types(void)
{
    TrytrDocument *document = NULL;
    TEST_ASSERT(trytr_document_parse(source, sizeof(source) - 1u, &document) == TRYTR_STATUS_OK);
    TEST_ASSERT(trytr_document_parameter_count(document) == 1u);
    const TrytrDocumentParameter *parameter = trytr_document_parameter_at(document, 0u);
    TEST_ASSERT(parameter != NULL);
    TEST_ASSERT(strcmp(parameter->id, "lift") == 0);
    TEST_ASSERT(strcmp(parameter->label, "Lift height") == 0);
    TEST_ASSERT_INT(0, parameter->minimum);
    TEST_ASSERT_INT(20, parameter->maximum);
    TEST_ASSERT_INT(8, parameter->default_value);
    TEST_ASSERT_INT(8, parameter->value);
    TEST_ASSERT_INT(2, parameter->step);

    RenderCapture capture = {0};
    TEST_ASSERT(trytr_document_execute(document, capture_element, &capture) == TRYTR_STATUS_OK);
    TEST_ASSERT_INT(8, capture.rect_x);
    TEST_ASSERT_INT(16, capture.rect_width);
    TEST_ASSERT(strcmp(capture.text, "Current peak: 8") == 0);
    TEST_ASSERT(strcmp(capture.math, "y = 8 x") == 0);
    TEST_ASSERT(strcmp(capture.plot, "0,0 1,8 2,0") == 0);
    trytr_document_destroy(document);
    return true;
}

static bool test_explicit_parameter_override_rebuilds_bound_content(void)
{
    const TrytrParameterOverride override = {.id = "lift", .value = 14};
    TrytrDocument *document = NULL;
    TEST_ASSERT(trytr_document_parse_with_parameters(
        source, sizeof(source) - 1u, &override, 1u, &document) == TRYTR_STATUS_OK);
    const TrytrDocumentParameter *parameter = trytr_document_parameter_at(document, 0u);
    TEST_ASSERT(parameter != NULL);
    TEST_ASSERT_INT(8, parameter->default_value);
    TEST_ASSERT_INT(14, parameter->value);

    RenderCapture capture = {0};
    TEST_ASSERT(trytr_document_execute(document, capture_element, &capture) == TRYTR_STATUS_OK);
    TEST_ASSERT_INT(14, capture.rect_x);
    TEST_ASSERT_INT(28, capture.rect_width);
    TEST_ASSERT(strcmp(capture.text, "Current peak: 14") == 0);
    TEST_ASSERT(strcmp(capture.math, "y = 14 x") == 0);
    TEST_ASSERT(strcmp(capture.plot, "0,0 1,14 2,0") == 0);
    trytr_document_destroy(document);
    return true;
}

static bool test_invalid_parameter_values_and_references_are_rejected(void)
{
    const TrytrParameterOverride out_of_range = {.id = "lift", .value = 22};
    TrytrDocument *document = NULL;
    TEST_ASSERT(trytr_document_parse_with_parameters(
        source, sizeof(source) - 1u, &out_of_range, 1u, &document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TEST_ASSERT(has_code(document, "TRYTR-E-PARAMETER-RANGE"));
    trytr_document_destroy(document);

    const TrytrParameterOverride off_step = {.id = "lift", .value = 9};
    document = NULL;
    TEST_ASSERT(trytr_document_parse_with_parameters(
        source, sizeof(source) - 1u, &off_step, 1u, &document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TEST_ASSERT(has_code(document, "TRYTR-E-PARAMETER-RANGE"));
    trytr_document_destroy(document);

    const char unknown_reference[] = "@document(version:1){@rect(#missing,0,1,1)}";
    document = NULL;
    TEST_ASSERT(trytr_document_parse(unknown_reference, sizeof(unknown_reference) - 1u,
                                     &document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TEST_ASSERT(has_code(document, "TRYTR-E-PARAMETER"));
    trytr_document_destroy(document);
    return true;
}

static bool test_parameters_must_precede_content_and_overrides_must_be_declared(void)
{
    const char late_parameter[] =
        "@document(version:1){@rect(0,0,1,1)"
        "@param #late(min:0,max:4,value:2,step:1){Late}}";
    TrytrDocument *document = NULL;
    TEST_ASSERT(trytr_document_parse(late_parameter, sizeof(late_parameter) - 1u,
                                     &document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TEST_ASSERT(has_code(document, "TRYTR-E-PARAMETER"));
    trytr_document_destroy(document);

    const TrytrParameterOverride unknown = {.id = "missing", .value = 1};
    document = NULL;
    TEST_ASSERT(trytr_document_parse_with_parameters(
        source, sizeof(source) - 1u, &unknown, 1u, &document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TEST_ASSERT(has_code(document, "TRYTR-E-PARAMETER"));
    trytr_document_destroy(document);
    return true;
}

static bool test_duplicate_override_and_invalid_default_are_rejected(void)
{
    const TrytrParameterOverride duplicates[] = {
        {.id = "lift", .value = 8},
        {.id = "lift", .value = 12}
    };
    TrytrDocument *document = NULL;
    TEST_ASSERT(trytr_document_parse_with_parameters(
        source, sizeof(source) - 1u, duplicates, 2u, &document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TEST_ASSERT(has_code(document, "TRYTR-E-PARAMETER"));
    trytr_document_destroy(document);

    const char off_step_default[] =
        "@document(version:1){@param #sample(min:0,max:10,value:5,step:2){Sample}}";
    document = NULL;
    TEST_ASSERT(trytr_document_parse(off_step_default, sizeof(off_step_default) - 1u,
                                    &document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TEST_ASSERT(has_code(document, "TRYTR-E-PARAMETER-RANGE"));
    trytr_document_destroy(document);

    const char dependent_range[] =
        "@document(version:1){"
        "@param #base(min:0,max:10,value:4,step:2){Base}"
        "@param #other(min:#base,max:20,value:12,step:2){Other}}";
    document = NULL;
    TEST_ASSERT(trytr_document_parse(dependent_range, sizeof(dependent_range) - 1u,
                                    &document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TEST_ASSERT(has_code(document, "TRYTR-E-PARAMETER"));
    trytr_document_destroy(document);
    return true;
}

static const TrytrTestCase tests[] = {
    {"default parameter reaches multiple content types",
     test_default_parameter_value_reaches_multiple_content_types},
    {"parameter override rebuilds bound content",
     test_explicit_parameter_override_rebuilds_bound_content},
    {"invalid parameter values and references are rejected",
     test_invalid_parameter_values_and_references_are_rejected},
    {"parameters precede content and overrides are declared",
     test_parameters_must_precede_content_and_overrides_must_be_declared},
    {"duplicate overrides and invalid defaults are rejected",
     test_duplicate_override_and_invalid_default_are_rejected}
};

TEST_MAIN(tests)
