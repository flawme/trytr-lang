#include "algebra_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

bool algebra_parse_unsigned(Parser *parser, uint64_t maximum, uint64_t *value)
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

bool algebra_parse_number(Parser *parser, Rational *number)
{
    uint64_t whole = 0U;
    if (!algebra_parse_unsigned(parser, (uint64_t)ALGEBRA_MAX_LITERAL, &whole)) {
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

bool algebra_parse_variable(Parser *parser, Polynomial *result)
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

