#include "notation_internal.h"

#include <stdbool.h>
#include <string.h>

#define TRYTR_MATH_NOTATION_NODE_LIMIT 8192u
#define TRYTR_MATH_NOTATION_DEPTH_LIMIT 32u

typedef struct MathParser {
    TrytrArena *arena;
    const char *source;
    size_t length;
    size_t position;
    size_t node_count;
    TrytrMathNotationStatus status;
} MathParser;

static TrytrMathNode *new_node(MathParser *parser, TrytrMathNodeKind kind,
                               size_t start, size_t end)
{
    if (parser->status != TRYTR_MATH_NOTATION_OK) return NULL;
    if (parser->node_count >= TRYTR_MATH_NOTATION_NODE_LIMIT) {
        parser->status = TRYTR_MATH_NOTATION_LIMIT_EXCEEDED;
        return NULL;
    }
    TrytrMathNode *node = trytr_arena_calloc(parser->arena, 1u, sizeof(*node));
    if (node == NULL) {
        parser->status = TRYTR_MATH_NOTATION_NO_MEMORY;
        return NULL;
    }
    ++parser->node_count;
    node->kind = kind;
    node->source_offset = start;
    node->source_length = end >= start ? end - start : 0u;
    return node;
}

static void node_append(TrytrMathNode *parent, TrytrMathNode *child,
                        TrytrMathNode **tail)
{
    if (parent->first_child == NULL) parent->first_child = child;
    else (*tail)->next_sibling = child;
    *tail = child;
    ++parent->child_count;
}

static void skip_space(MathParser *parser)
{
    while (parser->position < parser->length) {
        const char byte = parser->source[parser->position];
        if (byte != ' ' && byte != '\t' && byte != '\n' && byte != '\r' &&
            byte != '\f' && byte != '\v') break;
        ++parser->position;
    }
}

static bool is_ascii_letter(unsigned char byte)
{
    return (byte >= (unsigned char)'a' && byte <= (unsigned char)'z') ||
           (byte >= (unsigned char)'A' && byte <= (unsigned char)'Z');
}

static bool is_identifier_byte(unsigned char byte)
{
    return is_ascii_letter(byte) || (byte >= (unsigned char)'0' &&
           byte <= (unsigned char)'9') || byte == '_' || byte >= 0x80u;
}

static bool starts_with(const MathParser *parser, size_t position,
                        const char *text)
{
    const size_t length = strlen(text);
    return position <= parser->length && length <= parser->length - position &&
           memcmp(parser->source + position, text, length) == 0;
}

static bool starts_unicode_math_operator(const MathParser *parser,
                                         size_t position)
{
    static const char *const symbols[] = {
        "\xE2\x89\x88", "\xE2\x89\xA4", "\xE2\x89\xA5",
        "\xE2\x89\xA0", "\xE2\x88\x88", "\xE2\x86\x92",
        "\xC2\xB7", "\xC3\x97", "\xC3\xB7", "\xE2\x88\x9A"
    };
    for (size_t index = 0u; index < sizeof(symbols) / sizeof(symbols[0]); ++index) {
        if (starts_with(parser, position, symbols[index])) return true;
    }
    return false;
}

static bool command_is(const MathParser *parser, size_t start,
                       size_t command_end, const char *name)
{
    const size_t length = strlen(name);
    return command_end - start == length + 1u &&
           parser->source[start] == '\\' &&
           memcmp(parser->source + start + 1u, name, length) == 0;
}

static TrytrMathNode *parse_row(MathParser *parser, char terminator,
                                size_t depth);
static TrytrMathNode *parse_expression(MathParser *parser, unsigned int minimum,
                                       size_t depth);
static TrytrMathNode *parse_atom(MathParser *parser, size_t depth,
                                 bool consume_scripts);

static TrytrMathNode *raw_to_end(MathParser *parser, size_t start)
{
    parser->position = parser->length;
    return new_node(parser, TRYTR_MATH_NODE_RAW, start, parser->position);
}

