#include "trytr/html.h"
#include "trytr/layout.h"
#include "trytr/math/mathml.h"
#include "svg_render.h"
#include "limits.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct HtmlBuffer {
    char *data;
    size_t length, capacity;
    TrytrStatus status;
} HtmlBuffer;

static bool reserve(HtmlBuffer *buffer, size_t extra)
{
    if (buffer->status != TRYTR_STATUS_OK) return false;
    if (extra > TRYTR_DEFAULT_SVG_OUTPUT_LIMIT - buffer->length) {
        buffer->status = TRYTR_STATUS_LIMIT_EXCEEDED;
        return false;
    }
    const size_t required = buffer->length + extra + 1u;
    if (required <= buffer->capacity) return true;
    size_t capacity = buffer->capacity == 0u ? 1024u : buffer->capacity;
    while (capacity < required) {
        capacity *= 2u;
        if (capacity > TRYTR_DEFAULT_SVG_OUTPUT_LIMIT) {
            capacity = TRYTR_DEFAULT_SVG_OUTPUT_LIMIT + 1u;
            break;
        }
    }
    char *data = realloc(buffer->data, capacity);
    if (data == NULL) { buffer->status = TRYTR_STATUS_NO_MEMORY; return false; }
    buffer->data = data;
    buffer->capacity = capacity;
    return true;
}

