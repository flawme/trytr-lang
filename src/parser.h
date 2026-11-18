#ifndef TRYTR_SRC_PARSER_H
#define TRYTR_SRC_PARSER_H

#include "arena.h"
#include "ast.h"
#include "diagnostic.h"
#include "limits.h"
#include "trytr/document.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct TrytrParameterEntry {
    TrytrDocumentParameter parameter;
    struct TrytrParameterEntry *next;
} TrytrParameterEntry;

typedef struct TrytrParserOutput {
    TrytrArena arena;
    TrytrDiagnosticList diagnostics;
    TrytrNode *root;
    TrytrParameterEntry *parameters;
    TrytrParameterEntry *parameters_tail;
    size_t parameter_count;
    size_t document_body_end;
    bool initialized;
    bool out_of_memory;
} TrytrParserOutput;

bool trytr_parser_parse(
    const char *source,
    size_t length,
    TrytrParserOutput *output
);

bool trytr_parser_parse_with_parameters(
    const char *source,
    size_t length,
    const TrytrParameterOverride *overrides,
    size_t override_count,
    TrytrParserOutput *output
);

void trytr_parser_output_destroy(TrytrParserOutput *output);

#endif