static TrytrMathNode *parse_group(MathParser *parser, char closing,
                                  size_t depth)
{
    const size_t start = parser->position;
    ++parser->position;
    TrytrMathNode *group = new_node(parser, TRYTR_MATH_NODE_GROUP, start, start);
    if (group == NULL) return NULL;
    TrytrMathNode *content = parse_row(parser, closing, depth + 1u);
    if (content == NULL) return NULL;
    if (parser->position < parser->length &&
        parser->source[parser->position] == closing) {
        ++parser->position;
    }
    group->source_length = parser->position - start;
    group->first_child = content;
    group->child_count = 1u;
    return group;
}

static TrytrMathNode *parse_argument_group(MathParser *parser, size_t depth)
{
    if (parser->position >= parser->length ||
        parser->source[parser->position] != '{') return NULL;
    return parse_group(parser, '}', depth);
}

static TrytrMathNode *parse_command(MathParser *parser, size_t depth)
{
    const size_t start = parser->position++;
    if (parser->position < parser->length &&
        is_ascii_letter((unsigned char)parser->source[parser->position])) {
        while (parser->position < parser->length &&
               is_ascii_letter((unsigned char)parser->source[parser->position])) {
            ++parser->position;
        }
    } else if (parser->position < parser->length) {
        ++parser->position;
    }
    const size_t command_end = parser->position;

    if (command_is(parser, start, command_end, "frac")) {
        skip_space(parser);
        TrytrMathNode *numerator = parse_argument_group(parser, depth);
        if (numerator != NULL) {
            skip_space(parser);
            TrytrMathNode *denominator = parse_argument_group(parser, depth);
            if (denominator != NULL) {
                TrytrMathNode *fraction = new_node(
                    parser, TRYTR_MATH_NODE_FRACTION, start, parser->position);
                if (fraction == NULL) return NULL;
                TrytrMathNode *tail = NULL;
                node_append(fraction, numerator, &tail);
                node_append(fraction, denominator, &tail);
                return fraction;
            }
        }
        /* Malformed TeX remains visible as a literal command and later groups. */
        if (parser->status != TRYTR_MATH_NOTATION_OK) return NULL;
        parser->position = command_end;
        return new_node(parser, TRYTR_MATH_NODE_COMMAND, start, command_end);
    }

    if (command_is(parser, start, command_end, "sqrt")) {
        skip_space(parser);
        TrytrMathNode *radicand = parse_argument_group(parser, depth);
        if (radicand != NULL) {
            TrytrMathNode *root = new_node(
                parser, TRYTR_MATH_NODE_SQUARE_ROOT, start, parser->position);
            if (root == NULL) return NULL;
            root->first_child = radicand;
            root->child_count = 1u;
            return root;
        }
        if (parser->status != TRYTR_MATH_NOTATION_OK) return NULL;
        parser->position = command_end;
    }

    return new_node(parser, TRYTR_MATH_NODE_COMMAND, start, command_end);
}

static TrytrMathNode *parse_script_argument(MathParser *parser, size_t depth)
{
    skip_space(parser);
    if (parser->position < parser->length &&
        parser->source[parser->position] == '{') {
        return parse_group(parser, '}', depth);
    }
    return parse_atom(parser, depth + 1u, false);
}

