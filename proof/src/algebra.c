#include "algebra_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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
        if (digit > maximum || parsed > (maximum - digit) / 10U) {
            set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                      "integer literal exceeds the supported limit");
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

static bool parse_number(Parser *parser, Rational *number)
{
    uint64_t whole = 0U;
    if (!parse_unsigned(parser, (uint64_t)ALGEBRA_MAX_LITERAL, &whole)) {
        return false;
    }
    int64_t denominator = 1;
    uint64_t numerator = whole;
    skip_space(parser);
    if (parser->cursor < parser->length && parser->source[parser->cursor] == '.') {
        ++parser->cursor;
        uint64_t fractional = 0U;
        uint64_t scale = 1U;
        size_t digits = 0U;
        while (parser->cursor < parser->length &&
               isdigit((unsigned char)parser->source[parser->cursor]) != 0) {
            if (digits >= 9U) {
                set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                          "decimal precision exceeds nine digits");
                return false;
            }
            fractional = fractional * 10U +
                         (uint64_t)(parser->source[parser->cursor] - '0');
            scale *= 10U;
            ++digits;
            ++parser->cursor;
        }
        if (digits == 0U || whole > ((uint64_t)ALGEBRA_MAX_LITERAL * scale - fractional) /
                                      scale) {
            set_error(parser, TRYTR_ALGEBRA_INVALID,
                      "malformed or oversized decimal literal");
            return false;
        }
        numerator = whole * scale + fractional;
        denominator = (int64_t)scale;
    }
    *number = (Rational){.numerator = (int64_t)numerator,
                         .denominator = denominator};
    return rational_normalize(number);
}

