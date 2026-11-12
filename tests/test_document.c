#include "trytr/document.h"

#include "test.h"

#include <string.h>

static bool has_code(const TrytrDocument *document, const char *code)
{
    for (size_t index = 0; index < trytr_document_diagnostic_count(document); ++index) {
        const TrytrDiagnostic *diagnostic = trytr_document_diagnostic_at(document, index);
        if (diagnostic != NULL && strcmp(diagnostic->code, code) == 0) return true;
    }
    return false;
}

static TrytrStatus parse_text(const char *source, TrytrDocument **document)
{
    return trytr_document_parse(source, strlen(source), document);
}

static bool test_valid_public_document(void)
{
    const char *source = "@document(version: 1){@heading #title{Title}@rect(0,0,10,20)}";
    TrytrDocument *document = NULL;
    TRYTR_ASSERT(parse_text(source, &document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(document != NULL);
    TRYTR_ASSERT(trytr_document_validate(document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(trytr_document_diagnostic_count(document) == 0u);
    TRYTR_ASSERT(trytr_document_diagnostic_at(document, 0u) == NULL);
    trytr_document_destroy(document);
    return true;
}

static bool test_duplicate_stable_ids_are_rejected(void)
{
    const char *source = "@document(version: 1){@text #same{one}@heading #same{two}}";
    TrytrDocument *document = NULL;
    TRYTR_ASSERT(parse_text(source, &document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(trytr_document_validate(document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TRYTR_ASSERT(has_code(document, "TRYTR-E-DUPLICATE-ID"));
    trytr_document_destroy(document);
    return true;
}

static bool test_rect_properties_are_validated(void)
{
    const char *unknown = "@document(version: 1){@rect{x:0 y:0 width:1 height:1 depth:2}}";
    TrytrDocument *document = NULL;
    TRYTR_ASSERT(parse_text(unknown, &document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(trytr_document_validate(document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TRYTR_ASSERT(has_code(document, "TRYTR-E-PROPERTY"));
    trytr_document_destroy(document);

    const char *missing = "@document(version: 1){@rect{x:0 y:0 width:1}}";
    document = NULL;
    TRYTR_ASSERT(parse_text(missing, &document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(trytr_document_validate(document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TRYTR_ASSERT(has_code(document, "TRYTR-E-REQUIRED"));
    trytr_document_destroy(document);
    return true;
}

static bool test_line_and_circle_properties_are_validated(void)
{
    const char *valid =
        "@document(version:1){@line{x1:-2 y1:3 x2:4 y2:5}"
        "@circle{cx:0 cy:0 radius:7}@ellipse{cx:1 cy:2 rx:3 ry:4}}";
    TrytrDocument *document = NULL;
    TRYTR_ASSERT(parse_text(valid, &document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(trytr_document_validate(document) == TRYTR_STATUS_OK);
    trytr_document_destroy(document);

    const char *out_of_range = "@document(version:1){@circle(0,0,1000001)}";
    document = NULL;
    TRYTR_ASSERT(parse_text(out_of_range, &document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(trytr_document_validate(document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TRYTR_ASSERT(has_code(document, "TRYTR-E-COORDINATE"));
    trytr_document_destroy(document);

    const char *missing = "@document(version:1){@line{x1:0 y1:0 x2:1}}";
    document = NULL;
    TRYTR_ASSERT(parse_text(missing, &document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(trytr_document_validate(document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TRYTR_ASSERT(has_code(document, "TRYTR-E-REQUIRED"));
    trytr_document_destroy(document);

    const char *negative_radius = "@document(version:1){@ellipse(0,0,2,-1)}";
    document = NULL;
    TRYTR_ASSERT(parse_text(negative_radius, &document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(trytr_document_validate(document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TRYTR_ASSERT(has_code(document, "TRYTR-E-COORDINATE"));
    trytr_document_destroy(document);
    return true;
}

static bool test_duplicate_property_is_rejected(void)
{
    const char *source = "@document(version: 1){@rect{x:0 x:1 y:0 width:1 height:1}}";
    TrytrDocument *document = NULL;
    TRYTR_ASSERT(parse_text(source, &document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(trytr_document_validate(document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TRYTR_ASSERT(has_code(document, "TRYTR-E-PROPERTY"));
    trytr_document_destroy(document);
    return true;
}

static bool test_unsupported_version_is_rejected(void)
{
    const char *source = "@document(version: 2){}";
    TrytrDocument *document = NULL;
    TRYTR_ASSERT(parse_text(source, &document) == TRYTR_STATUS_OK);
    TRYTR_ASSERT(trytr_document_validate(document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TRYTR_ASSERT(has_code(document, "TRYTR-E-VERSION"));
    trytr_document_destroy(document);
    return true;
}

static bool test_syntax_errors_expose_structured_diagnostics(void)
{
    const char *source = "@document(version: 1){\n@unknown{}\n@text{valid}}";
    TrytrDocument *document = NULL;
    TRYTR_ASSERT(parse_text(source, &document) == TRYTR_STATUS_INVALID_DOCUMENT);
    TRYTR_ASSERT(document != NULL);
    TRYTR_ASSERT(trytr_document_diagnostic_count(document) > 0u);
    const TrytrDiagnostic *diagnostic = trytr_document_diagnostic_at(document, 0u);
    TRYTR_ASSERT(diagnostic != NULL);
    TRYTR_ASSERT(diagnostic->location.line >= 1u);
    TRYTR_ASSERT(diagnostic->location.column >= 1u);
    TRYTR_ASSERT(diagnostic->message != NULL);
    trytr_document_destroy(document);
    return true;
}

static bool test_invalid_api_arguments(void)
{
    TrytrDocument *document = NULL;
    TRYTR_ASSERT(trytr_document_parse(NULL, 0u, &document) == TRYTR_STATUS_INVALID_ARGUMENT);
    TRYTR_ASSERT(document == NULL);
    TRYTR_ASSERT(trytr_document_parse("", 0u, NULL) == TRYTR_STATUS_INVALID_ARGUMENT);
    TRYTR_ASSERT(trytr_document_validate(NULL) == TRYTR_STATUS_INVALID_ARGUMENT);
    TRYTR_ASSERT(trytr_document_diagnostic_count(NULL) == 0u);
    TRYTR_ASSERT(trytr_document_diagnostic_at(NULL, 0u) == NULL);
    TRYTR_ASSERT(strcmp(trytr_status_string(TRYTR_STATUS_OK), "ok") == 0);
    trytr_document_destroy(NULL);
    return true;
}

int main(void)
{
    TRYTR_RUN(test_valid_public_document);
    TRYTR_RUN(test_duplicate_stable_ids_are_rejected);
    TRYTR_RUN(test_rect_properties_are_validated);
    TRYTR_RUN(test_line_and_circle_properties_are_validated);
    TRYTR_RUN(test_duplicate_property_is_rejected);
    TRYTR_RUN(test_unsupported_version_is_rejected);
    TRYTR_RUN(test_syntax_errors_expose_structured_diagnostics);
    TRYTR_RUN(test_invalid_api_arguments);
    return 0;
}