static TrytrMathNode *parse_atom(MathParser *parser, size_t depth,
                                 bool consume_scripts)
{
    skip_space(parser);
    if (parser->position >= parser->length) return NULL;
    if (depth >= TRYTR_MATH_NOTATION_DEPTH_LIMIT) {
        return raw_to_end(parser, parser->position);
    }

    const size_t start = parser->position;
    TrytrMathNode *node = NULL;
    const unsigned char byte = (unsigned char)parser->source[parser->position];
    if (starts_with(parser, parser->position, "\xE2\x88\x9A")) {
        parser->position += 3u;
        const size_t radicand_start = parser->position;
        TrytrMathNode *radicand = parse_atom(parser, depth + 1u, false);
        if (radicand != NULL) {
            node = new_node(parser, TRYTR_MATH_NODE_SQUARE_ROOT, start,
                            parser->position);
            if (node != NULL) {
                node->first_child = radicand;
                node->child_count = 1u;
            }
        } else if (parser->status == TRYTR_MATH_NOTATION_OK) {
            parser->position = radicand_start;
            node = new_node(parser, TRYTR_MATH_NODE_TEXT, start, parser->position);
        }
    } else if (parser->source[parser->position] == '\\') {
        node = parse_command(parser, depth);
    } else if (parser->source[parser->position] == '{') {
        node = parse_group(parser, '}', depth);
    } else if (parser->source[parser->position] == '(') {
        node = parse_group(parser, ')', depth);
    } else if (parser->source[parser->position] == '[') {
        node = parse_group(parser, ']', depth);
    } else if ((byte >= (unsigned char)'0' && byte <= (unsigned char)'9') ||
               (byte == (unsigned char)'.' && parser->position + 1u < parser->length &&
                parser->source[parser->position + 1u] >= '0' &&
                parser->source[parser->position + 1u] <= '9')) {
        bool decimal_seen = byte == (unsigned char)'.';
        ++parser->position;
        while (parser->position < parser->length) {
            const unsigned char next = (unsigned char)parser->source[parser->position];
            if (next >= (unsigned char)'0' && next <= (unsigned char)'9') {
                ++parser->position;
            } else if (next == (unsigned char)'.' && !decimal_seen) {
                decimal_seen = true;
                ++parser->position;
            }
            else break;
        }
        if (parser->position < parser->length &&
            (parser->source[parser->position] == 'e' ||
             parser->source[parser->position] == 'E')) {
            size_t exponent = parser->position + 1u;
            if (exponent < parser->length &&
                (parser->source[exponent] == '+' || parser->source[exponent] == '-')) {
                ++exponent;
            }
            const size_t first_digit = exponent;
            while (exponent < parser->length && parser->source[exponent] >= '0' &&
                   parser->source[exponent] <= '9') ++exponent;
            if (exponent != first_digit) parser->position = exponent;
        }
        node = new_node(parser, TRYTR_MATH_NODE_NUMBER, start, parser->position);
    } else if (is_identifier_byte(byte) && byte != (unsigned char)'_' &&
               !starts_unicode_math_operator(parser, parser->position)) {
        ++parser->position;
        while (parser->position < parser->length &&
               is_identifier_byte((unsigned char)parser->source[parser->position]) &&
               parser->source[parser->position] != '_' &&
               !starts_unicode_math_operator(parser, parser->position)) {
            ++parser->position;
        }
        node = new_node(parser, TRYTR_MATH_NODE_IDENTIFIER, start, parser->position);
    } else {
        ++parser->position;
        node = new_node(parser, TRYTR_MATH_NODE_TEXT, start, parser->position);
    }
    if (node == NULL) return NULL;

    while (consume_scripts && parser->position < parser->length) {
        skip_space(parser);
        if (parser->position >= parser->length) break;
        const char script = parser->source[parser->position];
        if (script != '_' && script != '^') break;
        const size_t operator_position = parser->position++;
        TrytrMathNode *argument = parse_script_argument(parser, depth + 1u);
        if (argument == NULL) {
            if (parser->status != TRYTR_MATH_NOTATION_OK) return NULL;
            parser->position = operator_position;
            break;
        }
        TrytrMathNode *script_node = new_node(
            parser,
            script == '_' ? TRYTR_MATH_NODE_SUBSCRIPT : TRYTR_MATH_NODE_SUPERSCRIPT,
            start, parser->position);
        if (script_node == NULL) return NULL;
        script_node->operator_offset = operator_position;
        script_node->operator_length = 1u;
        TrytrMathNode *tail = NULL;
        node_append(script_node, node, &tail);
        node_append(script_node, argument, &tail);
        node = script_node;
    }
    return node;
}

typedef struct MathOperator {
    const char *text;
    unsigned int precedence;
    TrytrMathNodeKind kind;
} MathOperator;