static bool variable_is_unsupported_function(const char *name)
{
    static const char *const names[] = {
        "sin", "cos", "tan", "cot", "sec", "csc", "asin", "acos", "atan",
        "sinh", "cosh", "tanh", "log", "ln", "exp", "sqrt", "abs", "lim",
        "min", "max", "floor", "ceil", "det", "tr", "rank", "gcd", "lcm"
    };
    for (size_t index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (strcmp(name, names[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_supported_latex_variable(const char *name)
{
    static const char *const names[] = {
        "alpha", "beta", "gamma", "delta", "epsilon", "varepsilon", "zeta",
        "eta", "theta", "vartheta", "iota", "kappa", "lambda", "mu", "nu",
        "xi", "omicron", "rho", "varrho", "sigma", "varsigma", "tau",
        "upsilon", "phi", "varphi", "chi", "psi", "omega", "Gamma", "Delta",
        "Theta", "Lambda", "Xi", "Sigma", "Upsilon", "Phi", "Psi", "Omega",
        "ell"
    };
    for (size_t index = 0U; index < sizeof(names) / sizeof(names[0]); ++index) {
        if (strcmp(name, names[index]) == 0) {
            return true;
        }
    }
    return false;
}

static bool register_variable(Parser *parser, const char *name, size_t length,
                              size_t *index)
{
    if (length == 0U || length > ALGEBRA_MAX_NAME) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                  "variable name exceeds the supported length");
        return false;
    }
    char normalized[ALGEBRA_MAX_NAME + 1U] = {0};
    (void)memcpy(normalized, name, length);
    for (size_t variable = 0U; variable < parser->variable_count; ++variable) {
        if (strcmp(parser->variables[variable], normalized) == 0) {
            *index = variable;
            return true;
        }
    }
    if (parser->variable_count >= ALGEBRA_MAX_VARIABLES) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                  "expression uses more than eight variables");
        return false;
    }
    *index = parser->variable_count;
    (void)snprintf(parser->variables[parser->variable_count], ALGEBRA_MAX_NAME + 1U,
                   "%s", normalized);
    ++parser->variable_count;
    return true;
}

static bool parse_variable(Parser *parser, Polynomial *result)
{
    skip_space(parser);
    char name[ALGEBRA_MAX_NAME + 1U] = {0};
    size_t name_length = 0U;
    bool latex_symbol = false;
    if (parser->cursor >= parser->length) {
        return false;
    }
    if (parser->source[parser->cursor] == '\\') {
        latex_symbol = true;
        ++parser->cursor;
        const size_t start = parser->cursor;
        while (parser->cursor < parser->length &&
               isalpha((unsigned char)parser->source[parser->cursor]) != 0) {
            ++parser->cursor;
        }
        name_length = parser->cursor - start;
        if (name_length == 0U || name_length > ALGEBRA_MAX_NAME) {
            set_error(parser, TRYTR_ALGEBRA_INVALID, "malformed LaTeX symbol");
            return false;
        }
        (void)memcpy(name, parser->source + start, name_length);
        name[name_length] = '\0';
        if (strcmp(name, "pi") == 0 || strcmp(name, "infty") == 0 ||
            strcmp(name, "infinity") == 0) {
            set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                      "special mathematical constants are outside the polynomial checker");
            return false;
        }
        if (!is_supported_latex_variable(name)) {
            set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                      "unknown LaTeX command is outside the polynomial checker");
            return false;
        }
    } else if (isalpha((unsigned char)parser->source[parser->cursor]) != 0) {
        const size_t start = parser->cursor;
        while (parser->cursor < parser->length &&
               isalpha((unsigned char)parser->source[parser->cursor]) != 0) {
            ++parser->cursor;
        }
        const size_t word_length = parser->cursor - start;
        char word[ALGEBRA_MAX_NAME + 1U] = {0};
        if (word_length <= ALGEBRA_MAX_NAME) {
            (void)memcpy(word, parser->source + start, word_length);
        }
        if (word_length <= ALGEBRA_MAX_NAME &&
            variable_is_unsupported_function(word)) {
            set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                      "non-polynomial function is outside the algebra checker");
            return false;
        }
        if (parser->cursor < parser->length && parser->source[parser->cursor] == '(' &&
            word_length > 1U) {
            set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                      "function application is outside the polynomial checker");
            return false;
        }
        /* Adjacent Latin letters mean a product: xyz is x*y*z. */
        name[0] = parser->source[start];
        name_length = 1U;
        parser->cursor = start + 1U;
    } else {
        return false;
    }
    if (latex_symbol && variable_is_unsupported_function(name)) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                  "non-polynomial function is outside the algebra checker");
        return false;
    }

    if (parser->cursor < parser->length && parser->source[parser->cursor] == '_') {
        ++parser->cursor;
        bool braced = false;
        if (parser->cursor < parser->length && parser->source[parser->cursor] == '{') {
            braced = true;
            ++parser->cursor;
        }
        const size_t suffix_start = parser->cursor;
        while (parser->cursor < parser->length &&
               (isalnum((unsigned char)parser->source[parser->cursor]) != 0 ||
                parser->source[parser->cursor] == '-')) {
            ++parser->cursor;
        }
        const size_t suffix_length = parser->cursor - suffix_start;
        if (suffix_length == 0U || name_length + suffix_length + 1U > ALGEBRA_MAX_NAME ||
            (braced && (parser->cursor >= parser->length ||
                        parser->source[parser->cursor] != '}'))) {
            set_error(parser, TRYTR_ALGEBRA_INVALID, "malformed variable subscript");
            return false;
        }
        name[name_length++] = '_';
        (void)memcpy(name + name_length, parser->source + suffix_start, suffix_length);
        name_length += suffix_length;
        if (braced) {
            ++parser->cursor;
        }
    }
    name[name_length] = '\0';
    size_t variable = 0U;
    if (!register_variable(parser, name, name_length, &variable)) {
        return false;
    }
    *result = polynomial_zero();
    result->terms[0] = (AlgebraTerm){
        .monomial = {.powers = {0}},
        .coefficient = {.numerator = 1, .denominator = 1}
    };
    result->terms[0].monomial.powers[variable] = 1U;
    result->count = 1U;
    return true;
}

static bool parse_expression(Parser *parser, size_t depth, Polynomial *result);

