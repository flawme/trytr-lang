#include "trytr/document.h"

#include "parser.h"
#include "ir.h"
#include "validator.h"
#include "vm.h"

#include <stdlib.h>
#include <string.h>

struct TrytrDocument {
    TrytrParserOutput output;
    TrytrIRProgram program;
    bool validated;
    bool parsed;
    bool compiled;
    bool executing;
};

const char *trytr_status_string(TrytrStatus status)
{
    switch (status) {
        case TRYTR_STATUS_OK: return "ok";
        case TRYTR_STATUS_INVALID_ARGUMENT: return "invalid argument";
        case TRYTR_STATUS_INVALID_DOCUMENT: return "invalid document";
        case TRYTR_STATUS_NO_MEMORY: return "out of memory";
        case TRYTR_STATUS_LIMIT_EXCEEDED: return "resource limit exceeded";
        case TRYTR_STATUS_EXECUTION_FAILED: return "document execution failed";
    }
    return "unknown status";
}

static TrytrStatus document_status(const TrytrDocument *document)
{
    if (document->output.out_of_memory || document->output.diagnostics.allocation_failed) {
        return TRYTR_STATUS_NO_MEMORY;
    }
    if (!document->parsed && document->output.diagnostics.count == 0) {
        return TRYTR_STATUS_NO_MEMORY;
    }
    for (size_t index = 0; index < document->output.diagnostics.count; ++index) {
        const TrytrDiagnostic *diagnostic = &document->output.diagnostics.items[index];
        if (diagnostic->severity < TRYTR_DIAGNOSTIC_ERROR) continue;
        if (strcmp(diagnostic->code, "TRYTR-E-MEMORY") == 0) {
            return TRYTR_STATUS_NO_MEMORY;
        }
        if (strcmp(diagnostic->code, "TRYTR-E-LIMIT") == 0) {
            return TRYTR_STATUS_LIMIT_EXCEEDED;
        }
    }
    return trytr_diagnostic_has_errors(&document->output.diagnostics)
        ? TRYTR_STATUS_INVALID_DOCUMENT
        : TRYTR_STATUS_OK;
}

TrytrStatus trytr_document_parse(
    const char *source,
    size_t length,
    TrytrDocument **out_document
)
{
    if (out_document == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;
    *out_document = NULL;
    if (source == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;

    TrytrDocument *document = calloc(1, sizeof(*document));
    if (document == NULL) return TRYTR_STATUS_NO_MEMORY;

    document->parsed = trytr_parser_parse(source, length, &document->output);
    *out_document = document;
    return document_status(document);
}

TrytrStatus trytr_document_validate(TrytrDocument *document)
{
    if (document == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;
    if (!document->validated) {
        trytr_validator_validate(document->output.root, &document->output.diagnostics);
        document->validated = true;
    }
    return document_status(document);
}

bool trytr_document_get_canvas_size(
    const TrytrDocument *document,
    int64_t *out_width,
    int64_t *out_height
)
{
    if (out_width != NULL) *out_width = 0;
    if (out_height != NULL) *out_height = 0;
    if (document == NULL || out_width == NULL || out_height == NULL ||
        document->output.root == NULL ||
        !document->output.root->has_canvas_width ||
        !document->output.root->has_canvas_height) {
        return false;
    }
    *out_width = document->output.root->canvas_width;
    *out_height = document->output.root->canvas_height;
    return true;
}

TrytrStatus trytr_document_execute(
    TrytrDocument *document,
    TrytrElementVisitor visitor,
    void *user_data
)
{
    if (document == NULL || visitor == NULL || document->executing) {
        return TRYTR_STATUS_INVALID_ARGUMENT;
    }
    TrytrStatus status = trytr_document_validate(document);
    if (status != TRYTR_STATUS_OK) return status;
    if (!document->compiled) {
        status = trytr_ir_build(document->output.root, &document->output.arena,
                                &document->program);
        if (status != TRYTR_STATUS_OK) return status;
        document->compiled = true;
    }
    document->executing = true;
    status = trytr_vm_execute(&document->program, visitor, user_data);
    document->executing = false;
    return status;
}

void trytr_document_destroy(TrytrDocument *document)
{
    if (document == NULL) return;
    trytr_parser_output_destroy(&document->output);
    free(document);
}

size_t trytr_document_diagnostic_count(const TrytrDocument *document)
{
    return document == NULL ? 0u : document->output.diagnostics.count;
}

const TrytrDiagnostic *trytr_document_diagnostic_at(
    const TrytrDocument *document,
    size_t index
)
{
    if (document == NULL || index >= document->output.diagnostics.count) return NULL;
    return &document->output.diagnostics.items[index];
}