static const MathOperator operators[] = {
    {"\\approx", 1u, TRYTR_MATH_NODE_RELATION},
    {"\\ne", 1u, TRYTR_MATH_NODE_RELATION},
    {"\\neq", 1u, TRYTR_MATH_NODE_RELATION},
    {"\\leq", 1u, TRYTR_MATH_NODE_RELATION},
    {"\\le", 1u, TRYTR_MATH_NODE_RELATION},
    {"\\geq", 1u, TRYTR_MATH_NODE_RELATION},
    {"\\ge", 1u, TRYTR_MATH_NODE_RELATION},
    {"\\in", 1u, TRYTR_MATH_NODE_RELATION},
    {"\\to", 1u, TRYTR_MATH_NODE_RELATION},
    {"\\cdot", 3u, TRYTR_MATH_NODE_BINARY},
    {"\\times", 3u, TRYTR_MATH_NODE_BINARY},
    {"\\div", 3u, TRYTR_MATH_NODE_BINARY},
    {"\xE2\x89\x88", 1u, TRYTR_MATH_NODE_RELATION},
    {"\xE2\x89\xA4", 1u, TRYTR_MATH_NODE_RELATION},
    {"\xE2\x89\xA5", 1u, TRYTR_MATH_NODE_RELATION},
    {"\xE2\x89\xA0", 1u, TRYTR_MATH_NODE_RELATION},
    {"\xE2\x88\x88", 1u, TRYTR_MATH_NODE_RELATION},
    {"\xE2\x86\x92", 1u, TRYTR_MATH_NODE_RELATION},
    {"\xC2\xB7", 3u, TRYTR_MATH_NODE_BINARY},
    {"\xC3\x97", 3u, TRYTR_MATH_NODE_BINARY},
    {"\xC3\xB7", 3u, TRYTR_MATH_NODE_BINARY},
    {"<=", 1u, TRYTR_MATH_NODE_RELATION},
    {">=", 1u, TRYTR_MATH_NODE_RELATION},
    {"!=", 1u, TRYTR_MATH_NODE_RELATION},
    {"=", 1u, TRYTR_MATH_NODE_RELATION},
    {"<", 1u, TRYTR_MATH_NODE_RELATION},
    {">", 1u, TRYTR_MATH_NODE_RELATION},
    {"+", 2u, TRYTR_MATH_NODE_BINARY},
    {"-", 2u, TRYTR_MATH_NODE_BINARY},
    {"*", 3u, TRYTR_MATH_NODE_BINARY},
    {"/", 3u, TRYTR_MATH_NODE_BINARY}
};

static const MathOperator *operator_at(const MathParser *parser,
                                       size_t *out_length)
{
    for (size_t index = 0u; index < sizeof(operators) / sizeof(operators[0]); ++index) {
        if (starts_with(parser, parser->position, operators[index].text)) {
            const size_t length = strlen(operators[index].text);
            if (operators[index].text[0] == '\\' &&
                parser->position + length < parser->length &&
                is_ascii_letter((unsigned char)parser->source[parser->position + length])) {
                continue;
            }
            *out_length = length;
            return &operators[index];
        }
    }
    return NULL;
}

static TrytrMathNode *parse_prefix(MathParser *parser, size_t depth)
{
    skip_space(parser);
    if (parser->position < parser->length &&
        (parser->source[parser->position] == '+' ||
         parser->source[parser->position] == '-')) {
        const size_t start = parser->position;
        const size_t operator_position = parser->position++;
        TrytrMathNode *operand = parse_expression(parser, 4u, depth + 1u);
        if (operand == NULL) return NULL;
        TrytrMathNode *unary = new_node(parser, TRYTR_MATH_NODE_UNARY,
                                        start, parser->position);
        if (unary == NULL) return NULL;
        unary->operator_offset = operator_position;
        unary->operator_length = 1u;
        unary->first_child = operand;
        unary->child_count = 1u;
        return unary;
    }
    return parse_atom(parser, depth, true);
}

