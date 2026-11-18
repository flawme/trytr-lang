#include "inline_internal.h"
#include "notation_internal.h"

#include <stdbool.h>

typedef struct InlineParser {
    TrytrArena *arena;
    const char *source;
    size_t limit;
    size_t count;
    TrytrInlineRun *first;
    TrytrInlineRun *last;
} InlineParser;

static TrytrInlineStatus add_run(InlineParser *parser, TrytrInlineRunKind kind,
                                 size_t start, size_t end, size_t content, size_t length)
{
    if (end == start) return TRYTR_INLINE_OK;
    if (parser->count >= parser->limit) return TRYTR_INLINE_LIMIT_EXCEEDED;
    ++parser->count;
    if (parser->arena == NULL) return TRYTR_INLINE_OK;
    TrytrInlineRun *run = trytr_arena_calloc(parser->arena, 1u, sizeof(*run));
    if (run == NULL) return TRYTR_INLINE_NO_MEMORY;
    *run = (TrytrInlineRun){.kind = kind, .source_offset = start, .source_length = end - start,
        .content_offset = content, .content_length = length};
    if (kind == TRYTR_INLINE_MATH) {
        const TrytrMathNotationStatus status = trytr_math_notation_parse(
            parser->arena, parser->source + content, length, &run->math_tree);
        if (status != TRYTR_MATH_NOTATION_OK) return status == TRYTR_MATH_NOTATION_NO_MEMORY
            ? TRYTR_INLINE_NO_MEMORY : TRYTR_INLINE_LIMIT_EXCEEDED;
    }
    if (parser->last == NULL) parser->first = run;
    else parser->last->next = run;
    parser->last = run;
    return TRYTR_INLINE_OK;
}

static bool nonempty(const char *source, size_t start, size_t end)
{
    for (size_t index = start; index < end; ++index) {
        const char byte = source[index];
        if (byte != ' ' && byte != '\t' && byte != '\v' && byte != '\f') return true;
    }
    return false;
}

static bool find_math_end(const char *source, size_t length, size_t start, size_t *out_end)
{
    for (size_t index = start; index < length; ++index) {
        if (source[index] == '\n' || source[index] == '\r') return false;
        if (source[index] != '\\' || index + 1u >= length) continue;
        if (source[index + 1u] == '\n' || source[index + 1u] == '\r') return false;
        if (source[index + 1u] == '(') return false;
        if (source[index + 1u] == ')') { *out_end = index; return nonempty(source, start, index); }
        ++index; /* Commands and escaped backslashes cannot open/close a fence. */
    }
    return false;
}

TrytrInlineStatus trytr_inline_parse(
    TrytrArena *arena, const char *source, size_t length, size_t run_limit,
    const TrytrInlineRun **out_runs, size_t *out_count, size_t *out_error_offset)
{
    *out_runs = NULL;
    *out_count = 0u;
    *out_error_offset = 0u;
    InlineParser parser = {.arena = arena, .source = source, .limit = run_limit};
    size_t plain_start = 0u;
    size_t index = 0u;
    TrytrInlineStatus status = TRYTR_INLINE_OK;
    while (index + 1u < length) {
        if (source[index] != '\\') { ++index; continue; }
        const char next = source[index + 1u];
        if (next == ')') { status = TRYTR_INLINE_INVALID; break; }
        if (next != '(' && next != '\\') { index += 2u; continue; }
        status = add_run(&parser, TRYTR_INLINE_TEXT, plain_start, index,
                          plain_start, index - plain_start);
        if (status != TRYTR_INLINE_OK) break;
        if (next == '\\') {
            status = add_run(&parser, TRYTR_INLINE_TEXT, index, index + 2u, index, 1u);
            if (status != TRYTR_INLINE_OK) break;
            index += 2u;
        } else {
            size_t end = 0u;
            if (!find_math_end(source, length, index + 2u, &end)) {
                status = TRYTR_INLINE_INVALID;
                break;
            }
            status = add_run(&parser, TRYTR_INLINE_MATH, index, end + 2u,
                              index + 2u, end - index - 2u);
            if (status != TRYTR_INLINE_OK) break;
            index = end + 2u;
        }
        plain_start = index;
    }
    if (status == TRYTR_INLINE_OK) {
        status = add_run(&parser, TRYTR_INLINE_TEXT, plain_start, length,
                          plain_start, length - plain_start);
    }
    if (status != TRYTR_INLINE_OK) { *out_error_offset = index; return status; }
    *out_runs = parser.first;
    *out_count = parser.count;
    return TRYTR_INLINE_OK;
}
