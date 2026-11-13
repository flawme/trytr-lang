#include "validator.h"
#include "limits.h"
#include "trytr/document.h"

#include <stdlib.h>
#include <string.h>

static void validation_error(
    TrytrDiagnosticList *diagnostics,
    const char *code,
    TrytrSourceLocation location,
    const char *message
)
{
    if (diagnostics == NULL || diagnostics->allocation_failed ||
        diagnostics->count >= TRYTR_DEFAULT_DIAGNOSTIC_LIMIT ||
        diagnostics->count >= diagnostics->limit) {
        return;
    }
    if (diagnostics->count == TRYTR_DEFAULT_DIAGNOSTIC_LIMIT - 1u) {
        (void)trytr_diagnostic_add(
            diagnostics,
            TRYTR_DIAGNOSTIC_ERROR,
            "TRYTR-E-LIMIT",
            location,
            "diagnostic limit exceeded"
        );
        return;
    }
    (void)trytr_diagnostic_add(
        diagnostics,
        TRYTR_DIAGNOSTIC_ERROR,
        code,
        location,
        "%s",
        message
    );
}

static const char *const *geometry_property_names(TrytrNodeKind kind, size_t *out_count)
{
    static const char *const rect_names[] = {"x", "y", "width", "height"};
    static const char *const roundrect_names[] = {"x", "y", "width", "height", "radius"};
    static const char *const line_names[] = {"x1", "y1", "x2", "y2"};
    static const char *const circle_names[] = {"cx", "cy", "radius"};
    static const char *const ellipse_names[] = {"cx", "cy", "rx", "ry"};
    static const char *const plot_names[] = {"x", "y", "width", "height"};
    if (kind == TRYTR_NODE_RECT) {
        *out_count = 4u;
        return rect_names;
    }
    if (kind == TRYTR_NODE_ROUNDRECT) {
        *out_count = 5u;
        return roundrect_names;
    }
    if (kind == TRYTR_NODE_LINE) {
        *out_count = 4u;
        return line_names;
    }
    if (kind == TRYTR_NODE_CIRCLE) {
        *out_count = 3u;
        return circle_names;
    }
    if (kind == TRYTR_NODE_ELLIPSE) {
        *out_count = 4u;
        return ellipse_names;
    }
    if (kind == TRYTR_NODE_PLOT) {
        *out_count = 4u;
        return plot_names;
    }
    *out_count = 0u;
    return NULL;
}

static bool geometry_property_index(TrytrNodeKind kind, const char *name,
                                    size_t *out_index, bool *out_dimension)
{
    size_t count = 0u;
    const char *const *names = geometry_property_names(kind, &count);
    for (size_t index = 0u; index < count; ++index) {
        if (strcmp(name, names[index]) == 0) {
            *out_index = index;
            *out_dimension = (kind == TRYTR_NODE_RECT && index >= 2u) ||
                             (kind == TRYTR_NODE_ROUNDRECT && index >= 2u) ||
                             (kind == TRYTR_NODE_CIRCLE && index == 2u) ||
                             (kind == TRYTR_NODE_ELLIPSE && index >= 2u) ||
                             (kind == TRYTR_NODE_PLOT && index >= 2u);
            return true;
        }
    }
    return false;
}

static bool style_property_index(TrytrNodeKind kind, const char *name,
                                 size_t *out_index)
{
    const bool supports_fill = kind == TRYTR_NODE_RECT ||
                               kind == TRYTR_NODE_ROUNDRECT ||
                               kind == TRYTR_NODE_CIRCLE ||
                               kind == TRYTR_NODE_ELLIPSE ||
                               kind == TRYTR_NODE_PATH ||
                               kind == TRYTR_NODE_POLYGON;
    const bool supports_stroke = supports_fill || kind == TRYTR_NODE_LINE ||
                                 kind == TRYTR_NODE_PLOT;
    if (strcmp(name, "fill") == 0 && supports_fill) {
        *out_index = 0u;
        return true;
    }
    if (strcmp(name, "stroke") == 0 &&
        supports_stroke) {
        *out_index = 1u;
        return true;
    }
    if (strcmp(name, "stroke-width") == 0 &&
        supports_stroke) {
        *out_index = 2u;
        return true;
    }
    return false;
}

