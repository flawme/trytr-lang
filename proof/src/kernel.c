#include "trytr/proof.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define PROOF_MAX_SOURCE 65536U
#define PROOF_MAX_NODES 4096U
#define PROOF_MAX_DEPTH 128U
#define PROOF_MAX_STEPS 256U
#define NODE_NONE ((size_t)-1)

typedef enum TermKind {
    TERM_ZERO,
    TERM_SUCC,
    TERM_ADD
} TermKind;

typedef struct Term {
    TermKind kind;
    size_t left;
    size_t right;
} Term;

typedef struct Parser {
    const char *source;
    size_t length;
    size_t cursor;
    Term nodes[PROOF_MAX_NODES];
    size_t node_count;
    char error[160];
} Parser;

typedef enum RewriteRule {
    REWRITE_ADD_ZERO,
    REWRITE_ADD_SUCC
} RewriteRule;

static void set_error(Parser *parser, const char *message)
{
    if (parser->error[0] == '\0') {
        (void)snprintf(parser->error, sizeof(parser->error), "%s", message);
    }
}

static void skip_space(Parser *parser)
{
    while (parser->cursor < parser->length &&
           isspace((unsigned char)parser->source[parser->cursor]) != 0) {
        ++parser->cursor;
    }
}

static bool is_identifier_start(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') || character == '_';
}

static bool is_identifier_continue(char character)
{
    return is_identifier_start(character) ||
           (character >= '0' && character <= '9');
}

static bool consume_literal(Parser *parser, const char *literal)
{
    const size_t length = strlen(literal);
    skip_space(parser);
    if (length > parser->length - parser->cursor ||
        memcmp(parser->source + parser->cursor, literal, length) != 0) {
        return false;
    }
    parser->cursor += length;
    return true;
}

static bool consume_keyword(Parser *parser, const char *keyword)
{
    const size_t start = parser->cursor;
    if (!consume_literal(parser, keyword)) {
        return false;
    }
    if (parser->cursor < parser->length &&
        is_identifier_continue(parser->source[parser->cursor])) {
        parser->cursor = start;
        return false;
    }
    return true;
}

static bool consume_character(Parser *parser, char character)
{
    skip_space(parser);
    if (parser->cursor >= parser->length ||
        parser->source[parser->cursor] != character) {
        return false;
    }
    ++parser->cursor;
    return true;
}

static bool parse_identifier(Parser *parser)
{
    skip_space(parser);
    if (parser->cursor >= parser->length ||
        !is_identifier_start(parser->source[parser->cursor])) {
        return false;
    }
    ++parser->cursor;
    while (parser->cursor < parser->length &&
           is_identifier_continue(parser->source[parser->cursor])) {
        ++parser->cursor;
    }
    return true;
}

static size_t add_node(Parser *parser, TermKind kind, size_t left, size_t right)
{
    if (parser->node_count >= PROOF_MAX_NODES) {
        set_error(parser, "proof exceeds the term node limit");
        return NODE_NONE;
    }
    const size_t index = parser->node_count++;
    parser->nodes[index] = (Term){.kind = kind, .left = left, .right = right};
    return index;
}

static size_t parse_term(Parser *parser, size_t depth)
{
    if (depth > PROOF_MAX_DEPTH) {
        set_error(parser, "term exceeds the nesting limit");
        return NODE_NONE;
    }
    skip_space(parser);
    if (consume_literal(parser, "0")) {
        return add_node(parser, TERM_ZERO, NODE_NONE, NODE_NONE);
    }
    if (consume_keyword(parser, "succ")) {
        if (!consume_character(parser, '(')) {
            set_error(parser, "expected '(' after succ");
            return NODE_NONE;
        }
        const size_t child = parse_term(parser, depth + 1U);
        if (child == NODE_NONE || !consume_character(parser, ')')) {
            set_error(parser, "expected ')' after successor term");
            return NODE_NONE;
        }
        return add_node(parser, TERM_SUCC, child, NODE_NONE);
    }
    if (consume_keyword(parser, "add")) {
        if (!consume_character(parser, '(')) {
            set_error(parser, "expected '(' after add");
            return NODE_NONE;
        }
        const size_t left = parse_term(parser, depth + 1U);
        if (left == NODE_NONE || !consume_character(parser, ',')) {
            set_error(parser, "expected ',' between addition operands");
            return NODE_NONE;
        }
        const size_t right = parse_term(parser, depth + 1U);
        if (right == NODE_NONE || !consume_character(parser, ')')) {
            set_error(parser, "expected ')' after addition operands");
            return NODE_NONE;
        }
        return add_node(parser, TERM_ADD, left, right);
    }
    set_error(parser, "expected a natural-number term (0, succ(...), or add(...,...))");
    return NODE_NONE;
}

