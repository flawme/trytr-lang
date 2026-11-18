#include "ir.h"

#include "limits.h"

#include <stdint.h>
#include <string.h>

static bool shape_property(const TrytrNode *node, const char *name, int64_t *value)
{
    for (const TrytrProperty *property = node->properties;
         property != NULL;
         property = property->next) {
        if (property->name != NULL && strcmp(property->name, name) == 0) {
            *value = property->integer_value;
            return true;
        }
    }
    return false;
}

static bool shape_boolean_property(const TrytrNode *node, const char *name,
                                   bool default_value)
{
    int64_t value = 0;
    return shape_property(node, name, &value) ? value != 0 : default_value;
}

static bool rect_operands(const TrytrNode *node, TrytrIRInstruction *instruction)
{
    return shape_property(node, "x", &instruction->operands.rect.x) &&
           shape_property(node, "y", &instruction->operands.rect.y) &&
           shape_property(node, "width", &instruction->operands.rect.width) &&
           shape_property(node, "height", &instruction->operands.rect.height);
}

static bool roundrect_operands(const TrytrNode *node, TrytrIRInstruction *instruction)
{
    return shape_property(node, "x", &instruction->operands.roundrect.x) &&
           shape_property(node, "y", &instruction->operands.roundrect.y) &&
           shape_property(node, "width", &instruction->operands.roundrect.width) &&
           shape_property(node, "height", &instruction->operands.roundrect.height) &&
           shape_property(node, "radius", &instruction->operands.roundrect.radius);
}

static bool line_operands(const TrytrNode *node, TrytrIRInstruction *instruction)
{
    return shape_property(node, "x1", &instruction->operands.line.x1) &&
           shape_property(node, "y1", &instruction->operands.line.y1) &&
           shape_property(node, "x2", &instruction->operands.line.x2) &&
           shape_property(node, "y2", &instruction->operands.line.y2);
}

static bool circle_operands(const TrytrNode *node, TrytrIRInstruction *instruction)
{
    return shape_property(node, "cx", &instruction->operands.circle.cx) &&
           shape_property(node, "cy", &instruction->operands.circle.cy) &&
           shape_property(node, "radius", &instruction->operands.circle.radius);
}

static bool ellipse_operands(const TrytrNode *node, TrytrIRInstruction *instruction)
{
    return shape_property(node, "cx", &instruction->operands.ellipse.cx) &&
           shape_property(node, "cy", &instruction->operands.ellipse.cy) &&
           shape_property(node, "rx", &instruction->operands.ellipse.rx) &&
           shape_property(node, "ry", &instruction->operands.ellipse.ry);
}

static TrytrStatus account_nodes(const TrytrNode *parent, size_t *instruction_count,
                                 size_t *text_bytes, size_t *execution_units)
{
    for (const TrytrNode *node = parent->children; node != NULL; node = node->next) {
        const size_t instruction_cost = node->kind == TRYTR_NODE_GROUP ? 2u : 1u;
        if (*instruction_count > TRYTR_DEFAULT_IR_INSTRUCTION_LIMIT - instruction_cost ||
            *execution_units > TRYTR_DEFAULT_VM_FUEL - instruction_cost) {
            return TRYTR_STATUS_LIMIT_EXCEEDED;
        }
        *instruction_count += instruction_cost;
        *execution_units += instruction_cost;

        if (node->kind == TRYTR_NODE_HEADING || node->kind == TRYTR_NODE_TEXT ||
            node->kind == TRYTR_NODE_MATH || node->kind == TRYTR_NODE_PATH ||
            node->kind == TRYTR_NODE_POLYGON || node->kind == TRYTR_NODE_PLOT) {
            if (node->text == NULL) return TRYTR_STATUS_INVALID_DOCUMENT;
            if (node->text_length > TRYTR_DEFAULT_EXECUTION_TEXT_LIMIT - *text_bytes) {
                return TRYTR_STATUS_LIMIT_EXCEEDED;
            }
            *text_bytes += node->text_length;
        }
        if (node->kind == TRYTR_NODE_PATH) {
            if (node->path_segment_count > TRYTR_DEFAULT_PATH_SEGMENT_LIMIT ||
                node->path_segment_count > TRYTR_DEFAULT_VM_FUEL - *execution_units) {
                return TRYTR_STATUS_LIMIT_EXCEEDED;
            }
            *execution_units += node->path_segment_count;
        }
        if (node->kind == TRYTR_NODE_POLYGON) {
            if (node->polygon_point_count > TRYTR_DEFAULT_POLYGON_POINT_LIMIT ||
                node->polygon_point_count > TRYTR_DEFAULT_VM_FUEL - *execution_units) {
                return TRYTR_STATUS_LIMIT_EXCEEDED;
            }
            *execution_units += node->polygon_point_count;
        }
        if (node->kind == TRYTR_NODE_PLOT) {
            if (node->plot_point_count > TRYTR_DEFAULT_PLOT_POINT_LIMIT ||
                node->plot_point_count > TRYTR_DEFAULT_VM_FUEL - *execution_units) {
                return TRYTR_STATUS_LIMIT_EXCEEDED;
            }
            *execution_units += node->plot_point_count;
        }
        if (node->kind == TRYTR_NODE_GROUP) {
            TrytrStatus status = account_nodes(node, instruction_count, text_bytes,
                                               execution_units);
            if (status != TRYTR_STATUS_OK) return status;
        }
    }
    return TRYTR_STATUS_OK;
}

