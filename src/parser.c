#include "parser.h"
#include "lexer.h"
#include "path.h"
#include "plot.h"
#include "plot_formula.h"
#include "polygon.h"
#include "math/notation_internal.h"
#include "trytr/document.h"
#include "trytr/math/expression.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct TrytrParser {
    TrytrLexer lexer;
    TrytrParserOutput *output;
    TrytrToken current;
    size_t node_count;
    size_t expression_operations;
    size_t group_depth;
    const TrytrParameterOverride *overrides;
    size_t override_count;
    bool parameter_bounds_expression;
    bool content_started;
    bool aborted;
} TrytrParser;

static void parse_element(TrytrParser *parser, TrytrNode *parent);
static void parse_element_list(TrytrParser *parser, TrytrNode *parent);
static void error(TrytrParser *parser, const char *code,
                  TrytrSourceLocation location, const char *message);

static bool build_math_tree(TrytrParser *parser, TrytrNode *node)
{
    const TrytrMathNotationStatus status = trytr_math_notation_parse(
        &parser->output->arena, node->text, node->text_length, &node->math_tree);
    if (status == TRYTR_MATH_NOTATION_OK) return true;
    if (status == TRYTR_MATH_NOTATION_NO_MEMORY) {
        parser->output->out_of_memory = true;
        error(parser, "TRYTR-E-MEMORY", node->location,
              "unable to build mathematical notation tree");
    } else {
        error(parser, "TRYTR-E-LIMIT", node->location,
              "mathematical notation tree exceeds its node limit");
    }
    parser->aborted = true;
    return false;
}

static bool identifier_is(TrytrToken token, const char *name)
{
    return token.kind == TRYTR_TOKEN_IDENTIFIER && token.length == strlen(name) &&
           memcmp(token.lexeme, name, token.length) == 0;
}

static TrytrParameterEntry *find_parameter(TrytrParser *parser,
                                           const char *id, size_t id_length)
{
    for (TrytrParameterEntry *entry = parser->output->parameters;
         entry != NULL; entry = entry->next) {
        if (strlen(entry->parameter.id) == id_length &&
            memcmp(entry->parameter.id, id, id_length) == 0) return entry;
    }
    return NULL;
}

static void error(TrytrParser *parser, const char *code, TrytrSourceLocation location, const char *message)
{
    TrytrDiagnosticList *diagnostics = &parser->output->diagnostics;
    if (diagnostics->count >= TRYTR_DEFAULT_DIAGNOSTIC_LIMIT - 1u) {
        if (!trytr_diagnostic_add(diagnostics, TRYTR_DIAGNOSTIC_ERROR, "TRYTR-E-LIMIT",
                                  location, "diagnostic limit exceeded")) {
            parser->output->out_of_memory = true;
        }
        parser->aborted = true;
        return;
    }
    if (!trytr_diagnostic_add(diagnostics, TRYTR_DIAGNOSTIC_ERROR,
                              code, location, "%s", message)) {
        parser->output->out_of_memory = true;
        parser->aborted = true;
    }
}

static void advance(TrytrParser *parser)
{
    if (!parser->aborted) parser->current = trytr_lexer_next(&parser->lexer);
    if (parser->output->diagnostics.count >= TRYTR_DEFAULT_DIAGNOSTIC_LIMIT - 1u &&
        !parser->aborted) {
        error(parser, "TRYTR-E-LIMIT", parser->current.location,
              "diagnostic limit exceeded");
    }
}

static TrytrNode *node_new(TrytrParser *parser, TrytrNodeKind kind, TrytrSourceLocation location)
{
    if (parser->node_count >= TRYTR_DEFAULT_NODE_LIMIT) {
        error(parser, "TRYTR-E-LIMIT", location, "document element limit exceeded");
        parser->aborted = true;
        return NULL;
    }
    TrytrNode *node = trytr_ast_node_new(&parser->output->arena, &parser->node_count,
                                         TRYTR_DEFAULT_NODE_LIMIT, kind, location);
    if (node == NULL) {
        error(parser, "TRYTR-E-MEMORY", location, "unable to allocate document node");
        parser->aborted = true;
    }
    return node;
}

static char *copy_token(TrytrParser *parser, TrytrToken token)
{
    char *copy = trytr_arena_copy_string(&parser->output->arena, token.lexeme, token.length);
    if (copy == NULL) {
        error(parser, "TRYTR-E-MEMORY", token.location, "unable to allocate identifier");
        parser->aborted = true;
    }
    return copy;
}

static bool parameter_identifier_start(char value)
{
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') || value == '_';
}

static bool parameter_identifier_continue(char value)
{
    return parameter_identifier_start(value) || (value >= '0' && value <= '9') ||
           value == '-';
}

static bool expand_parameter_references(
    TrytrParser *parser,
    const char *source,
    size_t source_length,
    size_t output_limit,
    TrytrSourceLocation location,
    char **out_text,
    size_t *out_length
)
{
    if (out_text == NULL || out_length == NULL || (source == NULL && source_length != 0u)) {
        return false;
    }
    size_t expanded_length = 0u;
    for (size_t index = 0u; index < source_length;) {
        size_t consumed = 1u;
        size_t replacement_length = 1u;
        char number[32];
        if (source[index] == '#' && index + 1u < source_length &&
            parameter_identifier_start(source[index + 1u]) &&
            (index == 0u || source[index - 1u] != '\\')) {
            size_t end = index + 2u;
            while (end < source_length && parameter_identifier_continue(source[end])) ++end;
            TrytrParameterEntry *entry = find_parameter(parser, source + index + 1u,
                                                        end - index - 1u);
            if (entry == NULL) {
                error(parser, "TRYTR-E-PARAMETER", location,
                      "unknown parameter reference; declare it before use");
                return false;
            }
            const int count = snprintf(number, sizeof(number), "%" PRId64,
                                       entry->parameter.value);
            if (count < 0 || (size_t)count >= sizeof(number)) {
                error(parser, "TRYTR-E-PARAMETER", location,
                      "parameter value could not be formatted");
                return false;
            }
            consumed = end - index;
            replacement_length = (size_t)count;
        }
        if (replacement_length > output_limit - expanded_length) {
            error(parser, "TRYTR-E-LIMIT", location,
                  "parameter expansion exceeds the document text limit");
            return false;
        }
        expanded_length += replacement_length;
        index += consumed;
    }

    char *expanded = trytr_arena_alloc(&parser->output->arena, expanded_length + 1u);
    if (expanded == NULL) {
        error(parser, "TRYTR-E-MEMORY", location,
              "unable to expand parameter references");
        parser->aborted = true;
        return false;
    }
    size_t source_index = 0u;
    size_t target_index = 0u;
    while (source_index < source_length) {
        if (source[source_index] == '#' && source_index + 1u < source_length &&
            parameter_identifier_start(source[source_index + 1u]) &&
            (source_index == 0u || source[source_index - 1u] != '\\')) {
            size_t end = source_index + 2u;
            while (end < source_length && parameter_identifier_continue(source[end])) ++end;
            TrytrParameterEntry *entry = find_parameter(parser, source + source_index + 1u,
                                                        end - source_index - 1u);
            char number[32];
            const int count = snprintf(number, sizeof(number), "%" PRId64,
                                       entry->parameter.value);
            const size_t count_size = (size_t)count;
            memcpy(expanded + target_index, number, count_size);
            target_index += count_size;
            source_index = end;
        } else {
            expanded[target_index++] = source[source_index++];
        }
    }
    expanded[target_index] = '\0';
    *out_text = expanded;
    *out_length = target_index;
    return true;
}

static void skip_element(TrytrParser *parser)
{
    size_t braces = 0, parentheses = 0;
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_EOF) {
        TrytrTokenKind kind = parser->current.kind;
        if (braces == 0 && parentheses == 0 &&
            (kind == TRYTR_TOKEN_AT || kind == TRYTR_TOKEN_RBRACE)) return;
        if (kind == TRYTR_TOKEN_LBRACE) ++braces;
        else if (kind == TRYTR_TOKEN_RBRACE && braces != 0) --braces;
        else if (kind == TRYTR_TOKEN_LPAREN) ++parentheses;
        else if (kind == TRYTR_TOKEN_RPAREN && parentheses != 0) --parentheses;
        advance(parser);
    }
}

