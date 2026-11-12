#ifndef TRYTR_SRC_PARSER_H
#define TRYTR_SRC_PARSER_H

#include "arena.h"
#include "ast.h"
#include "diagnostic.h"
#include "limits.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct TrytrParserOutput {
    TrytrArena arena;
    TrytrDiagnosticList diagnostics;
    TrytrNode *root;
    bool initialized;
    bool out_of_memory;
} TrytrParserOutput;

bool trytr_parser_parse(
    const char *source,
    size_t length,
    TrytrParserOutput *output
);

void trytr_parser_output_destroy(TrytrParserOutput *output);

#endif
