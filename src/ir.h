#ifndef TRYTR_SRC_IR_H
#define TRYTR_SRC_IR_H

#include "arena.h"
#include "ast.h"
#include "trytr/document.h"

typedef enum TrytrIROpcode {
    TRYTR_IR_HEADING = 0,
    TRYTR_IR_TEXT,
    TRYTR_IR_MATH,
    TRYTR_IR_RECT,
    TRYTR_IR_LINE,
    TRYTR_IR_CIRCLE,
    TRYTR_IR_PATH,
    TRYTR_IR_GROUP_BEGIN,
    TRYTR_IR_GROUP_END,
    TRYTR_IR_ELLIPSE,
    TRYTR_IR_POLYGON,
    TRYTR_IR_ROUNDRECT,
    TRYTR_IR_PLOT
} TrytrIROpcode;

typedef struct TrytrIRInstruction {
    TrytrIROpcode opcode;
    size_t element_index;
    const char *stable_id;
    const TrytrMathNode *math_tree;
    const TrytrInlineRun *inline_runs;
    TrytrSourceLocation location;
    bool has_fill;
    uint32_t fill_rgb;
    bool has_stroke;
    uint32_t stroke_rgb;
    bool has_stroke_width;
    int64_t stroke_width;
    bool has_font_size;
    int64_t font_size;
    bool has_text_color;
    uint32_t text_color_rgb;
    bool has_text_alignment;
    TrytrTextAlignment text_alignment;
    bool has_text_width;
    int64_t text_width;
    union {
        struct {
            const char *bytes;
            size_t length;
        } text;
        struct {
            int64_t x;
            int64_t y;
            int64_t width;
            int64_t height;
        } rect;
        struct {
            int64_t x;
            int64_t y;
            int64_t width;
            int64_t height;
            int64_t radius;
        } roundrect;
        struct {
            int64_t x1;
            int64_t y1;
            int64_t x2;
            int64_t y2;
        } line;
        struct {
            int64_t cx;
            int64_t cy;
            int64_t radius;
        } circle;
        struct {
            int64_t cx;
            int64_t cy;
            int64_t rx;
            int64_t ry;
        } ellipse;
        struct {
            const char *points;
            size_t length;
            size_t point_count;
            int64_t min_x;
            int64_t min_y;
            int64_t max_x;
            int64_t max_y;
        } polygon;
        struct {
            const char *points;
            size_t length;
            size_t point_count;
            double min_x;
            double min_y;
            double max_x;
            double max_y;
            int64_t x;
            int64_t y;
            int64_t width;
            int64_t height;
            bool grid;
            bool markers;
            bool formula;
        } plot;
        struct {
            const char *data;
            size_t length;
            size_t segment_count;
            int64_t min_x;
            int64_t min_y;
            int64_t max_x;
            int64_t max_y;
        } path;
        struct {
            int64_t translate_x;
            int64_t translate_y;
        } group;
    } operands;
} TrytrIRInstruction;

typedef struct TrytrIRProgram {
    const TrytrIRInstruction *instructions;
    size_t instruction_count;
    size_t text_bytes;
    size_t execution_units;
} TrytrIRProgram;

TrytrStatus trytr_ir_build(
    const TrytrNode *root,
    TrytrArena *arena,
    TrytrIRProgram *out_program
);

#endif