static bool parse_expression(TrytrParser *parser, unsigned int min_precedence,
                             size_t depth, int64_t *out_value);

static bool expression_operation(TrytrParser *parser, TrytrSourceLocation location)
{
    if (parser->expression_operations >= TRYTR_DEFAULT_EXPRESSION_OPERATION_LIMIT) {
        error(parser, "TRYTR-E-LIMIT", location, "expression operation limit exceeded");
        parser->aborted = true;
        return false;
    }
    ++parser->expression_operations;
    return true;
}

static bool add_int64(int64_t left, int64_t right, int64_t *out)
{
    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)) return false;
    *out = left + right;
    return true;
}

static bool subtract_int64(int64_t left, int64_t right, int64_t *out)
{
    if ((right < 0 && left > INT64_MAX + right) ||
        (right > 0 && left < INT64_MIN + right)) return false;
    *out = left - right;
    return true;
}

static bool multiply_int64(int64_t left, int64_t right, int64_t *out)
{
    if (left == 0 || right == 0) {
        *out = 0;
        return true;
    }
    if ((left == -1 && right == INT64_MIN) ||
        (right == -1 && left == INT64_MIN)) return false;
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) ||
            (right < 0 && right < INT64_MIN / left)) return false;
    } else if ((right > 0 && left < INT64_MIN / right) ||
               (right < 0 && left < INT64_MAX / right)) {
        return false;
    }
    *out = left * right;
    return true;
}

static bool parse_expression_primary(TrytrParser *parser, size_t depth, int64_t *out_value)
{
    if (depth > TRYTR_DEFAULT_EXPRESSION_DEPTH_LIMIT) {
        error(parser, "TRYTR-E-LIMIT", parser->current.location,
              "expression nesting limit exceeded");
        parser->aborted = true;
        return false;
    }
    if (parser->current.kind == TRYTR_TOKEN_PLUS ||
        parser->current.kind == TRYTR_TOKEN_MINUS) {
        TrytrToken sign = parser->current;
        if (!expression_operation(parser, sign.location)) return false;
        advance(parser);
        if (sign.kind == TRYTR_TOKEN_MINUS && parser->current.kind == TRYTR_TOKEN_INTEGER &&
            parser->current.integer_value == (uint64_t)INT64_MAX + 1u) {
            *out_value = INT64_MIN;
            advance(parser);
            return true;
        }
        int64_t value;
        if (!parse_expression_primary(parser, depth + 1u, &value)) return false;
        if (sign.kind == TRYTR_TOKEN_MINUS) {
            if (value == INT64_MIN) {
                error(parser, "TRYTR-E-OVERFLOW", sign.location,
                      "integer negation overflows signed 64-bit range");
                return false;
            }
            value = -value;
        }
        *out_value = value;
        return true;
    }
    if (parser->current.kind == TRYTR_TOKEN_INTEGER) {
        const uint64_t magnitude = parser->current.integer_value;
        const TrytrSourceLocation location = parser->current.location;
        if (magnitude > (uint64_t)INT64_MAX) {
            error(parser, "TRYTR-E-INTEGER", location,
                  "integer is outside the signed 64-bit range");
            advance(parser);
            return false;
        }
        *out_value = (int64_t)magnitude;
        advance(parser);
        return true;
    }
    if (parser->current.kind == TRYTR_TOKEN_HASH) {
        const TrytrSourceLocation location = parser->current.location;
        advance(parser);
        if (parser->current.kind != TRYTR_TOKEN_IDENTIFIER) {
            error(parser, "TRYTR-E-PARAMETER", location,
                  "expected a parameter name after '#'");
            return false;
        }
        TrytrToken name = parser->current;
        TrytrParameterEntry *entry = find_parameter(parser, name.lexeme, name.length);
        if (entry == NULL) {
            error(parser, "TRYTR-E-PARAMETER", location,
                  "unknown parameter reference; declare it before use");
            advance(parser);
            return false;
        }
        if (parser->parameter_bounds_expression) {
            error(parser, "TRYTR-E-PARAMETER", location,
                  "parameter bounds and defaults cannot reference other parameters");
            advance(parser);
            return false;
        }
        *out_value = entry->parameter.value;
        advance(parser);
        return true;
    }
    if (parser->current.kind == TRYTR_TOKEN_LPAREN) {
        const TrytrSourceLocation location = parser->current.location;
        advance(parser);
        if (!parse_expression(parser, 0u, depth + 1u, out_value)) return false;
        if (parser->current.kind != TRYTR_TOKEN_RPAREN) {
            error(parser, "TRYTR-E-EXPR", location, "expected ')' in integer expression");
            return false;
        }
        advance(parser);
        return true;
    }

    const TrytrSourceLocation location = parser->current.location;
    error(parser, "TRYTR-E-EXPR", location, "expected an integer expression");
    if (parser->current.kind != TRYTR_TOKEN_EOF && parser->current.kind != TRYTR_TOKEN_COMMA &&
        parser->current.kind != TRYTR_TOKEN_RPAREN && parser->current.kind != TRYTR_TOKEN_RBRACE &&
        parser->current.kind != TRYTR_TOKEN_IDENTIFIER && parser->current.kind != TRYTR_TOKEN_AT) {
        advance(parser);
    }
    return false;
}

static unsigned int expression_precedence(TrytrTokenKind kind)
{
    if (kind == TRYTR_TOKEN_PLUS || kind == TRYTR_TOKEN_MINUS) return 1u;
    if (kind == TRYTR_TOKEN_STAR || kind == TRYTR_TOKEN_SLASH) return 2u;
    return 0u;
}

static bool parse_expression(TrytrParser *parser, unsigned int min_precedence,
                             size_t depth, int64_t *out_value)
{
    int64_t left;
    if (!parse_expression_primary(parser, depth, &left)) return false;

    for (;;) {
        const TrytrTokenKind operator_kind = parser->current.kind;
        const unsigned int precedence = expression_precedence(operator_kind);
        if (precedence == 0u || precedence < min_precedence) break;
        const TrytrSourceLocation operator_location = parser->current.location;
        if (!expression_operation(parser, operator_location)) return false;
        advance(parser);
        int64_t right;
        if (!parse_expression(parser, precedence + 1u, depth, &right)) return false;

        int64_t result = 0;
        bool valid = false;
        if (operator_kind == TRYTR_TOKEN_PLUS) valid = add_int64(left, right, &result);
        else if (operator_kind == TRYTR_TOKEN_MINUS) valid = subtract_int64(left, right, &result);
        else if (operator_kind == TRYTR_TOKEN_STAR) valid = multiply_int64(left, right, &result);
        else if (right == 0) {
            error(parser, "TRYTR-E-EXPR", operator_location,
                  "division by zero in integer expression");
            return false;
        } else if (left == INT64_MIN && right == -1) {
            error(parser, "TRYTR-E-OVERFLOW", operator_location,
                  "integer division overflows signed 64-bit range");
            return false;
        } else {
            result = left / right;
            valid = true;
        }
        if (!valid) {
            error(parser, "TRYTR-E-OVERFLOW", operator_location,
                  "integer expression overflows signed 64-bit range");
            return false;
        }
        left = result;
    }
    *out_value = left;
    return true;
}

static bool add_property(TrytrParser *parser, TrytrNode *node, const char *name,
                         TrytrSourceLocation location, int64_t value)
{
    if (trytr_ast_add_property(node, &parser->output->arena, name, strlen(name), location, value) == NULL) {
        error(parser, "TRYTR-E-MEMORY", location, "unable to allocate property");
        parser->aborted = true;
        return false;
    }
    return true;
}

static void skip_positional(TrytrParser *parser)
{
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_EOF) {
        if (parser->current.kind == TRYTR_TOKEN_RPAREN) { advance(parser); return; }
        if (parser->current.kind == TRYTR_TOKEN_AT || parser->current.kind == TRYTR_TOKEN_RBRACE) return;
        advance(parser);
    }
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