static void validate_geometry(const TrytrNode *node, TrytrDiagnosticList *diagnostics)
{
    size_t property_count = 0u;
    const char *const *names = geometry_property_names(node->kind, &property_count);
    unsigned int seen = 0;
    unsigned int style_seen = 0;
    unsigned int plot_option_seen = 0;
    for (const TrytrProperty *property = node->properties;
         property != NULL;
         property = property->next) {
        size_t index;
        if (node->kind == TRYTR_NODE_PLOT &&
            (strcmp(property->name, "grid") == 0 ||
             strcmp(property->name, "markers") == 0)) {
            const unsigned int option_index =
                strcmp(property->name, "grid") == 0 ? 0u : 1u;
            const unsigned int flag = 1u << option_index;
            if ((plot_option_seen & flag) != 0u) {
                validation_error(diagnostics, "TRYTR-E-PROPERTY", property->location,
                                 "duplicate plot option");
            }
            plot_option_seen |= flag;
            if (property->integer_value != 0 && property->integer_value != 1) {
                validation_error(diagnostics, "TRYTR-E-PLOT-OPTION", property->location,
                                 "plot grid and markers options must be true or false");
            }
            continue;
        }
        if (strcmp(property->name, "fill") == 0 ||
            strcmp(property->name, "stroke") == 0 ||
            strcmp(property->name, "stroke-width") == 0) {
            if (!style_property_index(node->kind, property->name, &index)) {
                validation_error(diagnostics, "TRYTR-E-PROPERTY", property->location,
                                 "style property is not supported by this shape");
                continue;
            }
            const unsigned int flag = 1u << index;
            if ((style_seen & flag) != 0u) {
                validation_error(diagnostics, "TRYTR-E-PROPERTY", property->location,
                                 "duplicate shape style property");
            }
            style_seen |= flag;
            if (strcmp(property->name, "stroke-width") == 0) {
                if (property->integer_value < 0 ||
                    property->integer_value > TRYTR_DEFAULT_STROKE_WIDTH_LIMIT) {
                    validation_error(diagnostics, "TRYTR-E-STROKE-WIDTH",
                                     property->location,
                                     "stroke width must be between 0 and 32");
                }
            } else if (property->integer_value < 0 || property->integer_value > 0xFFFFFF) {
                validation_error(diagnostics, "TRYTR-E-COLOR", property->location,
                                 "RGB color is outside the 24-bit range");
            }
            continue;
        }
        bool is_dimension;
        if (!geometry_property_index(node->kind, property->name, &index, &is_dimension)) {
            validation_error(
                diagnostics,
                "TRYTR-E-PROPERTY",
                property->location,
                "unknown shape property"
            );
            continue;
        }

        unsigned int flag = 1u << index;
        if ((seen & flag) != 0) {
            validation_error(
                diagnostics,
                "TRYTR-E-PROPERTY",
                property->location,
                "duplicate shape property"
            );
        }
        seen |= flag;

        const int64_t value = property->integer_value;
        if ((is_dimension && (value < 0 || value > TRYTR_DEFAULT_COORDINATE_LIMIT)) ||
            (!is_dimension &&
             (value < -TRYTR_DEFAULT_COORDINATE_LIMIT ||
              value > TRYTR_DEFAULT_COORDINATE_LIMIT))) {
            validation_error(
                diagnostics,
                "TRYTR-E-COORDINATE",
                property->location,
                is_dimension
                    ? "shape dimensions must be between 0 and the coordinate limit"
                    : "shape position exceeds the coordinate limit"
            );
        }
    }

    for (size_t index = 0u; index < property_count; ++index) {
        if ((seen & (1u << index)) == 0) {
            validation_error(
                diagnostics,
                "TRYTR-E-REQUIRED",
                node->location,
                names[index]
            );
        }
    }

    if (node->kind == TRYTR_NODE_ROUNDRECT && seen == 0x1Fu) {
        int64_t width = 0;
        int64_t height = 0;
        int64_t radius = 0;
        for (const TrytrProperty *property = node->properties;
             property != NULL;
             property = property->next) {
            if (strcmp(property->name, "width") == 0) width = property->integer_value;
            else if (strcmp(property->name, "height") == 0) height = property->integer_value;
            else if (strcmp(property->name, "radius") == 0) radius = property->integer_value;
        }
        if (width >= 0 && height >= 0 && radius >= 0 &&
            (radius > width / 2 || radius > height / 2)) {
            validation_error(
                diagnostics,
                "TRYTR-E-COORDINATE",
                node->location,
                "rounded rectangle radius must not exceed half its width or height"
            );
        }
    }
    if (node->kind == TRYTR_NODE_PLOT && seen == 0x0Fu) {
        int64_t width = 0;
        int64_t height = 0;
        for (const TrytrProperty *property = node->properties;
             property != NULL;
             property = property->next) {
            if (strcmp(property->name, "width") == 0) width = property->integer_value;
            else if (strcmp(property->name, "height") == 0) height = property->integer_value;
        }
        if (width == 0 || height == 0) {
            validation_error(diagnostics, "TRYTR-E-PLOT-SIZE", node->location,
                             "plot width and height must be greater than zero");
        }
    }
}

