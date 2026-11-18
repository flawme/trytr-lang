#include "internal.h"

#include <string.h>

size_t trytr_stream_current_parameters(const TrytrStream *stream,
                                       TrytrParameterOverride *overrides)
{
    TrytrDocument *document = stream->current->document;
    const size_t count = trytr_document_parameter_count(document);
    for (size_t i = 0u; i < count; ++i) {
        const TrytrDocumentParameter *parameter = trytr_document_parameter_at(document, i);
        overrides[i] = (TrytrParameterOverride){.id = parameter->id, .value = parameter->value};
    }
    return count;
}

TrytrStatus trytr_stream_set_parameters(TrytrStream *stream, uint64_t expected_revision,
    const TrytrParameterOverride *overrides, size_t count, bool *out_changed)
{
    if (out_changed != NULL) *out_changed = false;
    if (stream == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;
    trytr_stream_clear_error(stream);
    if (out_changed == NULL || (overrides == NULL && count != 0u))
        return trytr_stream_error(stream, TRYTR_STATUS_INVALID_ARGUMENT,
            "TRYTR-E-STREAM-ARG", "parameter changes require valid inputs and an output change flag");
    if (count > TRYTR_DEFAULT_PARAMETER_LIMIT) return trytr_stream_error(stream,
        TRYTR_STATUS_LIMIT_EXCEEDED, "TRYTR-E-LIMIT", "too many parameter changes");
    if (stream->phase == TRYTR_STREAM_APPENDING) return trytr_stream_error(stream,
        TRYTR_STATUS_INVALID_ARGUMENT, "TRYTR-E-STREAM-STATE", "commit or cancel the pending frame before changing parameters");
    if (expected_revision != stream->current->revision) return trytr_stream_error(stream,
        TRYTR_STATUS_REVISION_CONFLICT, "TRYTR-E-REVISION", "parameter base revision is stale");
    TrytrParameterOverride merged[TRYTR_DEFAULT_PARAMETER_LIMIT];
    const size_t total = trytr_stream_current_parameters(stream, merged);
    bool different = false;
    for (size_t i = 0u; i < count; ++i) {
        if (overrides[i].id == NULL || overrides[i].id[0] == '\0')
            return trytr_stream_error(stream, TRYTR_STATUS_INVALID_ARGUMENT,
                "TRYTR-E-PARAMETER", "parameter changes require nonempty identifiers");
        for (size_t j = 0u; j < i; ++j) {
            if (strcmp(overrides[i].id, overrides[j].id) == 0)
                return trytr_stream_error(stream, TRYTR_STATUS_INVALID_DOCUMENT,
                    "TRYTR-E-PARAMETER", "duplicate parameter change");
        }
        size_t index = 0u;
        while (index < total && strcmp(merged[index].id, overrides[i].id) != 0) ++index;
        if (index == total) return trytr_stream_error(stream, TRYTR_STATUS_INVALID_DOCUMENT,
            "TRYTR-E-PARAMETER", "change refers to an undeclared parameter");
        different |= merged[index].value != overrides[i].value;
        merged[index].value = overrides[i].value;
    }
    if (!different) return TRYTR_STATUS_OK;
    return trytr_stream_commit_source(stream, stream->source, stream->source_length,
                                     merged, total, out_changed);
}