static void parse_positional_geometry(TrytrParser *parser, TrytrNode *node)
{
    size_t count = 0u;
    const char *const *names = geometry_property_names(node->kind, &count);
    if (names == NULL) return;
    advance(parser);
    for (size_t index = 0; index < count; ++index) {
        TrytrSourceLocation location = parser->current.location;
        int64_t value;
        if (!parse_expression(parser, 0u, 0u, &value)) { skip_positional(parser); return; }
        if (!add_property(parser, node, names[index], location, value)) return;
        if (index + 1u < count) {
            if (parser->current.kind != TRYTR_TOKEN_COMMA) {
                error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                      "positional shape arguments must be comma-separated");
                skip_positional(parser);
                return;
            }
            advance(parser);
        }
    }
    if (parser->current.kind != TRYTR_TOKEN_RPAREN) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location, "expected ')' after shape arguments");
        skip_positional(parser);
        return;
    }
    advance(parser);
}

static void skip_property(TrytrParser *parser)
{
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_EOF) {
        TrytrTokenKind kind = parser->current.kind;
        if (kind == TRYTR_TOKEN_RBRACE || kind == TRYTR_TOKEN_AT) return;
        if (kind == TRYTR_TOKEN_COMMA || kind == TRYTR_TOKEN_IDENTIFIER) { advance(parser); return; }
        advance(parser);
    }
}

static bool parse_rgb_color(TrytrParser *parser, int64_t *out_rgb)
{
    TrytrSourceLocation location = parser->current.location;
    if (!identifier_is(parser->current, "rgb")) {
        error(parser, "TRYTR-E-COLOR", location,
              "color values must use rgb(red, green, blue)");
        return false;
    }
    advance(parser);
    if (parser->current.kind != TRYTR_TOKEN_LPAREN) {
        error(parser, "TRYTR-E-COLOR", parser->current.location,
              "expected '(' after rgb");
        return false;
    }
    advance(parser);

    int64_t channels[3];
    for (size_t index = 0u; index < 3u; ++index) {
        TrytrSourceLocation channel_location = parser->current.location;
        if (!parse_expression(parser, 0u, 0u, &channels[index])) return false;
        if (channels[index] < 0 || channels[index] > 255) {
            error(parser, "TRYTR-E-COLOR", channel_location,
                  "RGB channels must be between 0 and 255");
            return false;
        }
        if (index < 2u) {
            if (parser->current.kind != TRYTR_TOKEN_COMMA) {
                error(parser, "TRYTR-E-COLOR", parser->current.location,
                      "expected ',' between RGB channels");
                return false;
            }
            advance(parser);
        }
    }
    if (parser->current.kind != TRYTR_TOKEN_RPAREN) {
        error(parser, "TRYTR-E-COLOR", parser->current.location,
              "expected ')' after RGB channels");
        return false;
    }
    advance(parser);
    *out_rgb = (channels[0] << 16u) | (channels[1] << 8u) | channels[2];
    return true;
}

static void parse_named_geometry(TrytrParser *parser, TrytrNode *node)
{
    advance(parser);
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_EOF &&
           parser->current.kind != TRYTR_TOKEN_RBRACE) {
        if (parser->current.kind == TRYTR_TOKEN_AT) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "unterminated shape property block");
            return;
        }
        if (parser->current.kind == TRYTR_TOKEN_COMMA) { advance(parser); continue; }
        if (parser->current.kind != TRYTR_TOKEN_IDENTIFIER) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location, "expected a named shape property");
            skip_property(parser);
            continue;
        }
        TrytrToken name = parser->current;
        advance(parser);
        if (parser->current.kind != TRYTR_TOKEN_COLON) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location, "expected ':' after property name");
            if (parser->current.kind != TRYTR_TOKEN_IDENTIFIER) skip_property(parser);
            continue;
        }
        advance(parser);
        int64_t value;
        const bool color_property = identifier_is(name, "fill") ||
                                    identifier_is(name, "stroke");
        if (color_property) {
            if (!parse_rgb_color(parser, &value)) { skip_property(parser); continue; }
        } else if (!parse_expression(parser, 0u, 0u, &value)) {
            skip_property(parser);
            continue;
        }
        if (trytr_ast_add_property(node, &parser->output->arena, name.lexeme, name.length,
                                   name.location, value) == NULL) {
            error(parser, "TRYTR-E-MEMORY", name.location, "unable to allocate property");
            parser->aborted = true;
            break;
        }
        if (parser->current.kind == TRYTR_TOKEN_COMMA) advance(parser);
        else if (parser->current.kind != TRYTR_TOKEN_RBRACE && parser->current.kind != TRYTR_TOKEN_IDENTIFIER) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location, "expected another property or '}'");
            skip_property(parser);
        }
    }
    if (parser->current.kind == TRYTR_TOKEN_RBRACE) advance(parser);
    else if (!parser->aborted) error(parser, "TRYTR-E-SYNTAX", parser->current.location, "unterminated named shape block");
}

static void parse_style_options(TrytrParser *parser, TrytrNode *node)
{
    if (parser->current.kind != TRYTR_TOKEN_LPAREN) return;
    advance(parser);
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_EOF &&
           parser->current.kind != TRYTR_TOKEN_RPAREN) {
        if (parser->current.kind != TRYTR_TOKEN_IDENTIFIER) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "expected a named style option");
            skip_positional(parser);
            return;
        }
        TrytrToken name = parser->current;
        advance(parser);
        if (parser->current.kind != TRYTR_TOKEN_COLON) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "expected ':' after style option name");
            skip_positional(parser);
            return;
        }
        advance(parser);
        int64_t value;
        if (identifier_is(name, "fill") || identifier_is(name, "stroke")) {
            if (!parse_rgb_color(parser, &value)) {
                skip_positional(parser);
                return;
            }
        } else if (node->kind == TRYTR_NODE_PLOT &&
                   (identifier_is(name, "grid") || identifier_is(name, "markers")) &&
                   parser->current.kind == TRYTR_TOKEN_IDENTIFIER &&
                   (identifier_is(parser->current, "true") ||
                    identifier_is(parser->current, "false"))) {
            value = identifier_is(parser->current, "true") ? 1 : 0;
            advance(parser);
        } else if (!parse_expression(parser, 0u, 0u, &value)) {
            skip_positional(parser);
            return;
        }
        if (trytr_ast_add_property(node, &parser->output->arena,
                                   name.lexeme, name.length,
                                   name.location, value) == NULL) {
            error(parser, "TRYTR-E-MEMORY", name.location,
                  "unable to allocate style option");
            parser->aborted = true;
            return;
        }
        if (parser->current.kind == TRYTR_TOKEN_COMMA) {
            advance(parser);
        } else if (parser->current.kind != TRYTR_TOKEN_RPAREN) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "expected ',' or ')' after style option");
            skip_positional(parser);
            return;
        }
    }
    if (parser->current.kind == TRYTR_TOKEN_RPAREN) advance(parser);
    else if (!parser->aborted) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "unterminated style options");
    }
}

