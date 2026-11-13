#ifndef TRYTR_SRC_TEXT_RENDER_INTERNAL_H
#define TRYTR_SRC_TEXT_RENDER_INTERNAL_H

#include "trytr/document.h"
#include "limits.h"

#include <stdint.h>

#define TEXT_GRID_MAX_COLUMNS 80
#define TEXT_GRID_MAX_ROWS 32
#define TEXT_OUTPUT_LIMIT (2u * 1024u * 1024u)
typedef struct TextBuffer {
    char *data;
    size_t length;
    size_t capacity;
    TrytrStatus status;
} TextBuffer;

typedef struct TextState {
    TextBuffer output;
    uint32_t cells[TEXT_GRID_MAX_ROWS][TEXT_GRID_MAX_COLUMNS];
    uint8_t block_masks[TEXT_GRID_MAX_ROWS][TEXT_GRID_MAX_COLUMNS];
    size_t columns;
    size_t rows;
    bool have_bounds;
    bool has_geometry;
    int64_t min_x;
    int64_t min_y;
    int64_t max_x;
    int64_t max_y;
    int64_t group_x;
    int64_t group_y;
    int64_t stack_x[TRYTR_DEFAULT_DEPTH_LIMIT];
    int64_t stack_y[TRYTR_DEFAULT_DEPTH_LIMIT];
    size_t group_depth;
} TextState;

bool trytr_text_buffer_append(TextBuffer *buffer, const char *data, size_t length);
void trytr_text_grid_init(TextState *state, int64_t width, int64_t height);
bool trytr_text_draw_element(const TrytrDocumentElement *element, void *user_data);
bool trytr_text_append_grid(TextState *state);

#endif
