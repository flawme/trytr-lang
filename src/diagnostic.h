#ifndef TRYTR_SRC_DIAGNOSTIC_H
#define TRYTR_SRC_DIAGNOSTIC_H

#include "trytr/document.h"

#include "arena.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct TrytrDiagnosticList {
    TrytrDiagnostic *items;
    size_t count;
    size_t capacity;
    size_t limit;
    TrytrArena *arena;
    bool allocation_failed;
} TrytrDiagnosticList;

void trytr_diagnostic_list_init(
    TrytrDiagnosticList *list,
    TrytrArena *arena,
    size_t limit
);

bool trytr_diagnostic_add(
    TrytrDiagnosticList *list,
    TrytrDiagnosticSeverity severity,
    const char *code,
    TrytrSourceLocation location,
    const char *format,
    ...
);

bool trytr_diagnostic_has_errors(const TrytrDiagnosticList *list);

#endif