static void parse_text_options(TrytrParser *parser, TrytrNode *node)
{
    if (parser->current.kind != TRYTR_TOKEN_LPAREN) return;
    advance(parser);
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_EOF &&
           parser->current.kind != TRYTR_TOKEN_RPAREN) {
        if (parser->current.kind != TRYTR_TOKEN_IDENTIFIER) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "expected a named text option");
            skip_positional(parser);
            return;
        }
        TrytrToken name = parser->current;
        advance(parser);
        if (parser->current.kind != TRYTR_TOKEN_COLON) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "expected ':' after text option name");
            skip_positional(parser);
            return;
        }
        advance(parser);

        int64_t value = 0;
        if (identifier_is(name, "color")) {
            if (!parse_rgb_color(parser, &value)) {
                skip_positional(parser);
                return;
            }
        } else if (identifier_is(name, "align")) {
            if (identifier_is(parser->current, "left")) {
                value = TRYTR_TEXT_ALIGN_LEFT;
            } else if (identifier_is(parser->current, "center")) {
                value = TRYTR_TEXT_ALIGN_CENTER;
            } else if (identifier_is(parser->current, "right")) {
                value = TRYTR_TEXT_ALIGN_RIGHT;
            } else {
                error(parser, "TRYTR-E-TEXT-ALIGN", parser->current.location,
                      "text alignment must be left, center, or right");
                skip_positional(parser);
                return;
            }
            advance(parser);
        } else {
            if (!parse_expression(parser, 0u, 0u, &value)) {
                skip_positional(parser);
                return;
            }
        }

        if (trytr_ast_add_property(node, &parser->output->arena,
                                   name.lexeme, name.length,
                                   name.location, value) == NULL) {
            error(parser, "TRYTR-E-MEMORY", name.location,
                  "unable to allocate text option");
            parser->aborted = true;
            return;
        }
        if (parser->current.kind == TRYTR_TOKEN_COMMA) {
            advance(parser);
        } else if (parser->current.kind != TRYTR_TOKEN_RPAREN) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "expected ',' or ')' after text option");
            skip_positional(parser);
            return;
        }
    }
    if (parser->current.kind == TRYTR_TOKEN_RPAREN) advance(parser);
    else if (!parser->aborted) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "unterminated text options");
    }
}

static void parse_optional_id(TrytrParser *parser, TrytrNode *node)
{
    if (parser->current.kind != TRYTR_TOKEN_HASH) return;
    TrytrSourceLocation location = parser->current.location;
    advance(parser);
    if (parser->current.kind != TRYTR_TOKEN_IDENTIFIER) {
        error(parser, "TRYTR-E-SYNTAX", location, "expected a stable identifier after '#'");
        return;
    }
    node->id = copy_token(parser, parser->current);
    if (node->id != NULL && find_parameter(parser, node->id, strlen(node->id)) != NULL) {
        error(parser, "TRYTR-E-DUPLICATE-ID", location,
              "parameter and element identifiers must be unique");
    }
    advance(parser);
}

static bool parameter_value_is_valid(int64_t minimum, int64_t maximum,
                                     int64_t step, int64_t value)
{
    if (minimum < -TRYTR_DEFAULT_COORDINATE_LIMIT ||
        maximum > TRYTR_DEFAULT_COORDINATE_LIMIT || minimum > maximum ||
        step < 1 || step > TRYTR_DEFAULT_COORDINATE_LIMIT ||
        value < minimum || value > maximum) return false;
    return (value - minimum) % step == 0;
}

static bool parse_parameter_expression(TrytrParser *parser, int64_t *out_value)
{
    parser->parameter_bounds_expression = true;
    const bool parsed = parse_expression(parser, 0u, 0u, out_value);
    parser->parameter_bounds_expression = false;
    return parsed;
}

static void parse_parameter_element(TrytrParser *parser, TrytrSourceLocation location,
                                    TrytrNode *parent)
{
    if (parent->kind != TRYTR_NODE_DOCUMENT) {
        error(parser, "TRYTR-E-PARAMETER", location,
              "parameters can only be declared directly inside @document");
        skip_element(parser);
        return;
    }
    if (parser->content_started) {
        error(parser, "TRYTR-E-PARAMETER", location,
              "declare parameters before document content");
        skip_element(parser);
        return;
    }
    if (parser->output->parameter_count >= TRYTR_DEFAULT_PARAMETER_LIMIT) {
        error(parser, "TRYTR-E-LIMIT", location, "document parameter limit exceeded");
        parser->aborted = true;
        return;
    }
    if (parser->current.kind != TRYTR_TOKEN_HASH) {
        error(parser, "TRYTR-E-PARAMETER", parser->current.location,
              "a parameter requires a stable identifier such as #gain");
        skip_element(parser);
        return;
    }
    const TrytrSourceLocation id_location = parser->current.location;
    advance(parser);
    if (parser->current.kind != TRYTR_TOKEN_IDENTIFIER) {
        error(parser, "TRYTR-E-PARAMETER", id_location,
              "expected a parameter identifier after '#'");
        skip_element(parser);
        return;
    }
    const TrytrToken id_token = parser->current;
    char *id = copy_token(parser, id_token);
    if (id == NULL) return;
    if (find_parameter(parser, id, strlen(id)) != NULL) {
        error(parser, "TRYTR-E-DUPLICATE-ID", id_location,
              "duplicate parameter identifier");
    }
    advance(parser);
    if (parser->current.kind != TRYTR_TOKEN_LPAREN) {
        error(parser, "TRYTR-E-PARAMETER", parser->current.location,
              "expected parameter bounds and default in parentheses");
        skip_element(parser);
        return;
    }
    advance(parser);

    int64_t minimum = 0;
    int64_t maximum = 0;
    int64_t default_value = 0;
    int64_t step = 0;
    unsigned int seen = 0u;
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_EOF &&
           parser->current.kind != TRYTR_TOKEN_RPAREN) {
        if (parser->current.kind != TRYTR_TOKEN_IDENTIFIER) {
            error(parser, "TRYTR-E-PARAMETER", parser->current.location,
                  "expected min, max, value, or step in a parameter declaration");
            skip_positional(parser);
            return;
        }
        const TrytrToken name = parser->current;
        unsigned int flag = 0u;
        int64_t *destination = NULL;
        if (identifier_is(name, "min")) { flag = 1u; destination = &minimum; }
        else if (identifier_is(name, "max")) { flag = 2u; destination = &maximum; }
        else if (identifier_is(name, "value")) { flag = 4u; destination = &default_value; }
        else if (identifier_is(name, "step")) { flag = 8u; destination = &step; }
        advance(parser);
        if (parser->current.kind != TRYTR_TOKEN_COLON) {
            error(parser, "TRYTR-E-PARAMETER", parser->current.location,
                  "expected ':' after parameter option name");
            skip_positional(parser);
            return;
        }
        advance(parser);
        int64_t option_value = 0;
        if (!parse_parameter_expression(parser, &option_value)) {
            skip_positional(parser);
            return;
        }
        if (flag == 0u) {
            error(parser, "TRYTR-E-PROPERTY", name.location,
                  "unknown parameter option");
        } else if ((seen & flag) != 0u) {
            error(parser, "TRYTR-E-PROPERTY", name.location,
                  "duplicate parameter option");
        } else {
            seen |= flag;
            *destination = option_value;
        }
        if (parser->current.kind == TRYTR_TOKEN_COMMA) {
            advance(parser);
        } else if (parser->current.kind != TRYTR_TOKEN_RPAREN) {
            error(parser, "TRYTR-E-PARAMETER", parser->current.location,
                  "expected ',' or ')' after parameter option");
            skip_positional(parser);
            return;
        }
    }
    if (parser->current.kind == TRYTR_TOKEN_RPAREN) advance(parser);
    else {
        error(parser, "TRYTR-E-PARAMETER", parser->current.location,
              "unterminated parameter declaration");
        return;
    }
    if (seen != 15u) {
        error(parser, "TRYTR-E-PARAMETER", id_location,
              "parameter requires min, max, value, and step");
    }
    if (!parameter_value_is_valid(minimum, maximum, step, default_value)) {
        error(parser, "TRYTR-E-PARAMETER-RANGE", id_location,
              "parameter bounds, default, or step are invalid");
    }
    if (parser->current.kind != TRYTR_TOKEN_LBRACE) {
        error(parser, "TRYTR-E-PARAMETER", parser->current.location,
              "expected a label block after parameter options");
        skip_element(parser);
        return;
    }
    char *label = NULL;
    size_t label_length = 0u;
    const size_t diagnostics_before = parser->output->diagnostics.count;
    if (!trytr_lexer_raw_block(&parser->lexer, &parser->output->arena,
                               &label, &label_length)) {
        if (parser->output->diagnostics.count == diagnostics_before) {
            parser->output->out_of_memory = true;
        }
        parser->current = trytr_lexer_next(&parser->lexer);
        skip_element(parser);
        return;
    }
    parser->current = trytr_lexer_next(&parser->lexer);

    int64_t value = default_value;
    size_t matching_overrides = 0u;
    for (size_t index = 0u; index < parser->override_count; ++index) {
        const TrytrParameterOverride *override = &parser->overrides[index];
        if (override->id != NULL && strcmp(override->id, id) == 0) {
            ++matching_overrides;
            value = override->value;
        }
    }
    if (matching_overrides > 1u) {
        error(parser, "TRYTR-E-PARAMETER", id_location,
              "a parameter override was supplied more than once");
    }
    if (matching_overrides != 0u &&
        !parameter_value_is_valid(minimum, maximum, step, value)) {
        error(parser, "TRYTR-E-PARAMETER-RANGE", id_location,
              "parameter override is outside its range or step interval");
        value = default_value;
    }
    if (label_length == 0u) {
        error(parser, "TRYTR-E-PARAMETER", id_location,
              "parameter label must not be empty");
    }

    TrytrParameterEntry *entry = trytr_arena_calloc(
        &parser->output->arena, 1u, sizeof(*entry));
    if (entry == NULL) {
        error(parser, "TRYTR-E-MEMORY", id_location,
              "unable to allocate parameter declaration");
        parser->aborted = true;
        return;
    }
    entry->parameter = (TrytrDocumentParameter){
        .id = id,
        .label = label,
        .label_length = label_length,
        .minimum = minimum,
        .maximum = maximum,
        .default_value = default_value,
        .value = value,
        .step = step,
        .location = location
    };
    if (parser->output->parameters_tail == NULL) {
        parser->output->parameters = entry;
    } else {
        parser->output->parameters_tail->next = entry;
    }
    parser->output->parameters_tail = entry;
    ++parser->output->parameter_count;
}

