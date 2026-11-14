#include "expression_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EXPRESSION_MAX_DEPTH 64u
#define EXPRESSION_MAX_BYTES 4096u
#define EXPRESSION_MAX_NUMBER_BYTES 96u

typedef struct ExpressionCompiler {
    const char *source;
    size_t length;
    size_t cursor;
    size_t instruction_count;
    size_t error_offset;
    ExpressionInstruction instructions[EXPRESSION_MAX_INSTRUCTIONS];
    TrytrMathExpressionStatus status;
} ExpressionCompiler;

typedef struct ExpressionFunction {
    const char *name;
    size_t name_length;
    size_t arity;
    ExpressionOpcode opcode;
} ExpressionFunction;

static const ExpressionFunction functions[] = {
    {"sqrt", 4u, 1u, EXPRESSION_SQRT},
    {"abs", 3u, 1u, EXPRESSION_ABS},
    {"sin", 3u, 1u, EXPRESSION_SIN},
    {"cos", 3u, 1u, EXPRESSION_COS},
    {"tan", 3u, 1u, EXPRESSION_TAN},
    {"asin", 4u, 1u, EXPRESSION_ASIN},
    {"acos", 4u, 1u, EXPRESSION_ACOS},
    {"atan", 4u, 1u, EXPRESSION_ATAN},
    {"exp", 3u, 1u, EXPRESSION_EXP},
    {"log", 3u, 1u, EXPRESSION_LOG},
    {"log10", 5u, 1u, EXPRESSION_LOG10},
    {"sinh", 4u, 1u, EXPRESSION_SINH},
    {"cosh", 4u, 1u, EXPRESSION_COSH},
    {"tanh", 4u, 1u, EXPRESSION_TANH},
    {"pow", 3u, 2u, EXPRESSION_POWER},
    {"atan2", 5u, 2u, EXPRESSION_ATAN2},
    {"hypot", 5u, 2u, EXPRESSION_HYPOT},
    {"min", 3u, 2u, EXPRESSION_MIN},
    {"max", 3u, 2u, EXPRESSION_MAX}
};

static void skip_space(ExpressionCompiler *compiler)
{
    while (compiler->cursor < compiler->length) {
        const char value = compiler->source[compiler->cursor];
        if (value != ' ' && value != '\t' && value != '\n' && value != '\r') break;
        ++compiler->cursor;
    }
}

static void fail(ExpressionCompiler *compiler, TrytrMathExpressionStatus status,
                 size_t offset)
{
    if (compiler->status != TRYTR_MATH_EXPRESSION_OK) return;
    compiler->status = status;
    compiler->error_offset = offset > compiler->length ? compiler->length : offset;
}

static bool emit(ExpressionCompiler *compiler, ExpressionOpcode opcode,
                 double constant)
{
    if (compiler->instruction_count >= EXPRESSION_MAX_INSTRUCTIONS) {
        fail(compiler, TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED,
             compiler->cursor);
        return false;
    }
    compiler->instructions[compiler->instruction_count++] =
        (ExpressionInstruction){.opcode = opcode, .constant = constant};
    return true;
}

static bool identifier_start(char value)
{
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') || value == '_';
}

static bool identifier_continue(char value)
{
    return identifier_start(value) || (value >= '0' && value <= '9');
}

static bool parse_sum(ExpressionCompiler *compiler, size_t depth);
static bool parse_unary(ExpressionCompiler *compiler, size_t depth);

static const ExpressionFunction *find_function(const char *name, size_t length)
{
    for (size_t index = 0u; index < sizeof(functions) / sizeof(functions[0]); ++index) {
        if (functions[index].name_length == length &&
            memcmp(functions[index].name, name, length) == 0) return &functions[index];
    }
    return NULL;
}

static bool parse_number(ExpressionCompiler *compiler)
{
    const size_t start = compiler->cursor;
    bool has_digit = false;
    while (compiler->cursor < compiler->length &&
           compiler->source[compiler->cursor] >= '0' &&
           compiler->source[compiler->cursor] <= '9') {
        has_digit = true;
        ++compiler->cursor;
    }
    if (compiler->cursor < compiler->length &&
        compiler->source[compiler->cursor] == '.') {
        ++compiler->cursor;
        while (compiler->cursor < compiler->length &&
               compiler->source[compiler->cursor] >= '0' &&
               compiler->source[compiler->cursor] <= '9') {
            has_digit = true;
            ++compiler->cursor;
        }
    }
    if (!has_digit) {
        fail(compiler, TRYTR_MATH_EXPRESSION_SYNTAX, start);
        return false;
    }
    if (compiler->cursor < compiler->length &&
        (compiler->source[compiler->cursor] == 'e' ||
         compiler->source[compiler->cursor] == 'E')) {
        ++compiler->cursor;
        if (compiler->cursor < compiler->length &&
            (compiler->source[compiler->cursor] == '+' ||
             compiler->source[compiler->cursor] == '-')) ++compiler->cursor;
        const size_t exponent_start = compiler->cursor;
        while (compiler->cursor < compiler->length &&
               compiler->source[compiler->cursor] >= '0' &&
               compiler->source[compiler->cursor] <= '9') ++compiler->cursor;
        if (compiler->cursor == exponent_start) {
            fail(compiler, TRYTR_MATH_EXPRESSION_SYNTAX, start);
            return false;
        }
    }
    const size_t number_length = compiler->cursor - start;
    if (number_length >= EXPRESSION_MAX_NUMBER_BYTES) {
        fail(compiler, TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED, start);
        return false;
    }
    char number[EXPRESSION_MAX_NUMBER_BYTES];
    memcpy(number, compiler->source + start, number_length);
    number[number_length] = '\0';
    char *end = NULL;
    const double value = strtod(number, &end);
    if (end == number || *end != '\0' || !isfinite(value)) {
        fail(compiler, TRYTR_MATH_EXPRESSION_SYNTAX, start);
        return false;
    }
    return emit(compiler, EXPRESSION_PUSH_CONSTANT, value);
}

