#include "validator.h"
#include "limits.h"

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
    static const char *const line_names[] = {"x1", "y1", "x2", "y2"};
    static const char *const circle_names[] = {"cx", "cy", "radius"};
    static const char *const ellipse_names[] = {"cx", "cy", "rx", "ry"};
    if (kind == TRYTR_NODE_RECT) {
        *out_count = 4u;
        return rect_names;
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
                             (kind == TRYTR_NODE_CIRCLE && index == 2u) ||
                             (kind == TRYTR_NODE_ELLIPSE && index >= 2u);
            return true;
        }
    }
    return false;
}

static void validate_geometry(const TrytrNode *node, TrytrDiagnosticList *diagnostics)
{
    size_t property_count = 0u;
    const char *const *names = geometry_property_names(node->kind, &property_count);
    unsigned int seen = 0;
    for (const TrytrProperty *property = node->properties;
         property != NULL;
         property = property->next) {
        size_t index;
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
        } else if (node->kind == TRYTR_NODE_RECT || node->kind == TRYTR_NODE_LINE ||
                   node->kind == TRYTR_NODE_CIRCLE || node->kind == TRYTR_NODE_ELLIPSE) {
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

    validate_ids(root, diagnostics);
    validate_nodes(root, diagnostics);
}