static TrytrMathNode *parse_expression(MathParser *parser, unsigned int minimum,
                                       size_t depth)
{
    skip_space(parser);
    if (depth >= TRYTR_MATH_NOTATION_DEPTH_LIMIT) {
        return raw_to_end(parser, parser->position);
    }
    TrytrMathNode *left = parse_prefix(parser, depth);
    if (left == NULL) return NULL;
    while (parser->position < parser->length) {
        skip_space(parser);
        const size_t operator_position = parser->position;
        size_t operator_length = 0u;
        const MathOperator *operator = operator_at(parser, &operator_length);
        if (operator == NULL || operator->precedence < minimum) {
            break;
        }
        parser->position += operator_length;
        TrytrMathNode *right = parse_expression(
            parser, operator->precedence + 1u, depth + 1u);
        if (right == NULL) {
            if (parser->status != TRYTR_MATH_NOTATION_OK) return NULL;
            parser->position = operator_position;
            break;
        }
        TrytrMathNode *combined = new_node(
            parser, operator->kind, left->source_offset, parser->position);
        if (combined == NULL) return NULL;
        combined->operator_offset = operator_position;
        combined->operator_length = operator_length;
        TrytrMathNode *tail = NULL;
        node_append(combined, left, &tail);
        node_append(combined, right, &tail);
        left = combined;
    }
    return left;
}

static TrytrMathNode *parse_row(MathParser *parser, char terminator,
                                size_t depth)
{
    const size_t start = parser->position;
    TrytrMathNode *row = new_node(parser, TRYTR_MATH_NODE_ROW, start, start);
    if (row == NULL) return NULL;
    TrytrMathNode *tail = NULL;
    while (parser->position < parser->length &&
           parser->status == TRYTR_MATH_NOTATION_OK) {
        skip_space(parser);
        if (parser->position >= parser->length ||
            (terminator != '\0' && parser->source[parser->position] == terminator)) break;
        const size_t previous = parser->position;
        TrytrMathNode *expression = parse_expression(parser, 1u, depth + 1u);
        if (expression == NULL) {
            if (parser->status == TRYTR_MATH_NOTATION_OK) {
                parser->position = previous;
                TrytrMathNode *remainder = raw_to_end(parser, previous);
                if (remainder != NULL) node_append(row, remainder, &tail);
            }
            break;
        }
        node_append(row, expression, &tail);
        if (parser->position <= previous) {
            /* Defensive progress guard for malformed future token additions. */
            ++parser->position;
        }
    }
    row->source_length = parser->position - start;
    return row;
}

TrytrMathNotationStatus trytr_math_notation_parse(
    TrytrArena *arena,
    const char *source,
    size_t length,
    const TrytrMathNode **out_root
)
{
    if (out_root != NULL) *out_root = NULL;
    if (arena == NULL || out_root == NULL || (source == NULL && length != 0u)) {
        return TRYTR_MATH_NOTATION_NO_MEMORY;
    }
    MathParser parser = {
        .arena = arena,
        .source = source == NULL ? "" : source,
        .length = length,
        .status = TRYTR_MATH_NOTATION_OK
    };
    TrytrMathNode *root = parse_row(&parser, '\0', 0u);
    if (parser.status == TRYTR_MATH_NOTATION_LIMIT_EXCEEDED && root != NULL) {
        /* A bounded parser must still accept long notation as source text. */
        root->kind = TRYTR_MATH_NODE_RAW;
        root->source_offset = 0u;
        root->source_length = length;
        root->operator_offset = 0u;
        root->operator_length = 0u;
        root->child_count = 0u;
        root->first_child = NULL;
        parser.status = TRYTR_MATH_NOTATION_OK;
    }
    if (parser.status != TRYTR_MATH_NOTATION_OK) return parser.status;
    if (root == NULL) return TRYTR_MATH_NOTATION_NO_MEMORY;
    *out_root = root;
    return TRYTR_MATH_NOTATION_OK;
}