static void validate_text_options(const TrytrNode *node,
                                  TrytrDiagnosticList *diagnostics)
{
    unsigned int seen = 0u;
    for (const TrytrProperty *property = node->properties;
         property != NULL;
         property = property->next) {
        size_t index;
        if (strcmp(property->name, "font-size") == 0) index = 0u;
        else if (strcmp(property->name, "color") == 0) index = 1u;
        else if (strcmp(property->name, "align") == 0) index = 2u;
        else if (strcmp(property->name, "width") == 0 &&
                 node->kind != TRYTR_NODE_MATH) index = 3u;
        else {
            validation_error(diagnostics, "TRYTR-E-PROPERTY", property->location,
                             "unknown text option");
            continue;
        }
        const unsigned int flag = 1u << index;
        if ((seen & flag) != 0u) {
            validation_error(diagnostics, "TRYTR-E-PROPERTY", property->location,
                             "duplicate text option");
        }
        seen |= flag;

        if (index == 0u &&
            (property->integer_value < TRYTR_DEFAULT_FONT_SIZE_MIN ||
             property->integer_value > TRYTR_DEFAULT_FONT_SIZE_MAX)) {
            validation_error(diagnostics, "TRYTR-E-FONT-SIZE", property->location,
                             "font size must be between 1 and 256");
        } else if (index == 1u &&
                   (property->integer_value < 0 ||
                    property->integer_value > 0xFFFFFF)) {
            validation_error(diagnostics, "TRYTR-E-COLOR", property->location,
                             "RGB color is outside the 24-bit range");
        } else if (index == 2u &&
                   (property->integer_value < TRYTR_TEXT_ALIGN_LEFT ||
                    property->integer_value > TRYTR_TEXT_ALIGN_RIGHT)) {
            validation_error(diagnostics, "TRYTR-E-TEXT-ALIGN", property->location,
                             "invalid text alignment value");
        } else if (index == 3u &&
                   (property->integer_value < 1 ||
                    property->integer_value > TRYTR_DEFAULT_COORDINATE_LIMIT)) {
            validation_error(diagnostics, "TRYTR-E-TEXT-WIDTH", property->location,
                             "text width must be between 1 and the coordinate limit");
        }
    }
}

typedef struct TrytrIdEntry {
    const TrytrNode *node;
} TrytrIdEntry;

static int compare_id_entries(const void *left_pointer, const void *right_pointer)
{
    const TrytrIdEntry *left = left_pointer;
    const TrytrIdEntry *right = right_pointer;
    return strcmp(left->node->id, right->node->id);
}

