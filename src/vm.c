#include "vm.h"

#include "limits.h"

#include <math.h>

static TrytrElementKind public_kind(TrytrIROpcode opcode)
{
    switch (opcode) {
        case TRYTR_IR_HEADING: return TRYTR_ELEMENT_HEADING;
        case TRYTR_IR_TEXT: return TRYTR_ELEMENT_TEXT;
        case TRYTR_IR_MATH: return TRYTR_ELEMENT_MATH;
        case TRYTR_IR_RECT: return TRYTR_ELEMENT_RECT;
        case TRYTR_IR_LINE: return TRYTR_ELEMENT_LINE;
        case TRYTR_IR_CIRCLE: return TRYTR_ELEMENT_CIRCLE;
        case TRYTR_IR_PATH: return TRYTR_ELEMENT_PATH;
        case TRYTR_IR_GROUP_BEGIN: return TRYTR_ELEMENT_GROUP_BEGIN;
        case TRYTR_IR_GROUP_END: return TRYTR_ELEMENT_GROUP_END;
        case TRYTR_IR_ELLIPSE: return TRYTR_ELEMENT_ELLIPSE;
        case TRYTR_IR_POLYGON: return TRYTR_ELEMENT_POLYGON;
        case TRYTR_IR_ROUNDRECT: return TRYTR_ELEMENT_ROUNDRECT;
        case TRYTR_IR_PLOT: return TRYTR_ELEMENT_PLOT;
    }
    return TRYTR_ELEMENT_TEXT;
}