static void skip_group_contents(TrytrParser *parser)
{
    size_t depth = 1u;
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_EOF && depth != 0u) {
        if (parser->current.kind == TRYTR_TOKEN_LBRACE) ++depth;
        else if (parser->current.kind == TRYTR_TOKEN_RBRACE) --depth;
        advance(parser);
    }
}

static void parse_group_element(TrytrParser *parser, TrytrNode *node, TrytrNode *parent)
{
    if (parser->current.kind != TRYTR_TOKEN_LPAREN) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "expected group translation '(x, y)'");
        skip_element(parser);
        return;
    }
    advance(parser);
    if (!parse_expression(parser, 0u, 0u, &node->group_translate_x)) {
        skip_positional(parser);
        return;
    }
    if (parser->current.kind != TRYTR_TOKEN_COMMA) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "group translation coordinates must be comma-separated");
        skip_positional(parser);
        return;
    }
    advance(parser);
    if (!parse_expression(parser, 0u, 0u, &node->group_translate_y)) {
        skip_positional(parser);
        return;
    }
    if (parser->current.kind != TRYTR_TOKEN_RPAREN) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "expected ')' after group translation");
        skip_positional(parser);
        return;
    }
    advance(parser);
    if (parser->current.kind != TRYTR_TOKEN_LBRACE) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "expected a group body");
        skip_element(parser);
        return;
    }
    advance(parser);
    if (parser->group_depth >= TRYTR_DEFAULT_DEPTH_LIMIT) {
        error(parser, "TRYTR-E-LIMIT", node->location, "group nesting limit exceeded");
        skip_group_contents(parser);
        return;
    }
    ++parser->group_depth;
    parse_element_list(parser, node);
    --parser->group_depth;
    trytr_ast_add_child(parent, node);
}

static void parse_text_element(TrytrParser *parser, TrytrNode *node, TrytrNode *root)
{
    if (parser->current.kind != TRYTR_TOKEN_LBRACE) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location, "expected a raw text block");
        skip_element(parser);
        return;
    }
    const size_t diagnostics_before = parser->output->diagnostics.count;
    if (!trytr_lexer_raw_block(&parser->lexer, &parser->output->arena, &node->text, &node->text_length)) {
        if (parser->output->diagnostics.count == diagnostics_before) {
            parser->output->out_of_memory = true;
        }
        parser->current = trytr_lexer_next(&parser->lexer);
        skip_element(parser);
        return;
    }
    parser->current = trytr_lexer_next(&parser->lexer);
    char *expanded = NULL;
    size_t expanded_length = 0u;
    if (!expand_parameter_references(parser, node->text, node->text_length,
                                     TRYTR_DEFAULT_EXECUTION_TEXT_LIMIT,
                                     node->location, &expanded, &expanded_length)) {
        return;
    }
    node->text = expanded;
    node->text_length = expanded_length;
    if (!build_math_tree(parser, node)) return;
    trytr_ast_add_child(root, node);
}

static void parse_expression_element(TrytrParser *parser, TrytrNode *node,
                                     TrytrNode *root)
{
    if (parser->current.kind != TRYTR_TOKEN_LBRACE) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "expected a scalar expression block");
        skip_element(parser);
        return;
    }
    const size_t diagnostics_before = parser->output->diagnostics.count;
    if (!trytr_lexer_raw_block(&parser->lexer, &parser->output->arena,
                               &node->text, &node->text_length)) {
        if (parser->output->diagnostics.count == diagnostics_before)
            parser->output->out_of_memory = true;
        parser->current = trytr_lexer_next(&parser->lexer);
        skip_element(parser);
        return;
    }
    parser->current = trytr_lexer_next(&parser->lexer);

    char *expanded = NULL;
    size_t expanded_length = 0u;
    if (!expand_parameter_references(parser, node->text, node->text_length,
                                     4096u, node->location, &expanded,
                                     &expanded_length)) return;
    TrytrMathExpression *expression = NULL;
    size_t error_offset = 0u;
    const TrytrMathExpressionStatus compile_status = trytr_math_expression_compile(
        expanded, expanded_length, &expression, &error_offset);
    if (compile_status != TRYTR_MATH_EXPRESSION_OK) {
        if (compile_status == TRYTR_MATH_EXPRESSION_NO_MEMORY) {
            error(parser, "TRYTR-E-MEMORY", node->location,
                  "unable to compile scalar expression");
            parser->aborted = true;
            return;
        }
        error(parser, compile_status == TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED
                  ? "TRYTR-E-LIMIT" : "TRYTR-E-EXPRESSION",
              node->location,
              compile_status == TRYTR_MATH_EXPRESSION_UNKNOWN_NAME
                  ? "unknown name in scalar expression"
                  : "invalid scalar expression or function arguments");
        return;
    }
    double value = 0.0;
    if (!trytr_math_expression_evaluate(expression, 0.0, &value)) {
        trytr_math_expression_destroy(expression);
        error(parser, "TRYTR-E-EXPRESSION-DOMAIN", node->location,
              "scalar expression has a domain error or non-finite result");
        return;
    }
    trytr_math_expression_destroy(expression);
    char formatted[64];
    const int formatted_length = snprintf(formatted, sizeof(formatted), "%.17g", value);
    if (formatted_length < 0 || (size_t)formatted_length >= sizeof(formatted)) {
        error(parser, "TRYTR-E-EXPRESSION", node->location,
              "scalar expression result could not be formatted");
        return;
    }
    node->text = trytr_arena_copy_string(&parser->output->arena, formatted,
                                         (size_t)formatted_length);
    if (node->text == NULL) {
        error(parser, "TRYTR-E-MEMORY", node->location,
              "unable to store scalar expression result");
        parser->aborted = true;
        return;
    }
    node->text_length = (size_t)formatted_length;
    if (!build_math_tree(parser, node)) return;
    trytr_ast_add_child(root, node);
}

static void advance_source_location(const char *source, size_t length,
                                   size_t byte_count, TrytrSourceLocation *location)
{
    size_t position = 0u;
    if (byte_count > length) byte_count = length;
    while (position < byte_count) {
        const unsigned char first = (unsigned char)source[position];
        size_t width = first >= 0xF0u ? 4u : first >= 0xE0u ? 3u :
                       first >= 0xC0u ? 2u : 1u;
        if (width > byte_count - position) width = 1u;
        if (source[position] == '\n') {
            ++location->line;
            location->column = 1u;
        } else if ((first & 0xC0u) != 0x80u) {
            ++location->column;
        }
        position += width;
        location->offset += width;
    }
}