static bool parse_grouped_expression(Parser *parser, size_t depth,
                                     Polynomial *result)
{
    const bool latex_round = consume(parser, "\\left(");
    const bool latex_square = !latex_round && consume(parser, "\\left[");
    const bool round = !latex_round && !latex_square && consume(parser, "(");
    const bool square = !latex_round && !latex_square && !round && consume(parser, "[");
    const bool curly = !latex_round && !latex_square && !round && !square &&
                       consume(parser, "{");
    if (!latex_round && !latex_square && !round && !square && !curly) {
        return false;
    }
    if (!parse_expression(parser, depth + 1U, result)) {
        return false;
    }
    const bool closed = latex_round ? consume(parser, "\\right)")
                         : latex_square ? consume(parser, "\\right]")
                         : round ? consume(parser, ")")
                         : square ? consume(parser, "]")
                                   : consume(parser, "}");
    if (!closed) {
        set_error(parser, TRYTR_ALGEBRA_INVALID,
                  "expected closing parenthesis in expression");
        return false;
    }
    return true;
}

static bool parse_fraction(Parser *parser, size_t depth, Polynomial *result)
{
    if (!(consume(parser, "\\frac") || consume(parser, "\\dfrac"))) {
        return false;
    }
    if (!consume(parser, "{")) {
        set_error(parser, TRYTR_ALGEBRA_INVALID, "expected '{' after \\frac");
        return false;
    }
    Polynomial numerator;
    if (!parse_expression(parser, depth + 1U, &numerator) || !consume(parser, "}") ||
        !consume(parser, "{")) {
        set_error(parser, TRYTR_ALGEBRA_INVALID, "malformed numerator in \\frac");
        return false;
    }
    Polynomial denominator;
    if (!parse_expression(parser, depth + 1U, &denominator) || !consume(parser, "}")) {
        set_error(parser, TRYTR_ALGEBRA_INVALID, "malformed denominator in \\frac");
        return false;
    }
    if (denominator.count == 0U) {
        set_error(parser, TRYTR_ALGEBRA_INVALID, "division by zero");
        return false;
    }
    Rational divisor;
    if (denominator.count != 1U ||
        memcmp(denominator.terms[0].monomial.powers,
               (uint8_t[ALGEBRA_MAX_VARIABLES]){0},
               sizeof(denominator.terms[0].monomial.powers)) != 0) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                  "division by a variable expression is outside the polynomial checker");
        return false;
    }
    divisor = denominator.terms[0].coefficient;
    if (divisor.numerator == 0) {
        set_error(parser, TRYTR_ALGEBRA_INVALID, "division by zero");
        return false;
    }
    Rational reciprocal;
    if (!rational_divide((Rational){.numerator = 1, .denominator = 1},
                         divisor, &reciprocal) ||
        !polynomial_scale(parser, &numerator, reciprocal, result)) {
        if (parser->error[0] == '\0') {
            set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                      "fraction exceeds the supported rational range");
        }
        return false;
    }
    return true;
}

static bool parse_primary(Parser *parser, size_t depth, Polynomial *result)
{
    if (depth > ALGEBRA_MAX_NESTING) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                  "expression exceeds the nesting limit");
        return false;
    }
    if (parse_fraction(parser, depth, result)) {
        return true;
    }
    if (parser->error[0] != '\0') {
        return false;
    }
    if (consume(parser, "\\sqrt") || consume(parser, "\\sin") ||
        consume(parser, "\\cos") || consume(parser, "\\tan") ||
        consume(parser, "\\log") || consume(parser, "\\ln") ||
        consume(parser, "\\exp")) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                  "non-polynomial function is outside the algebra checker");
        return false;
    }
    if (parse_grouped_expression(parser, depth, result)) {
        return true;
    }
    if (parser->error[0] != '\0') {
        return false;
    }
    skip_space(parser);
    if (parser->cursor < parser->length &&
        isdigit((unsigned char)parser->source[parser->cursor]) != 0) {
        Rational number;
        if (!parse_number(parser, &number)) {
            return false;
        }
        *result = polynomial_constant(number);
        return true;
    }
    return parse_variable(parser, result);
}