TrytrStatus trytr_vm_execute(
    const TrytrIRProgram *program,
    TrytrElementVisitor visitor,
    void *user_data
)
{
    if (program == NULL || visitor == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;
    if (program->instruction_count > TRYTR_DEFAULT_IR_INSTRUCTION_LIMIT ||
        program->instruction_count > TRYTR_DEFAULT_VM_FUEL ||
        program->execution_units > TRYTR_DEFAULT_VM_FUEL ||
        program->text_bytes > TRYTR_DEFAULT_EXECUTION_TEXT_LIMIT ||
        (program->instruction_count != 0u && program->instructions == NULL)) {
        return TRYTR_STATUS_LIMIT_EXCEEDED;
    }

    size_t group_depth = 0u;
    for (size_t index = 0u; index < program->instruction_count; ++index) {
        const TrytrIROpcode opcode = program->instructions[index].opcode;
        if (opcode == TRYTR_IR_GROUP_BEGIN) {
            if (group_depth >= TRYTR_DEFAULT_DEPTH_LIMIT) return TRYTR_STATUS_LIMIT_EXCEEDED;
            ++group_depth;
        } else if (opcode == TRYTR_IR_GROUP_END) {
            if (group_depth == 0u) return TRYTR_STATUS_INVALID_DOCUMENT;
            --group_depth;
        }
    }
    if (group_depth != 0u) return TRYTR_STATUS_INVALID_DOCUMENT;

    size_t fuel = TRYTR_DEFAULT_VM_FUEL;
    for (size_t index = 0u; index < program->instruction_count; ++index) {
        if (fuel == 0u) return TRYTR_STATUS_LIMIT_EXCEEDED;
        --fuel;

        const TrytrIRInstruction *instruction = &program->instructions[index];
        TrytrDocumentElement element = {
            .kind = public_kind(instruction->opcode),
            .element_index = instruction->element_index,
            .id = instruction->stable_id,
            .math_tree = instruction->math_tree,
            .inline_runs = instruction->inline_runs,
            .location = instruction->location,
            .has_fill = instruction->has_fill,
            .fill_rgb = instruction->fill_rgb,
            .has_stroke = instruction->has_stroke,
            .stroke_rgb = instruction->stroke_rgb,
            .has_stroke_width = instruction->has_stroke_width,
            .stroke_width = instruction->stroke_width,
            .has_font_size = instruction->has_font_size,
            .font_size = instruction->font_size,
            .has_text_color = instruction->has_text_color,
            .text_color_rgb = instruction->text_color_rgb,
            .has_text_alignment = instruction->has_text_alignment,
            .text_alignment = (TrytrTextAlignment)instruction->text_alignment,
            .has_text_width = instruction->has_text_width,
            .text_width = instruction->text_width,
            .plot_grid = instruction->opcode == TRYTR_IR_PLOT
                ? instruction->operands.plot.grid : false,
            .plot_markers = instruction->opcode == TRYTR_IR_PLOT
                ? instruction->operands.plot.markers : false
        };
        switch (instruction->opcode) {
            case TRYTR_IR_HEADING:
            case TRYTR_IR_TEXT:
            case TRYTR_IR_MATH:
                element.text = instruction->operands.text.bytes;
                element.text_length = instruction->operands.text.length;
                break;
            case TRYTR_IR_PATH:
                if (instruction->operands.path.segment_count > fuel) {
                    return TRYTR_STATUS_LIMIT_EXCEEDED;
                }
                fuel -= instruction->operands.path.segment_count;
                element.text = instruction->operands.path.data;
                element.text_length = instruction->operands.path.length;
                element.min_x = instruction->operands.path.min_x;
                element.min_y = instruction->operands.path.min_y;
                element.max_x = instruction->operands.path.max_x;
                element.max_y = instruction->operands.path.max_y;
                break;
            case TRYTR_IR_RECT:
                element.x = instruction->operands.rect.x;
                element.y = instruction->operands.rect.y;
                element.width = instruction->operands.rect.width;
                element.height = instruction->operands.rect.height;
                break;
            case TRYTR_IR_ROUNDRECT:
                element.x = instruction->operands.roundrect.x;
                element.y = instruction->operands.roundrect.y;
                element.width = instruction->operands.roundrect.width;
                element.height = instruction->operands.roundrect.height;
                element.radius = instruction->operands.roundrect.radius;
                break;
            case TRYTR_IR_LINE:
                element.x = instruction->operands.line.x1;
                element.y = instruction->operands.line.y1;
                element.x2 = instruction->operands.line.x2;
                element.y2 = instruction->operands.line.y2;
                break;
            case TRYTR_IR_CIRCLE:
                element.x = instruction->operands.circle.cx;
                element.y = instruction->operands.circle.cy;
                element.radius = instruction->operands.circle.radius;
                break;
            case TRYTR_IR_GROUP_BEGIN:
                element.x = instruction->operands.group.translate_x;
                element.y = instruction->operands.group.translate_y;
                break;
            case TRYTR_IR_GROUP_END:
                break;
            case TRYTR_IR_ELLIPSE:
                element.x = instruction->operands.ellipse.cx;
                element.y = instruction->operands.ellipse.cy;
                element.radius_x = instruction->operands.ellipse.rx;
                element.radius_y = instruction->operands.ellipse.ry;
                break;
            case TRYTR_IR_POLYGON:
                if (instruction->operands.polygon.point_count > fuel) {
                    return TRYTR_STATUS_LIMIT_EXCEEDED;
                }
                fuel -= instruction->operands.polygon.point_count;
                element.text = instruction->operands.polygon.points;
                element.text_length = instruction->operands.polygon.length;
                element.min_x = instruction->operands.polygon.min_x;
                element.min_y = instruction->operands.polygon.min_y;
                element.max_x = instruction->operands.polygon.max_x;
                element.max_y = instruction->operands.polygon.max_y;
                break;
            case TRYTR_IR_PLOT:
                if (instruction->operands.plot.point_count > fuel) {
                    return TRYTR_STATUS_LIMIT_EXCEEDED;
                }
                fuel -= instruction->operands.plot.point_count;
                element.text = instruction->operands.plot.points;
                element.text_length = instruction->operands.plot.length;
                element.point_count = instruction->operands.plot.point_count;
                element.plot_min_x = instruction->operands.plot.min_x;
                element.plot_min_y = instruction->operands.plot.min_y;
                element.plot_max_x = instruction->operands.plot.max_x;
                element.plot_max_y = instruction->operands.plot.max_y;
                element.min_x = (int64_t)floor(instruction->operands.plot.min_x);
                element.min_y = (int64_t)floor(instruction->operands.plot.min_y);
                element.max_x = (int64_t)ceil(instruction->operands.plot.max_x);
                element.max_y = (int64_t)ceil(instruction->operands.plot.max_y);
                element.formula_plot = instruction->operands.plot.formula;
                element.x = instruction->operands.plot.x;
                element.y = instruction->operands.plot.y;
                element.width = instruction->operands.plot.width;
                element.height = instruction->operands.plot.height;
                break;
            default:
                return TRYTR_STATUS_INVALID_DOCUMENT;
        }
        if (!visitor(&element, user_data)) return TRYTR_STATUS_EXECUTION_FAILED;
    }
    return TRYTR_STATUS_OK;
}
