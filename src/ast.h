#ifndef TRYTR_SRC_AST_H
#define TRYTR_SRC_AST_H

#include "arena.h"
#include "diagnostic.h"
#include "trytr/math/notation.h"
#include "trytr/inline.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum TrytrNodeKind {
    TRYTR_NODE_DOCUMENT = 0,
    TRYTR_NODE_HEADING,
    TRYTR_NODE_TEXT,
    TRYTR_NODE_MATH,
    TRYTR_NODE_RECT,
    TRYTR_NODE_LINE,
    TRYTR_NODE_CIRCLE,
    TRYTR_NODE_PATH,
    TRYTR_NODE_GROUP,
    TRYTR_NODE_ELLIPSE,
    TRYTR_NODE_POLYGON,
    TRYTR_NODE_ROUNDRECT,
    TRYTR_NODE_PLOT
} TrytrNodeKind;

typedef struct TrytrProperty {
    char *name;
    int64_t integer_value;
    TrytrSourceLocation location;
    struct TrytrProperty *next;
} TrytrProperty;

typedef struct TrytrNode {
    TrytrNodeKind kind;
    char *id;
    char *text;
    size_t text_length;
    const TrytrMathNode *math_tree;
    const TrytrInlineRun *inline_runs;
    int64_t version;
    int64_t canvas_width;
    int64_t canvas_height;
    bool has_canvas_width;
    bool has_canvas_height;
    int64_t group_translate_x;
    int64_t group_translate_y;
    int64_t path_min_x;
    int64_t path_min_y;
    int64_t path_max_x;
    int64_t path_max_y;
    size_t path_segment_count;
    int64_t polygon_min_x;
    int64_t polygon_min_y;
    int64_t polygon_max_x;
    int64_t polygon_max_y;
    size_t polygon_point_count;
    double plot_min_x;
    double plot_min_y;
    double plot_max_x;
    double plot_max_y;
    size_t plot_point_count;
    bool formula_plot;
    TrytrSourceLocation location;
    TrytrProperty *properties;
    TrytrProperty *properties_tail;
    struct TrytrNode *children;
    struct TrytrNode *children_tail;
    struct TrytrNode *next;
} TrytrNode;

TrytrNode *trytr_ast_node_new(
    TrytrArena *arena,
    size_t *node_count,
    size_t node_limit,
    TrytrNodeKind kind,
    TrytrSourceLocation location
);

void trytr_ast_add_child(TrytrNode *parent, TrytrNode *child);

TrytrProperty *trytr_ast_add_property(
    TrytrNode *node,
    TrytrArena *arena,
    const char *name,
    size_t name_length,
    TrytrSourceLocation location,
    int64_t value
);

#endif