static bool parse_power(Parser *parser, size_t depth, Polynomial *result)
{
    if (!parse_primary(parser, depth, result)) {
        return false;
    }
    if (!consume(parser, "^")) {
        return true;
    }
    const bool braced = consume(parser, "{");
    uint64_t exponent = 0U;
    if (!parse_unsigned(parser, ALGEBRA_MAX_DEGREE, &exponent) ||
        (braced && !consume(parser, "}"))) {
        if (parser->error[0] == '\0') {
            set_error(parser, TRYTR_ALGEBRA_INVALID,
                      "expected a nonnegative integer exponent");
        }
        return false;
    }
    Polynomial base = *result;
    return polynomial_power(parser, &base, exponent, result);
}

static bool parse_unary(Parser *parser, size_t depth, Polynomial *result)
{
    if (depth > ALGEBRA_MAX_NESTING) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                  "expression exceeds the nesting limit");
        return false;
    }
    if (consume(parser, "+")) {
        return parse_unary(parser, depth + 1U, result);
    }
    if (consume(parser, "-")) {
        if (!parse_unary(parser, depth + 1U, result)) {
            return false;
        }
        for (size_t index = 0U; index < result->count; ++index) {
            if (result->terms[index].coefficient.numerator == INT64_MIN) {
                set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                          "rational coefficient exceeds the supported range");
                return false;
            }
            result->terms[index].coefficient.numerator =
                -result->terms[index].coefficient.numerator;
        }
        return true;
    }
    return parse_power(parser, depth, result);
}

static bool starts_primary(Parser *parser)
{
    skip_space(parser);
    if (parser->cursor >= parser->length) {
        return false;
    }
    const char character = parser->source[parser->cursor];
    if (isalnum((unsigned char)character) != 0 || character == '(' ||
        character == '[' || character == '{') {
        return true;
    }
    if (character != '\\' || parser->cursor + 1U >= parser->length ||
        isalpha((unsigned char)parser->source[parser->cursor + 1U]) == 0) {
        return false;
    }
    return parser->length - parser->cursor < strlen("\\right") ||
           memcmp(parser->source + parser->cursor, "\\right", strlen("\\right")) != 0;
}

static bool polynomial_divide(Parser *parser, const Polynomial *numerator,
                              const Polynomial *denominator, Polynomial *result)
{
    if (denominator->count == 0U) {
        set_error(parser, TRYTR_ALGEBRA_INVALID, "division by zero");
        return false;
    }
    if (denominator->count != 1U ||
        memcmp(denominator->terms[0].monomial.powers,
               (uint8_t[ALGEBRA_MAX_VARIABLES]){0},
               sizeof(denominator->terms[0].monomial.powers)) != 0) {
        set_error(parser, TRYTR_ALGEBRA_UNSUPPORTED,
                  "division by a variable expression is outside the polynomial checker");
        return false;
    }
    Rational reciprocal;
    if (!rational_divide((Rational){.numerator = 1, .denominator = 1},
                         denominator->terms[0].coefficient, &reciprocal) ||
        !polynomial_scale(parser, numerator, reciprocal, result)) {
        if (parser->error[0] == '\0') {
            set_error(parser, TRYTR_ALGEBRA_INVALID,
                      "division by zero or unsupported rational value");
        }
        return false;
    }
    return true;
}

static bool parse_term(Parser *parser, size_t depth, Polynomial *result)
{
    if (!parse_unary(parser, depth, result)) {
        return false;
    }
    for (;;) {
        const bool multiply = consume(parser, "*") || consume(parser, "\\cdot") ||
                              consume(parser, "\\times");
        const bool divide = !multiply && consume(parser, "/");
        if (!multiply && !divide && !starts_primary(parser)) {
            return true;
        }
        Polynomial right;
        if (!parse_unary(parser, depth, &right)) {
            return false;
        }
        Polynomial combined;
        const bool ok = divide ? polynomial_divide(parser, result, &right, &combined)
                               : polynomial_multiply(parser, result, &right, &combined);
        if (!ok) {
            return false;
        }
        *result = combined;
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
        Polynomial combined;
        if (!parse_term(parser, depth, &right) ||
            !polynomial_add(parser, result, &right, subtract, &combined)) {
            return false;
        }
        *result = combined;
    }
}