static bool rewrite_at(Parser *parser, size_t *root, RewriteRule rule, size_t depth)
{
    if (depth > PROOF_MAX_DEPTH || *root == NODE_NONE) {
        set_error(parser, "rewrite traversal exceeded the nesting limit");
        return false;
    }

    Term *node = &parser->nodes[*root];
    if (node->kind == TERM_ADD) {
        const Term right = parser->nodes[node->right];
        if (rule == REWRITE_ADD_ZERO && right.kind == TERM_ZERO) {
            *root = node->left;
            return true;
        }
        if (rule == REWRITE_ADD_SUCC && right.kind == TERM_SUCC) {
            const size_t replacement = add_node(parser, TERM_ADD,
                                                node->left, right.left);
            if (replacement == NODE_NONE) {
                return false;
            }
            node = &parser->nodes[*root];
            node->kind = TERM_SUCC;
            node->left = replacement;
            node->right = NODE_NONE;
            return true;
        }
    }

    node = &parser->nodes[*root];
    if (node->kind == TERM_SUCC) {
        return rewrite_at(parser, &node->left, rule, depth + 1U);
    }
    if (node->kind == TERM_ADD) {
        if (rewrite_at(parser, &node->left, rule, depth + 1U)) {
            return true;
        }
        if (parser->error[0] != '\0') {
            return false;
        }
        node = &parser->nodes[*root];
        return rewrite_at(parser, &node->right, rule, depth + 1U);
    }
    return false;
}

static bool terms_equal(const Parser *parser, size_t left, size_t right, size_t depth)
{
    if (depth > PROOF_MAX_DEPTH || left == NODE_NONE || right == NODE_NONE) {
        return false;
    }
    const Term a = parser->nodes[left];
    const Term b = parser->nodes[right];
    if (a.kind != b.kind) {
        return false;
    }
    if (a.kind == TERM_ZERO) {
        return true;
    }
    if (!terms_equal(parser, a.left, b.left, depth + 1U)) {
        return false;
    }
    return a.kind != TERM_ADD || terms_equal(parser, a.right, b.right, depth + 1U);
}

static bool parse_rewrite_rule(Parser *parser, RewriteRule *rule)
{
    if (consume_keyword(parser, "add_zero")) {
        *rule = REWRITE_ADD_ZERO;
        return true;
    }
    if (consume_keyword(parser, "add_succ")) {
        *rule = REWRITE_ADD_SUCC;
        return true;
    }
    set_error(parser, "unknown rewrite rule; available rules are add_zero and add_succ");
    return false;
}

TrytrProofResult trytr_proof_check(const char *source)
{
    Parser parser = {0};
    if (source == NULL) {
        (void)snprintf(parser.error, sizeof(parser.error), "proof source is null");
        return (TrytrProofResult){.valid = false, .offset = 0U,
                                  .message = "proof source is null"};
    }
    parser.source = source;
    parser.length = strnlen(source, PROOF_MAX_SOURCE + 1U);
    if (parser.length > PROOF_MAX_SOURCE) {
        (void)snprintf(parser.error, sizeof(parser.error), "proof source exceeds 64 KiB");
    }
    if (parser.error[0] == '\0' && !consume_keyword(&parser, "theorem")) {
        set_error(&parser, "expected 'theorem'");
    }
    if (parser.error[0] == '\0' && !parse_identifier(&parser)) {
        set_error(&parser, "expected theorem name");
    }
    if (parser.error[0] == '\0' && !consume_character(&parser, ':')) {
        set_error(&parser, "expected ':' after theorem name");
    }
    size_t current = NODE_NONE;
    size_t goal = NODE_NONE;
    if (parser.error[0] == '\0') {
        current = parse_term(&parser, 0U);
    }
    if (parser.error[0] == '\0' && !consume_character(&parser, '=')) {
        set_error(&parser, "expected '=' in theorem statement");
    }
    if (parser.error[0] == '\0') {
        goal = parse_term(&parser, 0U);
    }
    if (parser.error[0] == '\0' && !consume_character(&parser, '{')) {
        set_error(&parser, "expected '{' before proof steps");
    }

    size_t steps = 0U;
    while (parser.error[0] == '\0') {
        skip_space(&parser);
        if (consume_character(&parser, '}')) {
            break;
        }
        if (!consume_keyword(&parser, "rewrite")) {
            set_error(&parser, "expected 'rewrite' or '}' in proof body");
            break;
        }
        if (++steps > PROOF_MAX_STEPS) {
            set_error(&parser, "proof exceeds the step limit");
            break;
        }
        RewriteRule rule;
        if (!parse_rewrite_rule(&parser, &rule)) {
            break;
        }
        if (!rewrite_at(&parser, &current, rule, 0U)) {
            if (parser.error[0] == '\0') {
                set_error(&parser, "rewrite rule does not match any subterm");
            }
            break;
        }
        if (!consume_character(&parser, ';')) {
            set_error(&parser, "expected ';' after rewrite step");
            break;
        }
    }
    if (parser.error[0] == '\0') {
        skip_space(&parser);
        if (parser.cursor != parser.length) {
            set_error(&parser, "unexpected content after theorem proof");
        }
    }
    if (parser.error[0] == '\0' && !terms_equal(&parser, current, goal, 0U)) {
        set_error(&parser, "proof steps do not reduce the left side to the stated right side");
    }

    TrytrProofResult result = {.valid = parser.error[0] == '\0',
                               .offset = parser.cursor,
                               .message = {0}};
    (void)snprintf(result.message, sizeof(result.message), "%s",
                   result.valid ? "proof checked" : parser.error);
    return result;
}
