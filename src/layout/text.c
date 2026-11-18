#include "text.h"

static size_t line_end(const char *text, size_t length, size_t start)
{
    size_t end = start;
    while (end < length && text[end] != '\n' && text[end] != '\r') ++end;
    return end;
}

static size_t next_line_start(const char *text, size_t length, size_t end)
{
    if (end >= length) return length;
    if (text[end] == '\r' && end + 1u < length && text[end + 1u] == '\n') {
        return end + 2u;
    }
    return end + 1u;
}

static size_t utf8_scalar_width(unsigned char first)
{
    if (first <= 0x7Fu) return 1u;
    if (first <= 0xDFu) return 2u;
    if (first <= 0xEFu) return 3u;
    return 4u;
}

static size_t utf8_scalar_count(const char *text, size_t length)
{
    size_t count = 0u;
    for (size_t offset = 0u; offset < length; ++count) {
        size_t width = utf8_scalar_width((unsigned char)text[offset]);
        if (width > length - offset) width = 1u;
        offset += width;
    }
    return count;
}

void trytr_layout_text_metrics(const TrytrDocumentElement *element,
                        int64_t *font_size, int64_t *line_height)
{
    int64_t line_gap;
    switch (element->kind) {
        case TRYTR_ELEMENT_HEADING:
            *font_size = 28;
            line_gap = 8;
            break;
        case TRYTR_ELEMENT_MATH:
            *font_size = 18;
            line_gap = 8;
            break;
        default:
            *font_size = 16;
            line_gap = 6;
            break;
    }
    if (element->has_font_size) *font_size = element->font_size;
    *line_height = *font_size + line_gap;
}

static int64_t estimated_text_width(const char *text, size_t length,
                                    int64_t font_size)
{
    return (int64_t)utf8_scalar_count(text, length) * font_size;
}

static bool is_text_space(char value)
{
    return value == ' ' || value == '\t';
}

static void text_iterator_advance_paragraph(TrytrTextLineIterator *iterator,
                                            size_t paragraph_end)
{
    iterator->have_paragraph = false;
    if (paragraph_end == iterator->length) {
        iterator->done = true;
    } else {
        iterator->cursor = next_line_start(iterator->text,
                                           iterator->length,
                                           paragraph_end);
    }
}

static void text_iterator_advance_word(TrytrTextLineIterator *iterator,
                                       size_t word_end,
                                       size_t paragraph_end)
{
    size_t next_word = word_end;
    while (next_word < paragraph_end &&
           is_text_space(iterator->text[next_word])) ++next_word;
    if (next_word == paragraph_end) {
        text_iterator_advance_paragraph(iterator, paragraph_end);
    } else {
        iterator->cursor = next_word;
    }
}

bool trytr_layout_text_line_next(TrytrTextLineIterator *iterator, TrytrTextLine *out_line)
{
    if (iterator == NULL || out_line == NULL || iterator->done) return false;
    if (!iterator->have_paragraph) {
        iterator->paragraph_end = line_end(iterator->text, iterator->length, iterator->cursor);
        iterator->have_paragraph = true;
    }
    const size_t paragraph_end = iterator->paragraph_end;
    if (iterator->wrap_width <= 0) {
        out_line->start = iterator->cursor;
        out_line->end = paragraph_end;
        out_line->width = estimated_text_width(
            iterator->text + iterator->cursor,
            paragraph_end - iterator->cursor,
            iterator->font_size);
        text_iterator_advance_paragraph(iterator, paragraph_end);
        return true;
    }

    size_t word_start = iterator->cursor;
    while (word_start < paragraph_end &&
           is_text_space(iterator->text[word_start])) ++word_start;
    if (word_start == paragraph_end) {
        out_line->start = word_start;
        out_line->end = word_start;
        out_line->width = 0;
        text_iterator_advance_paragraph(iterator, paragraph_end);
        return true;
    }

    const size_t visual_start = word_start;
    size_t last_word_end = word_start;
    int64_t width = 0;
    for (;;) {
        const TrytrInlineRun *word_runs = iterator->inline_run;
        size_t word_end = word_start;
        while (word_end < paragraph_end &&
               !is_text_space(iterator->text[word_end])) {
            while (iterator->inline_run != NULL &&
                   iterator->inline_run->source_offset + iterator->inline_run->source_length <= word_end)
                iterator->inline_run = iterator->inline_run->next;
            const TrytrInlineRun *run = iterator->inline_run;
            if (run != NULL && run->kind == TRYTR_INLINE_MATH && run->source_offset == word_end) {
                word_end += run->source_length;
                continue;
            }
            size_t scalar_width = utf8_scalar_width(
                (unsigned char)iterator->text[word_end]);
            if (scalar_width > paragraph_end - word_end) scalar_width = 1u;
            word_end += scalar_width;
        }
        const int64_t word_width = estimated_text_width(
            iterator->text + word_start, word_end - word_start,
            iterator->font_size);
        const int64_t gap_width = width == 0
            ? 0
            : (int64_t)(word_start - last_word_end) * iterator->font_size;
        const int64_t candidate_width = width + gap_width + word_width;

        if (width > 0 && candidate_width > iterator->wrap_width) {
            out_line->start = visual_start;
            out_line->end = last_word_end;
            out_line->width = width;
            iterator->cursor = word_start;
            iterator->inline_run = word_runs;
            return true;
        }
        if (width == 0 && word_width > iterator->wrap_width) {
            out_line->start = visual_start;
            out_line->end = word_end;
            out_line->width = word_width;
            text_iterator_advance_word(iterator, word_end, paragraph_end);
            return true;
        }

        width = candidate_width;
        last_word_end = word_end;
        size_t next_word = word_end;
        while (next_word < paragraph_end &&
               is_text_space(iterator->text[next_word])) ++next_word;
        if (next_word == paragraph_end) {
            out_line->start = visual_start;
            out_line->end = last_word_end;
            out_line->width = width;
            text_iterator_advance_paragraph(iterator, paragraph_end);
            return true;
        }
        word_start = next_word;
    }
}

TrytrTextLineIterator trytr_layout_text_iterator(const TrytrDocumentElement *element,
                                           int64_t font_size)
{
    return (TrytrTextLineIterator){
        .text = element->text == NULL ? "" : element->text,
        .length = element->text_length,
        .font_size = font_size,
        .inline_run = element->inline_runs,
        .wrap_width = element->has_text_width &&
                      (element->kind == TRYTR_ELEMENT_HEADING ||
                       element->kind == TRYTR_ELEMENT_TEXT)
            ? element->text_width : 0
    };
}

