#include "ast.h"

#include <string.h>

TrytrNode *trytr_ast_node_new(
    TrytrArena *arena,
    size_t *node_count,
    size_t node_limit,
    TrytrNodeKind kind,
    TrytrSourceLocation location
)
{
    if (arena == NULL || node_count == NULL || *node_count >= node_limit) return NULL;

    TrytrNode *node = trytr_arena_calloc(arena, 1u, sizeof(*node));
    if (node == NULL) return NULL;

    node->kind = kind;
    node->location = location;
    ++*node_count;
    return node;
}

void trytr_ast_add_child(TrytrNode *parent, TrytrNode *child)
{
    if (parent == NULL || child == NULL) return;
    if (parent->children_tail == NULL) {
        parent->children = child;
    } else {
        parent->children_tail->next = child;
    }
    parent->children_tail = child;
}

TrytrProperty *trytr_ast_add_property(
    TrytrNode *node,
    TrytrArena *arena,
    const char *name,
    size_t name_length,
    TrytrSourceLocation location,
    int64_t value
)
{
    if (node == NULL || arena == NULL || name == NULL) return NULL;

    TrytrProperty *property = trytr_arena_calloc(arena, 1u, sizeof(*property));
    if (property == NULL) return NULL;
    property->name = trytr_arena_copy_string(arena, name, name_length);
    if (property->name == NULL) return NULL;
    property->integer_value = value;
    property->location = location;

    if (node->properties_tail == NULL) {
        node->properties = property;
    } else {
        node->properties_tail->next = property;
    }
    node->properties_tail = property;
    return property;
}