static TrytrStatus append_node(const TrytrNode *node, TrytrIRInstruction *instructions,
                               size_t *instruction_index, size_t *node_index)
{
    const size_t current_node_index = (*node_index)++;
    if (node->kind == TRYTR_NODE_GROUP) {
        TrytrIRInstruction *begin = &instructions[(*instruction_index)++];
        *begin = (TrytrIRInstruction){
            .opcode = TRYTR_IR_GROUP_BEGIN,
            .element_index = current_node_index,
            .stable_id = node->id,
            .location = node->location,
            .operands.group = {
                .translate_x = node->group_translate_x,
                .translate_y = node->group_translate_y
            }
        };
        for (const TrytrNode *child = node->children; child != NULL; child = child->next) {
            TrytrStatus status = append_node(child, instructions, instruction_index, node_index);
            if (status != TRYTR_STATUS_OK) return status;
        }
        TrytrIRInstruction *end = &instructions[(*instruction_index)++];
        *end = (TrytrIRInstruction){
            .opcode = TRYTR_IR_GROUP_END,
            .element_index = current_node_index,
            .stable_id = node->id,
            .location = node->location
        };
        return TRYTR_STATUS_OK;
    }

    TrytrIRInstruction *instruction = &instructions[(*instruction_index)++];
    instruction->element_index = current_node_index;
    instruction->stable_id = node->id;
    instruction->inline_runs = node->inline_runs;
    instruction->location = node->location;
    int64_t color_value = 0;
    if (shape_property(node, "fill", &color_value)) {
        instruction->has_fill = true;
        instruction->fill_rgb = (uint32_t)color_value;
    }
    if (shape_property(node, "stroke", &color_value)) {
        instruction->has_stroke = true;
        instruction->stroke_rgb = (uint32_t)color_value;
    }
    if (shape_property(node, "stroke-width", &color_value)) {
        instruction->has_stroke_width = true;
        instruction->stroke_width = color_value;
    }
    if (shape_property(node, "font-size", &color_value)) {
        instruction->has_font_size = true;
        instruction->font_size = color_value;
    }
    if (shape_property(node, "color", &color_value)) {
        instruction->has_text_color = true;
        instruction->text_color_rgb = (uint32_t)color_value;
    }
    if (shape_property(node, "align", &color_value)) {
        instruction->has_text_alignment = true;
        instruction->text_alignment = (TrytrTextAlignment)color_value;
    }
    if (shape_property(node, "width", &color_value) &&
        (node->kind == TRYTR_NODE_HEADING || node->kind == TRYTR_NODE_TEXT)) {
        instruction->has_text_width = true;
        instruction->text_width = color_value;
    }
    switch (node->kind) {
        case TRYTR_NODE_HEADING:
            instruction->opcode = TRYTR_IR_HEADING;
            instruction->operands.text.bytes = node->text;
            instruction->operands.text.length = node->text_length;
            break;
        case TRYTR_NODE_TEXT:
            instruction->opcode = TRYTR_IR_TEXT;
            instruction->operands.text.bytes = node->text;
            instruction->operands.text.length = node->text_length;
            break;
        case TRYTR_NODE_MATH:
            instruction->opcode = TRYTR_IR_MATH;
            instruction->operands.text.bytes = node->text;
            instruction->operands.text.length = node->text_length;
            instruction->math_tree = node->math_tree;
            break;
        case TRYTR_NODE_PATH:
            instruction->opcode = TRYTR_IR_PATH;
            instruction->operands.path.data = node->text;
            instruction->operands.path.length = node->text_length;
            instruction->operands.path.segment_count = node->path_segment_count;
            instruction->operands.path.min_x = node->path_min_x;
            instruction->operands.path.min_y = node->path_min_y;
            instruction->operands.path.max_x = node->path_max_x;
            instruction->operands.path.max_y = node->path_max_y;
            break;
        case TRYTR_NODE_RECT:
            instruction->opcode = TRYTR_IR_RECT;
            if (!rect_operands(node, instruction)) return TRYTR_STATUS_INVALID_DOCUMENT;
            break;
        case TRYTR_NODE_ROUNDRECT:
            instruction->opcode = TRYTR_IR_ROUNDRECT;
            if (!roundrect_operands(node, instruction)) return TRYTR_STATUS_INVALID_DOCUMENT;
            break;
        case TRYTR_NODE_LINE:
            instruction->opcode = TRYTR_IR_LINE;
            if (!line_operands(node, instruction)) return TRYTR_STATUS_INVALID_DOCUMENT;
            break;
        case TRYTR_NODE_CIRCLE:
            instruction->opcode = TRYTR_IR_CIRCLE;
            if (!circle_operands(node, instruction)) return TRYTR_STATUS_INVALID_DOCUMENT;
            break;
        case TRYTR_NODE_ELLIPSE:
            instruction->opcode = TRYTR_IR_ELLIPSE;
            if (!ellipse_operands(node, instruction)) return TRYTR_STATUS_INVALID_DOCUMENT;
            break;
        case TRYTR_NODE_POLYGON:
            instruction->opcode = TRYTR_IR_POLYGON;
            instruction->operands.polygon.points = node->text;
            instruction->operands.polygon.length = node->text_length;
            instruction->operands.polygon.point_count = node->polygon_point_count;
            instruction->operands.polygon.min_x = node->polygon_min_x;
            instruction->operands.polygon.min_y = node->polygon_min_y;
            instruction->operands.polygon.max_x = node->polygon_max_x;
            instruction->operands.polygon.max_y = node->polygon_max_y;
            break;
        case TRYTR_NODE_PLOT:
            instruction->opcode = TRYTR_IR_PLOT;
            if (!shape_property(node, "x", &instruction->operands.plot.x) ||
                !shape_property(node, "y", &instruction->operands.plot.y) ||
                !shape_property(node, "width", &instruction->operands.plot.width) ||
                !shape_property(node, "height", &instruction->operands.plot.height)) {
                return TRYTR_STATUS_INVALID_DOCUMENT;
            }
            instruction->operands.plot.points = node->text;
            instruction->operands.plot.length = node->text_length;
            instruction->operands.plot.point_count = node->plot_point_count;
            instruction->operands.plot.min_x = node->plot_min_x;
            instruction->operands.plot.min_y = node->plot_min_y;
            instruction->operands.plot.max_x = node->plot_max_x;
            instruction->operands.plot.max_y = node->plot_max_y;
            instruction->operands.plot.grid = shape_boolean_property(node, "grid", true);
            instruction->operands.plot.markers = shape_boolean_property(node, "markers", true);
            instruction->operands.plot.formula = node->formula_plot;
            break;
        case TRYTR_NODE_DOCUMENT:
        case TRYTR_NODE_GROUP:
        default:
            return TRYTR_STATUS_INVALID_DOCUMENT;
    }
    return TRYTR_STATUS_OK;
}