static bool parse_identifier(ExpressionCompiler *compiler, size_t depth)
{
    const size_t start = compiler->cursor++;
    while (compiler->cursor < compiler->length &&
           identifier_continue(compiler->source[compiler->cursor])) ++compiler->cursor;
    const size_t length = compiler->cursor - start;
    const char *name = compiler->source + start;
    if (length == 1u && name[0] == 'x')
        return emit(compiler, EXPRESSION_PUSH_X, 0.0);
    if (length == 2u && memcmp(name, "pi", 2u) == 0)
        return emit(compiler, EXPRESSION_PUSH_CONSTANT, TRYTR_REAL_PI);
    if (length == 1u && name[0] == 'e')
        return emit(compiler, EXPRESSION_PUSH_CONSTANT, TRYTR_REAL_E);

    const ExpressionFunction *function = find_function(name, length);
    if (function == NULL) {
        fail(compiler, TRYTR_MATH_EXPRESSION_UNKNOWN_NAME, start);
        return false;
    }
    skip_space(compiler);
    if (compiler->cursor >= compiler->length ||
        compiler->source[compiler->cursor] != '(') {
        fail(compiler, TRYTR_MATH_EXPRESSION_SYNTAX, compiler->cursor);
        return false;
    }
    ++compiler->cursor;
    for (size_t argument = 0u; argument < function->arity; ++argument) {
        if (!parse_sum(compiler, depth + 1u)) return false;
        skip_space(compiler);
        if (argument + 1u < function->arity) {
            if (compiler->cursor >= compiler->length ||
                compiler->source[compiler->cursor] != ',') {
                fail(compiler, TRYTR_MATH_EXPRESSION_FUNCTION_ARITY,
                     compiler->cursor);
                return false;
            }
            ++compiler->cursor;
        }
    }
    skip_space(compiler);
    if (compiler->cursor >= compiler->length ||
        compiler->source[compiler->cursor] != ')') {
        fail(compiler, TRYTR_MATH_EXPRESSION_FUNCTION_ARITY,
             compiler->cursor);
        return false;
    }
    ++compiler->cursor;
    return emit(compiler, function->opcode, 0.0);
}

static bool parse_primary(ExpressionCompiler *compiler, size_t depth)
{
    if (depth > EXPRESSION_MAX_DEPTH) {
        fail(compiler, TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED,
             compiler->cursor);
        return false;
    }
    skip_space(compiler);
    if (compiler->cursor >= compiler->length) {
        fail(compiler, TRYTR_MATH_EXPRESSION_SYNTAX, compiler->cursor);
        return false;
    }
    const char value = compiler->source[compiler->cursor];
    if ((value >= '0' && value <= '9') || value == '.')
        return parse_number(compiler);
    if (identifier_start(value)) return parse_identifier(compiler, depth);
    if (value == '(') {
        ++compiler->cursor;
        if (!parse_sum(compiler, depth + 1u)) return false;
        skip_space(compiler);
        if (compiler->cursor >= compiler->length ||
            compiler->source[compiler->cursor] != ')') {
            fail(compiler, TRYTR_MATH_EXPRESSION_SYNTAX, compiler->cursor);
            return false;
        }
        ++compiler->cursor;
        return true;
    }
    fail(compiler, TRYTR_MATH_EXPRESSION_SYNTAX, compiler->cursor);
    return false;
}

static bool parse_power(ExpressionCompiler *compiler, size_t depth)
{
    if (depth > EXPRESSION_MAX_DEPTH) {
        fail(compiler, TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED,
             compiler->cursor);
        return false;
    }
    if (!parse_primary(compiler, depth)) return false;
    skip_space(compiler);
    if (compiler->cursor < compiler->length &&
        compiler->source[compiler->cursor] == '^') {
        ++compiler->cursor;
        if (!parse_unary(compiler, depth + 1u)) return false;
        return emit(compiler, EXPRESSION_POWER, 0.0);
    }
    return true;
}