static bool polynomial_equal(const Polynomial *left, const Polynomial *right)
{
    if (left->count != right->count) {
        return false;
    }
    for (size_t left_index = 0U; left_index < left->count; ++left_index) {
        bool found = false;
        for (size_t right_index = 0U; right_index < right->count; ++right_index) {
            if (monomial_equal(&left->terms[left_index].monomial,
                               &right->terms[right_index].monomial) &&
                rational_equal(left->terms[left_index].coefficient,
                               right->terms[right_index].coefficient)) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

static bool consume_math_delimiter(Parser *parser, bool opening, char *closing)
{
    if (opening && consume(parser, "\\(")) {
        *closing = ')';
        return true;
    }
    if (opening && consume(parser, "\\[")) {
        *closing = ']';
        return true;
    }
    if (opening && consume(parser, "$$")) {
        *closing = '$';
        return true;
    }
    if (opening && consume(parser, "$")) {
        *closing = '$';
        return true;
    }
    if (!opening && *closing == ')' && consume(parser, "\\)")) {
        return true;
    }
    if (!opening && *closing == ']' && consume(parser, "\\]")) {
        return true;
    }
    if (!opening && *closing == '$' && consume(parser, "$$")) {
        return true;
    }
    if (!opening && *closing == '$' && consume(parser, "$")) {
        return true;
    }
    return false;
}

TrytrAlgebraResult trytr_algebra_check(const char *source)
{
    Parser parser = {0};
    parser.error_status = TRYTR_ALGEBRA_INVALID;
    if (source == NULL) {
        return (TrytrAlgebraResult){.status = TRYTR_ALGEBRA_INVALID,
                                    .offset = 0U,
                                    .message = "equation source is null"};
    }
    parser.source = source;
    parser.length = strnlen(source, ALGEBRA_MAX_SOURCE + 1U);
    if (parser.length > ALGEBRA_MAX_SOURCE) {
        return (TrytrAlgebraResult){.status = TRYTR_ALGEBRA_INVALID,
                                    .offset = 0U,
                                    .message = "equation source exceeds 64 KiB"};
    }
    char closing = '\0';
    (void)consume_math_delimiter(&parser, true, &closing);
    Polynomial left;
    Polynomial right;
    bool parsed = parse_expression(&parser, 0U, &left);
    if (parsed && !consume(&parser, "=")) {
        set_error(&parser, TRYTR_ALGEBRA_INVALID,
                  "expected '=' between the two polynomial expressions");
        parsed = false;
    }
    if (parsed) {
        parsed = parse_expression(&parser, 0U, &right);
    }
    if (parsed && closing != '\0' && !consume_math_delimiter(&parser, false, &closing)) {
        set_error(&parser, TRYTR_ALGEBRA_INVALID,
                  "missing closing math delimiter");
        parsed = false;
    }
    skip_space(&parser);
    if (parsed && parser.cursor != parser.length) {
        set_error(&parser, TRYTR_ALGEBRA_INVALID,
                  "unexpected content after the equality");
        parsed = false;
    }
    if (parsed && polynomial_equal(&left, &right)) {
        return (TrytrAlgebraResult){.status = TRYTR_ALGEBRA_VERIFIED,
                                    .offset = parser.cursor,
                                    .message = "identity verified over rational polynomials"};
    }
    if (parsed) {
        return (TrytrAlgebraResult){.status = TRYTR_ALGEBRA_NOT_IDENTITY,
                                    .offset = parser.cursor,
                                    .message = "equality is not a polynomial identity"};
    }
    TrytrAlgebraResult result = {.status = parser.error_status,
                                 .offset = parser.cursor,
                                 .message = {0}};
    (void)snprintf(result.message, sizeof(result.message), "%s",
                   parser.error[0] == '\0' ? "invalid polynomial equality"
                                             : parser.error);
    return result;
}