TrytrStatus trytr_ir_build(
    const TrytrNode *root,
    TrytrArena *arena,
    TrytrIRProgram *out_program
)
{
    if (out_program == NULL || arena == NULL || root == NULL) {
        return TRYTR_STATUS_INVALID_ARGUMENT;
    }
    *out_program = (TrytrIRProgram){0};
    if (root->kind != TRYTR_NODE_DOCUMENT || root->version != 1) {
        return TRYTR_STATUS_INVALID_DOCUMENT;
    }

    size_t count = 0u;
    size_t text_bytes = 0u;
    size_t execution_units = 0u;
    TrytrStatus status = account_nodes(root, &count, &text_bytes, &execution_units);
    if (status != TRYTR_STATUS_OK) return status;

    TrytrIRInstruction *instructions = NULL;
    if (count != 0u) {
        instructions = trytr_arena_calloc(arena, count, sizeof(*instructions));
        if (instructions == NULL) return TRYTR_STATUS_NO_MEMORY;
    }

    size_t instruction_index = 0u;
    size_t node_index = 0u;
    for (const TrytrNode *node = root->children; node != NULL; node = node->next) {
        status = append_node(node, instructions, &instruction_index, &node_index);
        if (status != TRYTR_STATUS_OK) return status;
    }
    *out_program = (TrytrIRProgram){
        .instructions = instructions,
        .instruction_count = count,
        .text_bytes = text_bytes,
        .execution_units = execution_units
    };
    return TRYTR_STATUS_OK;
}