static TrytrSourceLocation raw_block_error_location(
    const TrytrParser *parser,
    size_t raw_source_offset,
    TrytrSourceLocation raw_location,
    size_t error_offset
)
{
    size_t trimmed_bytes = 0u;
    while (raw_source_offset + trimmed_bytes < parser->lexer.length) {
        const char value = parser->lexer.source[raw_source_offset + trimmed_bytes];
        if (value != ' ' && value != '\t' && value != '\n' && value != '\r' &&
            value != '\v' && value != '\f') break;
        ++trimmed_bytes;
    }
    if (raw_source_offset > parser->lexer.length) return raw_location;
    advance_source_location(parser->lexer.source + raw_source_offset,
                            parser->lexer.length - raw_source_offset,
                            trimmed_bytes + error_offset, &raw_location);
    return raw_location;
}

static void parse_path_element(TrytrParser *parser, TrytrNode *node, TrytrNode *root)
{
    if (parser->current.kind != TRYTR_TOKEN_LBRACE) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "expected a path data block");
        skip_element(parser);
        return;
    }
    const size_t diagnostics_before = parser->output->diagnostics.count;
    const size_t raw_source_offset = parser->lexer.position;
    const TrytrSourceLocation raw_location = {
        .offset = raw_source_offset,
        .line = parser->lexer.line,
        .column = parser->lexer.column
    };
    if (!trytr_lexer_raw_block(&parser->lexer, &parser->output->arena,
                               &node->text, &node->text_length)) {
        if (parser->output->diagnostics.count == diagnostics_before) {
            parser->output->out_of_memory = true;
        }
        parser->current = trytr_lexer_next(&parser->lexer);
        skip_element(parser);
        return;
    }
    parser->current = trytr_lexer_next(&parser->lexer);

    TrytrPathAnalysis analysis;
    size_t error_offset = 0u;
    const TrytrPathStatus path_status = trytr_path_analyze(
        node->text, node->text_length, &analysis, &error_offset);
    if (path_status != TRYTR_PATH_OK) {
        const TrytrSourceLocation location = raw_block_error_location(
            parser, raw_source_offset, raw_location, error_offset);
        if (path_status == TRYTR_PATH_LIMIT_EXCEEDED) {
            error(parser, "TRYTR-E-LIMIT", location,
                  "path data exceeds its byte or segment limit");
        } else if (path_status == TRYTR_PATH_COORDINATE_RANGE) {
            error(parser, "TRYTR-E-COORDINATE", location,
                  "path coordinate exceeds the coordinate limit");
        } else {
            error(parser, "TRYTR-E-PATH", location,
                  "invalid path data; expected uppercase M, L, H, V, C, Q, or Z commands with integer coordinates");
        }
        return;
    }
    node->path_min_x = analysis.min_x;
    node->path_min_y = analysis.min_y;
    node->path_max_x = analysis.max_x;
    node->path_max_y = analysis.max_y;
    node->path_segment_count = analysis.segment_count;
    trytr_ast_add_child(root, node);
}

static void parse_polygon_element(TrytrParser *parser, TrytrNode *node, TrytrNode *root)
{
    if (parser->current.kind != TRYTR_TOKEN_LBRACE) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "expected a polygon point block");
        skip_element(parser);
        return;
    }
    const size_t diagnostics_before = parser->output->diagnostics.count;
    const size_t raw_source_offset = parser->lexer.position;
    const TrytrSourceLocation raw_location = {
        .offset = raw_source_offset,
        .line = parser->lexer.line,
        .column = parser->lexer.column
    };
    if (!trytr_lexer_raw_block(&parser->lexer, &parser->output->arena,
                               &node->text, &node->text_length)) {
        if (parser->output->diagnostics.count == diagnostics_before) {
            parser->output->out_of_memory = true;
        }
        parser->current = trytr_lexer_next(&parser->lexer);
        skip_element(parser);
        return;
    }
    parser->current = trytr_lexer_next(&parser->lexer);

    TrytrPolygonAnalysis analysis;
    size_t error_offset = 0u;
    const TrytrPolygonStatus status = trytr_polygon_analyze(
        node->text, node->text_length, &analysis, &error_offset);
    if (status != TRYTR_POLYGON_OK) {
        const TrytrSourceLocation location = raw_block_error_location(
            parser, raw_source_offset, raw_location, error_offset);
        if (status == TRYTR_POLYGON_LIMIT_EXCEEDED) {
            error(parser, "TRYTR-E-LIMIT", location,
                  "polygon points exceed their byte or point limit");
        } else if (status == TRYTR_POLYGON_COORDINATE_RANGE) {
            error(parser, "TRYTR-E-COORDINATE", location,
                  "polygon coordinate exceeds the coordinate limit");
        } else {
            error(parser, "TRYTR-E-POLYGON", location,
                  "invalid polygon points; expected at least three integer coordinate pairs");
        }
        return;
    }
    node->polygon_point_count = analysis.point_count;
    node->polygon_min_x = analysis.min_x;
    node->polygon_min_y = analysis.min_y;
    node->polygon_max_x = analysis.max_x;
    node->polygon_max_y = analysis.max_y;
    trytr_ast_add_child(root, node);
}

