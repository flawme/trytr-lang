#ifndef TRYTR_LAYOUT_TEXT_H
#define TRYTR_LAYOUT_TEXT_H

#include "trytr/document.h"

typedef struct TrytrTextLineIterator {
    const char *text;
    size_t length;
    size_t cursor;
    int64_t font_size;
    int64_t wrap_width;
    bool done;
    size_t paragraph_end;
    bool have_paragraph;
    const TrytrInlineRun *inline_run;
} TrytrTextLineIterator;

typedef struct TrytrTextLine {
    size_t start;
    size_t end;
    int64_t width;
} TrytrTextLine;

void trytr_layout_text_metrics(const TrytrDocumentElement *element,
                               int64_t *font_size, int64_t *line_height);
TrytrTextLineIterator trytr_layout_text_iterator(
    const TrytrDocumentElement *element, int64_t font_size);
bool trytr_layout_text_line_next(TrytrTextLineIterator *iterator, TrytrTextLine *out_line);

#endif