static void collect_ids(const TrytrNode *parent, TrytrIdEntry *entries, size_t *count)
{
    for (const TrytrNode *node = parent->children; node != NULL; node = node->next) {
        if (node->id != NULL && *count < TRYTR_DEFAULT_NODE_LIMIT) {
            entries[(*count)++] = (TrytrIdEntry){node};
        }
        collect_ids(node, entries, count);
    }
}

static void validate_ids(const TrytrNode *root, TrytrDiagnosticList *diagnostics)
{
    TrytrIdEntry entries[TRYTR_DEFAULT_NODE_LIMIT];
    size_t count = 0u;
    collect_ids(root, entries, &count);
    qsort(entries, count, sizeof(entries[0]), compare_id_entries);
    for (size_t index = 1u; index < count; ++index) {
        if (strcmp(entries[index - 1u].node->id, entries[index].node->id) == 0) {
            validation_error(
                diagnostics,
                "TRYTR-E-DUPLICATE-ID",
                entries[index].node->location,
                "duplicate stable element identifier"
            );
        }
    }
}

static void validate_nodes(const TrytrNode *parent, TrytrDiagnosticList *diagnostics)
{
    for (const TrytrNode *node = parent->children; node != NULL; node = node->next) {
        if (node->kind == TRYTR_NODE_GROUP) {
            if (node->group_translate_x < -TRYTR_DEFAULT_COORDINATE_LIMIT ||
                node->group_translate_x > TRYTR_DEFAULT_COORDINATE_LIMIT ||
                node->group_translate_y < -TRYTR_DEFAULT_COORDINATE_LIMIT ||
                node->group_translate_y > TRYTR_DEFAULT_COORDINATE_LIMIT) {
                validation_error(
                    diagnostics,
                    "TRYTR-E-COORDINATE",
                    node->location,
                    "group translation exceeds the coordinate limit"
                );
            }
        } else if (node->kind == TRYTR_NODE_HEADING || node->kind == TRYTR_NODE_TEXT ||
                   node->kind == TRYTR_NODE_MATH) {
            validate_text_options(node, diagnostics);
        } else if (node->kind == TRYTR_NODE_RECT || node->kind == TRYTR_NODE_ROUNDRECT ||
                   node->kind == TRYTR_NODE_LINE || node->kind == TRYTR_NODE_CIRCLE ||
                   node->kind == TRYTR_NODE_ELLIPSE || node->kind == TRYTR_NODE_PATH ||
                   node->kind == TRYTR_NODE_POLYGON || node->kind == TRYTR_NODE_PLOT) {
            validate_geometry(node, diagnostics);
        }
        validate_nodes(node, diagnostics);
    }
}

void trytr_validator_validate(
    const TrytrNode *root,
    TrytrDiagnosticList *diagnostics
)
{
    if (root == NULL) return;

    if (root->kind != TRYTR_NODE_DOCUMENT) {
        validation_error(
            diagnostics,
            "TRYTR-E-TYPE",
            root->location,
            "root AST node is not a document"
        );
        return;
    }
    if (root->version != 1) {
        validation_error(
            diagnostics,
            "TRYTR-E-VERSION",
            root->location,
            "unsupported document version; expected version 1"
        );
    }
    if (root->has_canvas_width != root->has_canvas_height) {
        validation_error(
            diagnostics,
            "TRYTR-E-CANVAS-SIZE",
            root->location,
            "a fixed canvas requires both width and height"
        );
    }
    if (root->has_canvas_width && root->has_canvas_height) {
        if (root->canvas_width < 1 ||
            root->canvas_width > TRYTR_DEFAULT_COORDINATE_LIMIT) {
            validation_error(
                diagnostics,
                "TRYTR-E-CANVAS-SIZE",
                root->location,
                "canvas width must be between 1 and the coordinate limit"
            );
        }
        if (root->canvas_height < 1 ||
            root->canvas_height > TRYTR_DEFAULT_COORDINATE_LIMIT) {
            validation_error(
                diagnostics,
                "TRYTR-E-CANVAS-SIZE",
                root->location,
                "canvas height must be between 1 and the coordinate limit"
            );
        }
    }

    validate_ids(root, diagnostics);
    validate_nodes(root, diagnostics);
}