static void parse_plot_element(TrytrParser *parser, TrytrNode *node, TrytrNode *root)
{
    if (parser->current.kind != TRYTR_TOKEN_LBRACE) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "expected a plot data block after its chart bounds");
        skip_element(parser);
        trytr_ast_add_child(root, node);
        return;
    }
    const size_t diagnostics_before = parser->output->diagnostics.count;
    const size_t raw_source_offset = parser->lexer.position;
    const TrytrSourceLocation raw_location = {
        .offset = raw_source_offset,
        .line = parser->lexer.line,
        .column = parser->lexer.column
    };
    if (!trytr_lexer_raw_block(&parser->lexer, &parser->output->arena,
                               &node->text, &node->text_length)) {
        if (parser->output->diagnostics.count == diagnostics_before) {
            parser->output->out_of_memory = true;
        }
        parser->current = trytr_lexer_next(&parser->lexer);
        skip_element(parser);
        return;
    }
    parser->current = trytr_lexer_next(&parser->lexer);

    char *expanded_points = NULL;
    size_t expanded_points_length = 0u;
    if (!expand_parameter_references(parser, node->text, node->text_length,
                                     TRYTR_DEFAULT_PLOT_BYTE_LIMIT,
                                     raw_location, &expanded_points,
                                     &expanded_points_length)) {
        trytr_ast_add_child(root, node);
        return;
    }
    node->text = expanded_points;
    node->text_length = expanded_points_length;

    size_t expression_start = 0u;
    while (expression_start < node->text_length &&
           (node->text[expression_start] == ' ' || node->text[expression_start] == '\t' ||
            node->text[expression_start] == '\n' || node->text[expression_start] == '\r')) {
        ++expression_start;
    }
    const bool formula_plot = node->text_length - expression_start >= 2u &&
        node->text[expression_start] == 'y' &&
        (node->text[expression_start + 1u] == '=' ||
         node->text[expression_start + 1u] == ' ' ||
         node->text[expression_start + 1u] == '\t');
    if (formula_plot) {
        size_t formula_start = expression_start + 1u;
        while (formula_start < node->text_length &&
               (node->text[formula_start] == ' ' || node->text[formula_start] == '\t')) {
            ++formula_start;
        }
        if (formula_start >= node->text_length || node->text[formula_start] != '=') {
            error(parser, "TRYTR-E-PLOT-FORMULA", raw_location,
                  "formula plots must begin with 'y = expression'");
            trytr_ast_add_child(root, node);
            return;
        }
        ++formula_start;
        while (formula_start < node->text_length &&
               (node->text[formula_start] == ' ' || node->text[formula_start] == '\t' ||
                node->text[formula_start] == '\n' || node->text[formula_start] == '\r')) {
            ++formula_start;
        }
        int64_t min_x = -10;
        int64_t max_x = 10;
        int64_t samples = 257;
        for (const TrytrProperty *property = node->properties;
             property != NULL; property = property->next) {
            if (strcmp(property->name, "x-min") == 0) min_x = property->integer_value;
            else if (strcmp(property->name, "x-max") == 0) max_x = property->integer_value;
            else if (strcmp(property->name, "samples") == 0) samples = property->integer_value;
        }
        if (min_x >= max_x || min_x < -TRYTR_DEFAULT_COORDINATE_LIMIT ||
            max_x > TRYTR_DEFAULT_COORDINATE_LIMIT) {
            error(parser, "TRYTR-E-PLOT-DOMAIN", raw_location,
                  "formula plot x-min must be less than x-max within the coordinate range");
            trytr_ast_add_child(root, node);
            return;
        }
        if (samples < 2 || samples > 1024) {
            error(parser, "TRYTR-E-PLOT-SAMPLES", raw_location,
                  "formula plot samples must be between 2 and 1024");
            trytr_ast_add_child(root, node);
            return;
        }
        char *expanded_formula = NULL;
        size_t expanded_formula_length = 0u;
        if (!expand_parameter_references(parser, node->text + formula_start,
                                         node->text_length - formula_start,
                                         4096u, raw_location, &expanded_formula,
                                         &expanded_formula_length)) {
            trytr_ast_add_child(root, node);
            return;
        }
        TrytrPlotAnalysis formula_analysis;
        size_t formula_error_offset = 0u;
        const TrytrPlotFormulaStatus formula_status = trytr_plot_formula_sample(
            expanded_formula, expanded_formula_length, (double)min_x,
            (double)max_x, (size_t)samples, &parser->output->arena,
            &node->text, &node->text_length, &formula_analysis,
            &formula_error_offset);
        if (formula_status != TRYTR_PLOT_FORMULA_OK) {
            const TrytrSourceLocation location = raw_block_error_location(
                parser, raw_source_offset, raw_location,
                formula_start + formula_error_offset);
            if (formula_status == TRYTR_PLOT_FORMULA_NO_MEMORY) {
                error(parser, "TRYTR-E-MEMORY", location,
                      "unable to sample formula plot");
                parser->aborted = true;
            } else if (formula_status == TRYTR_PLOT_FORMULA_LIMIT_EXCEEDED) {
                error(parser, "TRYTR-E-LIMIT", location,
                      "formula exceeds its operation or output limit");
            } else if (formula_status == TRYTR_PLOT_FORMULA_DOMAIN) {
                error(parser, "TRYTR-E-PLOT-FORMULA", location,
                      "formula is undefined or outside the coordinate range at a sample");
            } else {
                error(parser, "TRYTR-E-PLOT-FORMULA", location,
                      "invalid formula; use y = expression with supported math functions");
            }
            trytr_ast_add_child(root, node);
            return;
        }
        node->formula_plot = true;
        node->plot_point_count = formula_analysis.point_count;
        node->plot_min_x = formula_analysis.min_x;
        node->plot_min_y = formula_analysis.min_y;
        node->plot_max_x = formula_analysis.max_x;
        node->plot_max_y = formula_analysis.max_y;
        trytr_ast_add_child(root, node);
        return;
    }

    TrytrPlotAnalysis analysis;
    size_t error_offset = 0u;
    const TrytrPlotStatus status = trytr_plot_analyze(
        node->text, node->text_length, &analysis, &error_offset);
    if (status != TRYTR_PLOT_OK) {
        const TrytrSourceLocation location = raw_block_error_location(
            parser, raw_source_offset, raw_location, error_offset);
        if (status == TRYTR_PLOT_LIMIT_EXCEEDED) {
            error(parser, "TRYTR-E-LIMIT", location,
                  "plot data exceeds its byte or point limit");
        } else if (status == TRYTR_PLOT_COORDINATE_RANGE) {
            error(parser, "TRYTR-E-COORDINATE", location,
                  "plot data coordinate exceeds the coordinate limit");
        } else {
            error(parser, "TRYTR-E-PLOT", location,
                  "invalid plot data; expected at least two decimal x/y pairs");
        }
        trytr_ast_add_child(root, node);
        return;
    }
    node->plot_point_count = analysis.point_count;
    node->plot_min_x = analysis.min_x;
    node->plot_min_y = analysis.min_y;
    node->plot_max_x = analysis.max_x;
    node->plot_max_y = analysis.max_y;
    trytr_ast_add_child(root, node);
}

static void parse_element(TrytrParser *parser, TrytrNode *root)
{
    TrytrSourceLocation location = parser->current.location;
    advance(parser);
    if (parser->current.kind != TRYTR_TOKEN_IDENTIFIER) {
        error(parser, "TRYTR-E-SYNTAX", location, "expected an element name after '@'");
        skip_element(parser);
        return;
    }
    TrytrToken name = parser->current;
    advance(parser);
    if (identifier_is(name, "param")) {
        parse_parameter_element(parser, location, root);
        return;
    }
    parser->content_started = true;
    TrytrNodeKind kind;
    bool text_element = false;
    bool expression_element = false;
    bool path_element = false;
    bool group_element = false;
    bool polygon_element = false;
    bool plot_element = false;
    if (identifier_is(name, "heading")) { kind = TRYTR_NODE_HEADING; text_element = true; }
    else if (identifier_is(name, "text")) { kind = TRYTR_NODE_TEXT; text_element = true; }
    else if (identifier_is(name, "math")) { kind = TRYTR_NODE_MATH; text_element = true; }
    else if (identifier_is(name, "expr")) { kind = TRYTR_NODE_MATH; expression_element = true; }
    else if (identifier_is(name, "rect")) kind = TRYTR_NODE_RECT;
    else if (identifier_is(name, "roundrect")) kind = TRYTR_NODE_ROUNDRECT;
    else if (identifier_is(name, "line")) kind = TRYTR_NODE_LINE;
    else if (identifier_is(name, "circle")) kind = TRYTR_NODE_CIRCLE;
    else if (identifier_is(name, "ellipse")) kind = TRYTR_NODE_ELLIPSE;
    else if (identifier_is(name, "path")) { kind = TRYTR_NODE_PATH; path_element = true; }
    else if (identifier_is(name, "polygon")) { kind = TRYTR_NODE_POLYGON; polygon_element = true; }
    else if (identifier_is(name, "plot")) { kind = TRYTR_NODE_PLOT; plot_element = true; }
    else if (identifier_is(name, "group")) { kind = TRYTR_NODE_GROUP; group_element = true; }
    else {
        error(parser, "TRYTR-E-ELEMENT", name.location, "unknown document element");
        skip_element(parser);
        return;
    }
    TrytrNode *node = node_new(parser, kind, location);
    if (node == NULL) return;
    parse_optional_id(parser, node);
    if (text_element) {
        parse_text_options(parser, node);
        if (parser->aborted) return;
        parse_text_element(parser, node, root);
        return;
    }
    if (expression_element) {
        parse_expression_element(parser, node, root);
        return;
    }
    if (path_element || polygon_element) {
        parse_style_options(parser, node);
        if (parser->aborted) return;
        if (path_element) parse_path_element(parser, node, root);
        else parse_polygon_element(parser, node, root);
        return;
    }
    if (plot_element) {
        if (parser->current.kind == TRYTR_TOKEN_LPAREN) parse_positional_geometry(parser, node);
        else {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "expected plot bounds as (x, y, width, height)");
            skip_element(parser);
        }
        if (parser->aborted) return;
        parse_style_options(parser, node);
        if (parser->aborted) return;
        parse_plot_element(parser, node, root);
        return;
    }
    if (group_element) { parse_group_element(parser, node, root); return; }
    if (parser->current.kind == TRYTR_TOKEN_LPAREN) parse_positional_geometry(parser, node);
    else if (parser->current.kind == TRYTR_TOKEN_LBRACE) parse_named_geometry(parser, node);
    else {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "expected positional arguments or a named shape block");
        skip_element(parser);
    }
    trytr_ast_add_child(root, node);
}

