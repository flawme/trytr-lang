#include "diagnostic.h"

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void trytr_diagnostic_list_init(
    TrytrDiagnosticList *list,
    TrytrArena *arena,
    size_t limit
)
{
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
    list->limit = limit;
    list->arena = arena;
    list->allocation_failed = false;
}

static bool diagnostic_list_grow(TrytrDiagnosticList *list)
{
    if (list == NULL || list->arena == NULL || list->capacity >= list->limit) return false;

    size_t capacity = list->capacity == 0 ? 8u : list->capacity * 2u;
    if (capacity < list->capacity) return false;
    if (capacity > list->limit) capacity = list->limit;
    if (capacity == 0) return false;
    if (capacity > SIZE_MAX / sizeof(*list->items)) return false;

    TrytrDiagnostic *items = trytr_arena_alloc(list->arena, capacity * sizeof(*items));
    if (items == NULL) {
        list->allocation_failed = true;
        return false;
    }
    if (list->count != 0) {
        memcpy(items, list->items, list->count * sizeof(*items));
    }
    list->items = items;
    list->capacity = capacity;
    return true;
}

bool trytr_diagnostic_add(
    TrytrDiagnosticList *list,
    TrytrDiagnosticSeverity severity,
    const char *code,
    TrytrSourceLocation location,
    const char *format,
    ...
)
{
    if (list == NULL || list->arena == NULL || code == NULL || format == NULL ||
        list->count >= list->limit) {
        return false;
    }
    if (list->count == list->capacity && !diagnostic_list_grow(list)) return false;

    va_list arguments;
    va_start(arguments, format);
    va_list copy;
    va_copy(copy, arguments);
    int required = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (required < 0) {
        va_end(arguments);
        return false;
    }

    char *message = trytr_arena_alloc(list->arena, (size_t)required + 1u);
    if (message == NULL) {
        list->allocation_failed = true;
        va_end(arguments);
        return false;
    }
    (void)vsnprintf(message, (size_t)required + 1u, format, arguments);
    va_end(arguments);

    TrytrDiagnostic *diagnostic = &list->items[list->count++];
    diagnostic->code = code;
    diagnostic->severity = severity;
    diagnostic->location = location;
    diagnostic->message = message;
    return true;
}

bool trytr_diagnostic_has_errors(const TrytrDiagnosticList *list)
{
    if (list == NULL) return false;
    for (size_t index = 0; index < list->count; ++index) {
        if (list->items[index].severity >= TRYTR_DIAGNOSTIC_ERROR) return true;
    }
    return false;
}