static bool parse_unary(ExpressionCompiler *compiler, size_t depth)
{
    if (depth > EXPRESSION_MAX_DEPTH) {
        fail(compiler, TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED,
             compiler->cursor);
        return false;
    }
    skip_space(compiler);
    if (compiler->cursor < compiler->length &&
        (compiler->source[compiler->cursor] == '+' ||
         compiler->source[compiler->cursor] == '-')) {
        const char sign = compiler->source[compiler->cursor++];
        if (!parse_unary(compiler, depth + 1u)) return false;
        return sign == '+' || emit(compiler, EXPRESSION_NEGATE, 0.0);
    }
    return parse_power(compiler, depth);
}

static bool parse_product(ExpressionCompiler *compiler, size_t depth)
{
    if (!parse_unary(compiler, depth)) return false;
    for (;;) {
        skip_space(compiler);
        if (compiler->cursor >= compiler->length) return true;
        const char operation = compiler->source[compiler->cursor];
        if (operation != '*' && operation != '/') return true;
        ++compiler->cursor;
        if (!parse_unary(compiler, depth)) return false;
        if (!emit(compiler, operation == '*' ? EXPRESSION_MULTIPLY
                                             : EXPRESSION_DIVIDE, 0.0)) return false;
    }
}

static bool parse_sum(ExpressionCompiler *compiler, size_t depth)
{
    if (!parse_product(compiler, depth)) return false;
    for (;;) {
        skip_space(compiler);
        if (compiler->cursor >= compiler->length) return true;
        const char operation = compiler->source[compiler->cursor];
        if (operation != '+' && operation != '-') return true;
        ++compiler->cursor;
        if (!parse_product(compiler, depth)) return false;
        if (!emit(compiler, operation == '+' ? EXPRESSION_ADD
                                             : EXPRESSION_SUBTRACT, 0.0)) return false;
    }
}

TrytrMathExpressionStatus trytr_math_expression_compile(
    const char *source, size_t length, TrytrMathExpression **out_expression,
    size_t *out_error_offset)
{
    if (out_expression != NULL) *out_expression = NULL;
    if (out_error_offset != NULL) *out_error_offset = 0u;
    if (source == NULL || out_expression == NULL || out_error_offset == NULL)
        return TRYTR_MATH_EXPRESSION_INVALID_ARGUMENT;
    if (length == 0u) return TRYTR_MATH_EXPRESSION_SYNTAX;
    if (length > EXPRESSION_MAX_BYTES) {
        *out_error_offset = EXPRESSION_MAX_BYTES;
        return TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED;
    }

    ExpressionCompiler compiler = {
        .source = source,
        .length = length,
        .status = TRYTR_MATH_EXPRESSION_OK
    };
    const bool parsed = parse_sum(&compiler, 0u);
    skip_space(&compiler);
    if (parsed && compiler.cursor != compiler.length)
        fail(&compiler, TRYTR_MATH_EXPRESSION_SYNTAX, compiler.cursor);
    if (!parsed || compiler.status != TRYTR_MATH_EXPRESSION_OK) {
        *out_error_offset = compiler.error_offset;
        return compiler.status == TRYTR_MATH_EXPRESSION_OK
            ? TRYTR_MATH_EXPRESSION_SYNTAX : compiler.status;
    }

    const size_t bytes = sizeof(TrytrMathExpression) +
                         compiler.instruction_count * sizeof(ExpressionInstruction);
    TrytrMathExpression *expression = malloc(bytes);
    if (expression == NULL) return TRYTR_MATH_EXPRESSION_NO_MEMORY;
    expression->instruction_count = compiler.instruction_count;
    memcpy(expression->instructions, compiler.instructions,
           compiler.instruction_count * sizeof(ExpressionInstruction));
    *out_expression = expression;
    return TRYTR_MATH_EXPRESSION_OK;
}

const char *trytr_math_expression_status_string(TrytrMathExpressionStatus status)
{
    switch (status) {
        case TRYTR_MATH_EXPRESSION_OK: return "ok";
        case TRYTR_MATH_EXPRESSION_INVALID_ARGUMENT: return "invalid argument";
        case TRYTR_MATH_EXPRESSION_SYNTAX: return "invalid expression syntax";
        case TRYTR_MATH_EXPRESSION_UNKNOWN_NAME: return "unknown name";
        case TRYTR_MATH_EXPRESSION_FUNCTION_ARITY: return "incorrect function arguments";
        case TRYTR_MATH_EXPRESSION_LIMIT_EXCEEDED: return "expression limit exceeded";
        case TRYTR_MATH_EXPRESSION_NO_MEMORY: return "out of memory";
    }
    return "unknown expression status";
}

void trytr_math_expression_destroy(TrytrMathExpression *expression)
{
    free(expression);
}