static void parse_element_list(TrytrParser *parser, TrytrNode *parent)
{
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_EOF &&
           parser->current.kind != TRYTR_TOKEN_RBRACE) {
        if (parser->current.kind != TRYTR_TOKEN_AT) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "expected an element or group end");
            advance(parser);
            continue;
        }
        parse_element(parser, parent);
    }
    if (parser->current.kind == TRYTR_TOKEN_RBRACE) advance(parser);
    else if (!parser->aborted) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              parent->kind == TRYTR_NODE_DOCUMENT
                  ? "unterminated document body"
                  : "unterminated element group");
    }
}

static void skip_document_option(TrytrParser *parser)
{
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_COMMA &&
           parser->current.kind != TRYTR_TOKEN_RPAREN &&
           parser->current.kind != TRYTR_TOKEN_EOF) {
        advance(parser);
    }
}

static void parse_document_options(TrytrParser *parser, TrytrNode *root)
{
    if (parser->current.kind != TRYTR_TOKEN_LPAREN) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location, "expected '(version: 1)'");
        return;
    }
    advance(parser);
    bool have_version = false;
    bool first_option = true;
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_RPAREN &&
           parser->current.kind != TRYTR_TOKEN_EOF) {
        if (!first_option) {
            if (parser->current.kind != TRYTR_TOKEN_COMMA) {
                error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                      "expected ',' between document options");
                skip_document_option(parser);
                if (parser->current.kind != TRYTR_TOKEN_COMMA) break;
            }
            advance(parser);
            if (parser->current.kind == TRYTR_TOKEN_RPAREN) {
                error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                      "expected a document option after ','");
                break;
            }
        }
        first_option = false;

        TrytrToken name = parser->current;
        if (name.kind != TRYTR_TOKEN_IDENTIFIER) {
            error(parser, "TRYTR-E-SYNTAX", name.location,
                  "expected a document option name");
            advance(parser);
            continue;
        }
        advance(parser);
        if (parser->current.kind != TRYTR_TOKEN_COLON) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "expected ':' after document option");
            skip_document_option(parser);
            continue;
        }
        advance(parser);
        if (parser->current.kind != TRYTR_TOKEN_INTEGER ||
            parser->current.integer_value > (uint64_t)INT64_MAX) {
            error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                  "document option values must be non-negative integers");
            skip_document_option(parser);
            continue;
        }
        const int64_t value = (int64_t)parser->current.integer_value;
        if (identifier_is(name, "version")) {
            if (have_version) {
                error(parser, "TRYTR-E-PROPERTY", name.location,
                      "duplicate document version option");
            } else if (value == 0) {
                error(parser, "TRYTR-E-SYNTAX", parser->current.location,
                      "version must be a positive integer");
            } else {
                root->version = value;
                have_version = true;
            }
        } else if (identifier_is(name, "width")) {
            if (root->has_canvas_width) {
                error(parser, "TRYTR-E-PROPERTY", name.location,
                      "duplicate document width option");
            } else {
                root->canvas_width = value;
                root->has_canvas_width = true;
            }
        } else if (identifier_is(name, "height")) {
            if (root->has_canvas_height) {
                error(parser, "TRYTR-E-PROPERTY", name.location,
                      "duplicate document height option");
            } else {
                root->canvas_height = value;
                root->has_canvas_height = true;
            }
        } else {
            error(parser, "TRYTR-E-PROPERTY", name.location,
                  "unknown document option");
        }
        advance(parser);
    }
    if (!have_version) {
        error(parser, "TRYTR-E-SYNTAX", root->location,
              "document requires a version property");
    }
    if (parser->current.kind == TRYTR_TOKEN_RPAREN) advance(parser);
    else if (!parser->aborted) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location,
              "expected ')' after document options");
    }
}

static TrytrNode *parse_document(TrytrParser *parser)
{
    TrytrSourceLocation location = parser->current.location;
    if (parser->current.kind != TRYTR_TOKEN_AT) {
        error(parser, "TRYTR-E-SYNTAX", location, "document must begin with @document");
        return NULL;
    }
    advance(parser);
    if (!identifier_is(parser->current, "document")) {
        error(parser, "TRYTR-E-SYNTAX", location, "document must begin with @document");
        return NULL;
    }
    advance(parser);
    TrytrNode *root = node_new(parser, TRYTR_NODE_DOCUMENT, location);
    if (root == NULL) return NULL;
    parse_document_options(parser, root);
    while (!parser->aborted && parser->current.kind != TRYTR_TOKEN_LBRACE &&
           parser->current.kind != TRYTR_TOKEN_EOF) advance(parser);
    if (parser->current.kind != TRYTR_TOKEN_LBRACE) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location, "expected document body");
        return root;
    }
    advance(parser);
    parse_element_list(parser, root);
    if (!parser->aborted && parser->current.kind != TRYTR_TOKEN_EOF) {
        error(parser, "TRYTR-E-SYNTAX", parser->current.location, "unexpected content after document");
    }
    return root;
}

bool trytr_parser_parse_with_parameters(
    const char *source,
    size_t length,
    const TrytrParameterOverride *overrides,
    size_t override_count,
    TrytrParserOutput *output
)
{
    if (output == NULL) return false;
    memset(output, 0, sizeof(*output));
    trytr_arena_init(&output->arena, TRYTR_DEFAULT_ARENA_LIMIT);
    trytr_diagnostic_list_init(&output->diagnostics, &output->arena,
                               TRYTR_DEFAULT_DIAGNOSTIC_LIMIT + 1u);
    output->initialized = true;
    if (source == NULL) return false;
    if (length > TRYTR_DEFAULT_SOURCE_LIMIT) {
        (void)trytr_diagnostic_add(&output->diagnostics, TRYTR_DIAGNOSTIC_ERROR, "TRYTR-E-LIMIT",
            (TrytrSourceLocation){0, 1, 1}, "source exceeds the implementation size limit");
        return false;
    }
    if (!trytr_utf8_validate(source, length, &output->diagnostics)) return false;
    if ((overrides == NULL && override_count != 0u) ||
        override_count > TRYTR_DEFAULT_PARAMETER_LIMIT) {
        (void)trytr_diagnostic_add(&output->diagnostics, TRYTR_DIAGNOSTIC_ERROR,
            override_count > TRYTR_DEFAULT_PARAMETER_LIMIT ? "TRYTR-E-LIMIT" : "TRYTR-E-PARAMETER",
            (TrytrSourceLocation){0u, 1u, 1u},
            "parameter overrides are invalid or exceed the parameter limit");
        return false;
    }
    TrytrParser parser;
    memset(&parser, 0, sizeof(parser));
    parser.output = output;
    parser.overrides = overrides;
    parser.override_count = override_count;
    trytr_lexer_init(&parser.lexer, source, length, &output->diagnostics);
    parser.current = trytr_lexer_next(&parser.lexer);
    output->root = parse_document(&parser);
    for (size_t index = 0u; index < override_count; ++index) {
        if (overrides[index].id == NULL || overrides[index].id[0] == '\0') {
            (void)trytr_diagnostic_add(&output->diagnostics, TRYTR_DIAGNOSTIC_ERROR,
                "TRYTR-E-PARAMETER", (TrytrSourceLocation){0u, 1u, 1u},
                "parameter override requires a non-empty identifier");
            continue;
        }
        bool found = false;
        for (TrytrParameterEntry *entry = output->parameters;
             entry != NULL; entry = entry->next) {
            if (strcmp(entry->parameter.id, overrides[index].id) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            (void)trytr_diagnostic_add(&output->diagnostics, TRYTR_DIAGNOSTIC_ERROR,
                "TRYTR-E-PARAMETER", (TrytrSourceLocation){0u, 1u, 1u},
                "override refers to an undeclared parameter");
        }
    }
    return output->root != NULL && !parser.aborted && !output->out_of_memory &&
           !trytr_diagnostic_has_errors(&output->diagnostics);
}

bool trytr_parser_parse(const char *source, size_t length, TrytrParserOutput *output)
{
    return trytr_parser_parse_with_parameters(source, length, NULL, 0u, output);
}

void trytr_parser_output_destroy(TrytrParserOutput *output)
{
    if (output == NULL || !output->initialized) return;
    trytr_arena_destroy(&output->arena);
    memset(output, 0, sizeof(*output));
}