static bool append(HtmlBuffer *buffer, const char *text, size_t length)
{
    if (!reserve(buffer, length)) return false;
    if (length != 0u) memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool literal(HtmlBuffer *buffer, const char *text)
{ return append(buffer, text, strlen(text)); }

static bool format(HtmlBuffer *buffer, const char *text, ...)
{
    va_list args, sizing;
    va_start(args, text);
    va_copy(sizing, args);
    const int needed = vsnprintf(NULL, 0u, text, sizing);
    va_end(sizing);
    if (needed < 0) buffer->status = TRYTR_STATUS_EXECUTION_FAILED;
    if (needed < 0 || !reserve(buffer, (size_t)needed)) { va_end(args); return false; }
    (void)vsnprintf(buffer->data + buffer->length, buffer->capacity - buffer->length, text, args);
    va_end(args);
    buffer->length += (size_t)needed;
    return true;
}

static bool escaped(HtmlBuffer *buffer, const char *text, size_t length)
{
    for (size_t index = 0u; index < length; ++index) {
        const unsigned char c = (unsigned char)text[index];
        const char *replacement = c == '&' ? "&amp;" : c == '<' ? "&lt;" :
            c == '>' ? "&gt;" : c == '"' ? "&quot;" : c == '\'' ? "&#39;" :
            (c < 0x20u && c != '\n' && c != '\r' && c != '\t') ? "\xEF\xBF\xBD" : NULL;
        if (!(replacement == NULL ? append(buffer, text + index, 1u)
                                  : literal(buffer, replacement))) return false;
    }
    return true;
}

static bool inline_line(HtmlBuffer *buffer, const TrytrDocumentElement *element,
                         const TrytrLayoutLine *line, const TrytrInlineRun **cursor)
{
    const size_t end = line->start + line->length;
    while (*cursor != NULL) {
        const TrytrInlineRun *run = *cursor;
        const size_t run_end = run->source_offset + run->source_length;
        if (run->source_offset >= end) break;
        if (run_end <= line->start) { *cursor = run->next; continue; }
        if (run->kind == TRYTR_INLINE_MATH) {
            if (run->source_offset < line->start || run_end > end) {
                buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
                return false;
            }
            char *fragment = NULL;
            size_t length = 0u;
            const TrytrStatus status = trytr_mathml_render_inline_run(element, run, &fragment, &length);
            if (status != TRYTR_STATUS_OK) { buffer->status = status; return false; }
            const bool appended = append(buffer, fragment, length);
            free(fragment);
            if (!appended) return false;
        } else {
            const size_t start = run->source_offset > line->start ? run->source_offset : line->start;
            const size_t stop = run_end < end ? run_end : end;
            const size_t offset = run->content_offset + start - run->source_offset;
            const size_t length = run->source_length == run->content_length
                ? stop - start : run->content_length;
            if (!escaped(buffer, element->text + offset, length)) return false;
        }
        if (run_end > end) break;
        *cursor = run->next;
    }
    return true;
}

static bool render_block(HtmlBuffer *buffer, const TrytrLayout *layout, size_t index)
{
    const TrytrLayoutBlock *block = trytr_layout_block_at(layout, index);
    const TrytrLayoutElement *item = trytr_layout_element_at(layout, block->first_element);
    const TrytrDocumentElement *element = &item->element;
    const char *tag = block->kind == TRYTR_LAYOUT_HEADING ? "h1" :
                      block->kind == TRYTR_LAYOUT_TEXT ? "p" :
                      block->kind == TRYTR_LAYOUT_CANVAS ? "figure" : "div";
    if (!format(buffer, "<%s class=\"trytr-block\" data-kind=\"%s\"",
                tag, block->kind == TRYTR_LAYOUT_CANVAS ? "canvas" :
                     block->kind == TRYTR_LAYOUT_MATH ? "math" :
                     block->kind == TRYTR_LAYOUT_HEADING ? "heading" : "text")) return false;
    if (block->kind != TRYTR_LAYOUT_CANVAS && element->id != NULL &&
        (!literal(buffer, " id=\"") || !escaped(buffer, element->id, strlen(element->id)) ||
         !literal(buffer, "\""))) return false;
    if (!format(buffer, " style=\"font-size:%" PRId64 "px;line-height:%" PRId64 "px;",
                block->font_size == 0 ? 16 : block->font_size,
                block->line_height == 0 ? 22 : block->line_height)) return false;
    if (element->has_text_color &&
        !format(buffer, "color:#%06" PRIx32 ";", element->text_color_rgb)) return false;
    if (element->has_text_alignment &&
        !format(buffer, "text-align:%s;", element->text_alignment == TRYTR_TEXT_ALIGN_CENTER
                    ? "center" : element->text_alignment == TRYTR_TEXT_ALIGN_RIGHT ? "right" : "left"))
        return false;
    if (block->kind != TRYTR_LAYOUT_CANVAS &&
        !format(buffer, "width:%" PRId64 "px;", block->width > 0 ? block->width : 1)) return false;
    if (block->kind != TRYTR_LAYOUT_CANVAS && (item->translate_x != 0 || item->translate_y != 0) &&
        !format(buffer, "transform:translate(%" PRId64 "px,%" PRId64 "px);",
                item->translate_x, item->translate_y)) return false;
    if (!literal(buffer, "\">")) return false;
    if (block->kind == TRYTR_LAYOUT_CANVAS || block->kind == TRYTR_LAYOUT_MATH) {
        char *fragment = NULL;
        size_t length = 0u;
        const TrytrStatus status = block->kind == TRYTR_LAYOUT_CANVAS
            ? trytr_svg_render_layout_canvas(layout, index, &fragment, &length)
            : trytr_mathml_render_element(element, &fragment, &length);
        if (status != TRYTR_STATUS_OK) { buffer->status = status; return false; }
        const bool appended = append(buffer, fragment, length);
        free(fragment);
        if (!appended) return false;
    } else {
        const TrytrInlineRun *cursor = element->inline_runs;
        for (size_t line_index = 0u; line_index < block->line_count; ++line_index) {
            const TrytrLayoutLine *line = trytr_layout_line_at(layout, block->first_line + line_index);
            if (!literal(buffer, "<span class=\"trytr-line\">") ||
                !(element->inline_runs != NULL ? inline_line(buffer, element, line, &cursor)
                    : escaped(buffer, element->text + line->start, line->length)) ||
                !literal(buffer, "</span>")) return false;
        }
    }
    return format(buffer, "</%s>\n", tag);
}

TrytrStatus trytr_html_render(TrytrDocument *document, char **out_html, size_t *out_length)
{
    if (out_html != NULL) *out_html = NULL;
    if (out_length != NULL) *out_length = 0u;
    if (document == NULL || out_html == NULL || out_length == NULL)
        return TRYTR_STATUS_INVALID_ARGUMENT;
    TrytrLayout *layout = NULL;
    const TrytrLayoutOptions options = {.width = 640, .gap = 8};
    TrytrStatus status = trytr_layout_create(document, &options, &layout);
    if (status != TRYTR_STATUS_OK) return status;
    HtmlBuffer buffer = {.status = TRYTR_STATUS_OK};
    if (literal(&buffer,
        "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Trytr document</title><style>"
        "body{margin:0;background:#f5f3ee;color:#252b32;font-family:system-ui,sans-serif}"
        "main{max-width:640px;margin:40px auto;padding:32px;background:#fff;border-radius:12px}"
        ".trytr-block{margin:0 0 8px;max-width:100%;overflow:auto}"
        ".trytr-line{display:block;white-space:pre;min-height:1lh}"
        "math[display=block]{margin:8px 0;padding:8px 0}"
        "math[display=inline]{margin:0;padding:0}svg{display:block}"
        "@media(max-width:720px){main{margin:0;padding:20px;border-radius:0}}"
        "@media print{body{background:white}main{margin:0;padding:0}}"
        "</style></head><body><main>\n")) {
        for (size_t index = 0u; index < trytr_layout_block_count(layout); ++index) {
            if (!render_block(&buffer, layout, index)) break;
        }
    }
    if (buffer.status == TRYTR_STATUS_OK) (void)literal(&buffer, "</main></body></html>\n");
    status = buffer.status;
    trytr_layout_destroy(layout);
    if (status != TRYTR_STATUS_OK) { free(buffer.data); return status; }
    *out_html = buffer.data;
    *out_length = buffer.length;
    return TRYTR_STATUS_OK;
}
