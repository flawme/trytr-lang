#include "trytr/limit.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LIMIT_MAX_SOURCE 65536U
#define LIMIT_MAX_DEGREE 16U
#define LIMIT_MAX_DEPTH 32U
#define LIMIT_MAX_LITERAL INT64_C(1000000000)

typedef struct Polynomial {
    int64_t coefficient[LIMIT_MAX_DEGREE + 1U];
} Polynomial;

typedef struct Parser {
    const char *source;
    size_t length;
    size_t cursor;
    char error[160];
} Parser;

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

static bool consume(Parser *parser, const char *literal)
{
    skip_space(parser);
    const size_t size = strlen(literal);
    if (size > parser->length - parser->cursor ||
        memcmp(parser->source + parser->cursor, literal, size) != 0) {
        return false;
    }
    parser->cursor += size;
    return true;
}

static bool checked_add(int64_t left, int64_t right, int64_t *result)
{
    if ((right > 0 && left > INT64_MAX - right) ||
        (right < 0 && left < INT64_MIN - right)) {
        return false;
    }
    *result = left + right;
    return true;
}

static bool checked_multiply(int64_t left, int64_t right, int64_t *result)
{
    if (left == 0 || right == 0) {
        *result = 0;
        return true;
    }
    if ((left == -1 && right == INT64_MIN) ||
        (right == -1 && left == INT64_MIN)) {
        return false;
    }
    if (left > 0) {
        if ((right > 0 && left > INT64_MAX / right) ||
            (right < 0 && right < INT64_MIN / left)) {
            return false;
        }
    } else if ((right > 0 && left < INT64_MIN / right) ||
               (right < 0 && left < INT64_MAX / right)) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool parse_unsigned(Parser *parser, uint64_t maximum, uint64_t *value)
{
    skip_space(parser);
    const size_t start = parser->cursor;
    uint64_t parsed = 0U;
    while (parser->cursor < parser->length) {
        const unsigned char character = (unsigned char)parser->source[parser->cursor];
        if (isdigit(character) == 0) {
            break;
        }
        const uint64_t digit = (uint64_t)(character - (unsigned char)'0');
        if (parsed > (maximum - digit) / 10U) {
            set_error(parser, "integer literal exceeds the supported limit");
            return false;
        }
        parsed = parsed * 10U + digit;
        ++parser->cursor;
    }
    if (parser->cursor == start) {
        return false;
    }
    *value = parsed;
    return true;
}

static bool parse_signed(Parser *parser, int64_t maximum, int64_t *value)
{
    skip_space(parser);
    const bool negative = parser->cursor < parser->length &&
                          parser->source[parser->cursor] == '-';
    if (negative) {
        ++parser->cursor;
    }
    uint64_t magnitude = 0U;
    if (!parse_unsigned(parser, (uint64_t)maximum, &magnitude)) {
        if (negative && parser->error[0] == '\0') {
            set_error(parser, "expected digits after minus sign");
        }
        return false;
    }
    *value = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    return true;
}

static bool consume_open_paren(Parser *parser)
{
    return consume(parser, "\\left(") || consume(parser, "(");
}

static bool consume_close_paren(Parser *parser)
{
    return consume(parser, "\\right)") || consume(parser, ")");
}

static Polynomial polynomial_constant(int64_t value)
{
    Polynomial result = {0};
    result.coefficient[0] = value;
    return result;
}

static bool polynomial_add(Parser *parser, Polynomial left, Polynomial right,
                           bool subtract, Polynomial *result)
{
    for (size_t index = 0U; index <= LIMIT_MAX_DEGREE; ++index) {
        if (subtract && right.coefficient[index] == INT64_MIN) {
            set_error(parser, "polynomial coefficient exceeds the supported range");
            return false;
        }
        const int64_t term = subtract ? -right.coefficient[index]
                                      : right.coefficient[index];
        if (!checked_add(left.coefficient[index], term, &result->coefficient[index])) {
            set_error(parser, "polynomial coefficient arithmetic overflow");
            return false;
        }
    }
    return true;
}

static bool polynomial_multiply(Parser *parser, Polynomial left, Polynomial right,
                                Polynomial *result)
{
    *result = polynomial_constant(0);
    for (size_t left_index = 0U; left_index <= LIMIT_MAX_DEGREE; ++left_index) {
        if (left.coefficient[left_index] == 0) {
            continue;
        }
        for (size_t right_index = 0U; right_index <= LIMIT_MAX_DEGREE; ++right_index) {
            if (right.coefficient[right_index] == 0) {
                continue;
            }
            if (left_index + right_index > LIMIT_MAX_DEGREE) {
                set_error(parser, "polynomial degree exceeds 16");
                return false;
            }
            int64_t product = 0;
            if (!checked_multiply(left.coefficient[left_index],
                                  right.coefficient[right_index], &product) ||
                !checked_add(result->coefficient[left_index + right_index],
                             product, &result->coefficient[left_index + right_index])) {
                set_error(parser, "polynomial coefficient arithmetic overflow");
                return false;
            }
        }
    }
    return true;
}

static bool parse_expression(Parser *parser, size_t depth, Polynomial *result);

static bool parse_primary(Parser *parser, size_t depth, Polynomial *result)
{
    if (depth > LIMIT_MAX_DEPTH) {
        set_error(parser, "polynomial exceeds the nesting limit");
        return false;
    }
    skip_space(parser);
    if (consume(parser, "x")) {
        *result = polynomial_constant(0);
        result->coefficient[1] = 1;
        return true;
    }
    if (consume_open_paren(parser)) {
        if (!parse_expression(parser, depth + 1U, result) ||
            !consume_close_paren(parser)) {
            set_error(parser, "expected ')' after polynomial expression");
            return false;
        }
        return true;
    }
    uint64_t integer = 0U;
    if (parse_unsigned(parser, (uint64_t)LIMIT_MAX_LITERAL, &integer)) {
        *result = polynomial_constant((int64_t)integer);
        return true;
    }
    if (parser->error[0] == '\0') {
        set_error(parser, "expected x, an integer, or a parenthesized polynomial");
    }
    return false;
}

static bool parse_unary(Parser *parser, size_t depth, Polynomial *result)
{
    if (consume(parser, "+")) {
        return parse_unary(parser, depth + 1U, result);
    }
    if (consume(parser, "-")) {
        if (!parse_unary(parser, depth + 1U, result)) {
            return false;
        }
        for (size_t index = 0U; index <= LIMIT_MAX_DEGREE; ++index) {
            if (result->coefficient[index] == INT64_MIN) {
                set_error(parser, "polynomial coefficient exceeds the supported range");
                return false;
            }
            result->coefficient[index] = -result->coefficient[index];
        }
        return true;
    }
    if (!parse_primary(parser, depth, result)) {
        return false;
    }
    if (consume(parser, "^")) {
        const bool braced = consume(parser, "{");
        uint64_t exponent = 0U;
        if (!parse_unsigned(parser, LIMIT_MAX_DEGREE, &exponent) ||
            (braced && !consume(parser, "}"))) {
            set_error(parser, "expected an integer exponent from 0 to 16");
            return false;
        }
        Polynomial base = *result;
        Polynomial power = polynomial_constant(1);
        for (uint64_t count = 0U; count < exponent; ++count) {
            Polynomial product;
            if (!polynomial_multiply(parser, power, base, &product)) {
                return false;
            }
            power = product;
        }
        *result = power;
    }
    return true;
}

static bool starts_primary(Parser *parser)
{
    skip_space(parser);
    if (parser->cursor >= parser->length) {
        return false;
    }
    const char character = parser->source[parser->cursor];
    return isdigit((unsigned char)character) != 0 || character == 'x' ||
           character == '(' ||
           (character == '\\' &&
            parser->length - parser->cursor >= strlen("\\left(") &&
            memcmp(parser->source + parser->cursor, "\\left(", strlen("\\left(")) == 0);
}

static bool parse_term(Parser *parser, size_t depth, Polynomial *result)
{
    if (!parse_unary(parser, depth, result)) {
        return false;
    }
    for (;;) {
        const bool explicit_multiply = consume(parser, "*");
        if (!explicit_multiply && !starts_primary(parser)) {
            return true;
        }
        Polynomial right;
        if (!parse_unary(parser, depth, &right)) {
            return false;
        }
        Polynomial product;
        if (!polynomial_multiply(parser, *result, right, &product)) {
            return false;
        }
        *result = product;
    }
}

static bool parse_expression(Parser *parser, size_t depth, Polynomial *result)
{
    if (!parse_term(parser, depth, result)) {
        return false;
    }
    for (;;) {
        const bool subtract = consume(parser, "-");
        const bool add = !subtract && consume(parser, "+");
        if (!subtract && !add) {
            return true;
        }
        Polynomial right;
        if (!parse_term(parser, depth, &right) ||
            !polynomial_add(parser, *result, right, subtract, result)) {
            return false;
        }
    }
}

static bool evaluate_polynomial(Parser *parser, const Polynomial *polynomial,
                                int64_t x, int64_t *value)
{
    int64_t evaluated = polynomial->coefficient[LIMIT_MAX_DEGREE];
    for (size_t index = LIMIT_MAX_DEGREE; index > 0U; --index) {
        if (!checked_multiply(evaluated, x, &evaluated) ||
            !checked_add(evaluated, polynomial->coefficient[index - 1U], &evaluated)) {
            set_error(parser, "polynomial evaluation exceeds the supported range");
            return false;
        }
    }
    *value = evaluated;
    return true;
}

static bool absolute_coefficient(int64_t value, uint64_t *magnitude)
{
    if (value == INT64_MIN) {
        return false;
    }
    *magnitude = (uint64_t)(value < 0 ? -value : value);
    return true;
}

static bool compute_quotient_bound(Parser *parser, const Polynomial *polynomial,
                                   int64_t point, uint64_t *bound)
{
    int64_t quotient[LIMIT_MAX_DEGREE] = {0};
    int degree = (int)LIMIT_MAX_DEGREE;
    while (degree > 0 && polynomial->coefficient[(size_t)degree] == 0) {
        --degree;
    }
    if (degree == 0) {
        *bound = 0U;
        return true;
    }

    quotient[(size_t)degree - 1U] = polynomial->coefficient[(size_t)degree];
    for (int index = degree - 1; index > 0; --index) {
        int64_t product = 0;
        if (!checked_multiply(point, quotient[(size_t)index], &product) ||
            !checked_add(polynomial->coefficient[(size_t)index], product,
                         &quotient[(size_t)index - 1U])) {
            set_error(parser, "difference quotient exceeds the supported range");
            return false;
        }
    }

    const uint64_t radius = (uint64_t)(point < 0 ? -point : point) + 1U;
    uint64_t total = 0U;
    uint64_t radius_power = 1U;
    for (int index = 0; index < degree; ++index) {
        uint64_t magnitude = 0U;
        if (!absolute_coefficient(quotient[(size_t)index], &magnitude) ||
            (magnitude != 0U && radius_power > UINT64_MAX / magnitude)) {
            set_error(parser, "polynomial bound exceeds the supported range");
            return false;
        }
        const uint64_t term = magnitude * radius_power;
        if (term > UINT64_MAX - total) {
            set_error(parser, "polynomial bound exceeds the supported range");
            return false;
        }
        total += term;
        if (index + 1 < degree) {
            if (radius_power > UINT64_MAX / radius) {
                set_error(parser, "polynomial bound exceeds the supported range");
                return false;
            }
            radius_power *= radius;
        }
    }
    *bound = total;
    return true;
}

static bool parse_limit_header(Parser *parser, int64_t *point, Polynomial *polynomial,
                               int64_t *limit_value)
{
    if (!consume(parser, "\\lim") || !consume(parser, "_") ||
        !consume(parser, "{") || !consume(parser, "x") ||
        !consume(parser, "\\to")) {
        set_error(parser, "expected a limit of the form \\lim_{x\\to a}(p(x))=L");
        return false;
    }
    if (!parse_signed(parser, LIMIT_MAX_LITERAL, point) ||
        !consume(parser, "}") || !consume_open_paren(parser) ||
        !parse_expression(parser, 0U, polynomial) || !consume_close_paren(parser) ||
        !consume(parser, "=") || !parse_signed(parser, LIMIT_MAX_LITERAL, limit_value)) {
        if (parser->error[0] == '\0') {
            set_error(parser, "malformed polynomial limit statement");
        }
        return false;
    }
    return true;
}

TrytrProofResult trytr_limit_check(const char *source)
{
    Parser parser = {0};
    uint64_t required_bound = 0U;
    if (source == NULL) {
        return (TrytrProofResult){.valid = false, .offset = 0U,
                                  .message = "limit proof source is null"};
    }
    parser.source = source;
    parser.length = strnlen(source, LIMIT_MAX_SOURCE + 1U);
    if (parser.length > LIMIT_MAX_SOURCE) {
        return (TrytrProofResult){.valid = false, .offset = 0U,
                                  .message = "limit proof source exceeds 64 KiB"};
    }

    const bool has_environment = consume(&parser, "\\begin{limitproof}");
    int64_t point = 0;
    int64_t stated_limit = 0;
    Polynomial polynomial = {0};
    if (!parse_limit_header(&parser, &point, &polynomial, &stated_limit)) {
        goto done;
    }
    int64_t actual_limit = 0;
    if (!evaluate_polynomial(&parser, &polynomial, point, &actual_limit)) {
        goto done;
    }
    if (actual_limit != stated_limit) {
        set_error(&parser, "stated limit is not the polynomial's value at the approach point");
        goto done;
    }

    if (!consume(&parser, "\\delta") || !consume(&parser, "=") ||
        !consume(&parser, "\\min") || !consume_open_paren(&parser) ||
        !consume(&parser, "1") ||
        !consume(&parser, ",") || !consume(&parser, "\\frac") ||
        !consume(&parser, "{") ||
        !(consume(&parser, "\\varepsilon") || consume(&parser, "\\epsilon")) ||
        !consume(&parser, "}") || !consume(&parser, "{")) {
        set_error(&parser, "expected \\delta=\\min(1,\\frac{\\varepsilon}{C})");
        goto done;
    }
    uint64_t claimed_bound = 0U;
    if (!parse_unsigned(&parser, (uint64_t)INT64_MAX, &claimed_bound) ||
        !consume(&parser, "}") || !consume_close_paren(&parser)) {
        if (parser.error[0] == '\0') {
            set_error(&parser, "malformed delta choice; C must be a positive integer");
        }
        goto done;
    }
    if (claimed_bound == 0U) {
        set_error(&parser, "delta denominator C must be positive");
        goto done;
    }
    if (has_environment && !consume(&parser, "\\end{limitproof}")) {
        set_error(&parser, "missing \\end{limitproof}");
        goto done;
    }
    skip_space(&parser);
    if (parser.cursor != parser.length) {
        set_error(&parser, "unexpected content after the limit certificate");
        goto done;
    }

    if (!compute_quotient_bound(&parser, &polynomial, point, &required_bound)) {
        goto done;
    }
    if (claimed_bound < required_bound || claimed_bound == 0U) {
        set_error(&parser, "delta denominator is smaller than the verified polynomial bound");
        goto done;
    }

done:
    {
        TrytrProofResult result = {.valid = parser.error[0] == '\0',
                                   .offset = parser.cursor,
                                   .message = {0}};
        if (result.valid) {
            (void)snprintf(result.message, sizeof(result.message),
                           "epsilon-delta certificate checked (required bound %llu)",
                           (unsigned long long)required_bound);
        } else {
            (void)snprintf(result.message, sizeof(result.message), "%s", parser.error);
        }
        return result;
    }
}
