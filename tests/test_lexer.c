#include "test.h"

#include "lexer.h"

#include <stdint.h>

typedef struct LexerHarness {
    TrytrArena arena;
    TrytrDiagnosticList diagnostics;
    TrytrLexer lexer;
} LexerHarness;

static void harness_init(LexerHarness *harness, const char *source, size_t length)
{
    trytr_arena_init(&harness->arena, 1024u * 1024u);
    trytr_diagnostic_list_init(&harness->diagnostics, &harness->arena, 64u);
    trytr_lexer_init(&harness->lexer, source, length, &harness->diagnostics);
}

static void harness_destroy(LexerHarness *harness)
{
    trytr_arena_destroy(&harness->arena);
}

static bool assert_token(const TrytrToken *token, TrytrTokenKind kind, const char *lexeme)
{
    const size_t length = strlen(lexeme);
    TEST_ASSERT_INT(kind, token->kind);
    TEST_ASSERT_SIZE(length, token->length);
    TEST_ASSERT(length == 0u || memcmp(token->lexeme, lexeme, length) == 0);
    return true;
}

static bool test_structural_tokens_comments_and_locations(void)
{
    static const char source[] =
        "@rect(-1,+2,3,4) // line\n"
        "/* multi\nline */ #stable-id";
    static const struct {
        TrytrTokenKind kind;
        const char *lexeme;
    } expected[] = {
        {TRYTR_TOKEN_AT, "@"},
        {TRYTR_TOKEN_IDENTIFIER, "rect"},
        {TRYTR_TOKEN_LPAREN, "("},
        {TRYTR_TOKEN_MINUS, "-"},
        {TRYTR_TOKEN_INTEGER, "1"},
        {TRYTR_TOKEN_COMMA, ","},
        {TRYTR_TOKEN_PLUS, "+"},
        {TRYTR_TOKEN_INTEGER, "2"},
        {TRYTR_TOKEN_COMMA, ","},
        {TRYTR_TOKEN_INTEGER, "3"},
        {TRYTR_TOKEN_COMMA, ","},
        {TRYTR_TOKEN_INTEGER, "4"},
        {TRYTR_TOKEN_RPAREN, ")"},
        {TRYTR_TOKEN_HASH, "#"},
        {TRYTR_TOKEN_IDENTIFIER, "stable-id"},
        {TRYTR_TOKEN_EOF, ""}
    };
    LexerHarness harness;
    harness_init(&harness, source, sizeof(source) - 1u);

    for (size_t index = 0u; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        const TrytrToken token = trytr_lexer_next(&harness.lexer);
        if (!assert_token(&token, expected[index].kind, expected[index].lexeme)) {
            harness_destroy(&harness);
            return false;
        }
        if (index == 13u) {
            TEST_ASSERT_SIZE(3u, token.location.line);
            TEST_ASSERT_SIZE(9u, token.location.column);
        }
    }
    TEST_ASSERT_SIZE(0u, harness.diagnostics.count);
    harness_destroy(&harness);
    return true;
}

static bool test_utf8_validation_accepts_scalar_values(void)
{
    static const char source[] = "// \xF0\x9F\x98\x80 and \xCE\xB1\n@text";
    LexerHarness harness;
    harness_init(&harness, source, sizeof(source) - 1u);

    TEST_ASSERT(trytr_utf8_validate(source, sizeof(source) - 1u, &harness.diagnostics));
    TEST_ASSERT_INT(TRYTR_TOKEN_AT, trytr_lexer_next(&harness.lexer).kind);
    TEST_ASSERT_INT(TRYTR_TOKEN_IDENTIFIER, trytr_lexer_next(&harness.lexer).kind);
    TEST_ASSERT_SIZE(0u, harness.diagnostics.count);
    harness_destroy(&harness);
    return true;
}

static bool test_utf8_validation_rejects_malformed_sequences(void)
{
    static const char overlong[] = {'\xC0', '\xAF'};
    static const char surrogate[] = {'\xED', '\xA0', '\x80'};
    static const char truncated[] = {'\xE2', '\x82'};
    static const char above_unicode[] = {'\xF4', '\x90', '\x80', '\x80'};
    static const struct {
        const char *source;
        size_t length;
    } cases[] = {
        {overlong, sizeof(overlong)},
        {surrogate, sizeof(surrogate)},
        {truncated, sizeof(truncated)},
        {above_unicode, sizeof(above_unicode)}
    };

    for (size_t index = 0u; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        LexerHarness harness;
        harness_init(&harness, cases[index].source, cases[index].length);
        TEST_ASSERT(!trytr_utf8_validate(cases[index].source, cases[index].length,
                                         &harness.diagnostics));
        TEST_ASSERT_SIZE(1u, harness.diagnostics.count);
        TEST_ASSERT_STRING("TRYTR-E-UTF8", harness.diagnostics.items[0].code);
        TEST_ASSERT_SIZE(0u, harness.diagnostics.items[0].location.offset);
        harness_destroy(&harness);
    }
    return true;
}

static bool test_utf8_error_has_source_location(void)
{
    static const char source[] = {'/', '/', '\n', '\xC0', '\xAF'};
    LexerHarness harness;
    harness_init(&harness, source, sizeof(source));

    TEST_ASSERT(!trytr_utf8_validate(source, sizeof(source), &harness.diagnostics));
    TEST_ASSERT_SIZE(1u, harness.diagnostics.count);
    TEST_ASSERT_SIZE(3u, harness.diagnostics.items[0].location.offset);
    TEST_ASSERT_SIZE(2u, harness.diagnostics.items[0].location.line);
    TEST_ASSERT_SIZE(1u, harness.diagnostics.items[0].location.column);
    harness_destroy(&harness);
    return true;
}

static bool test_integer_magnitude_and_overflow(void)
{
    static const char source[] = "-9223372036854775808 9223372036854775807 18446744073709551616";
    LexerHarness harness;
    harness_init(&harness, source, sizeof(source) - 1u);

    TrytrToken token = trytr_lexer_next(&harness.lexer);
    TEST_ASSERT_INT(TRYTR_TOKEN_MINUS, token.kind);
    token = trytr_lexer_next(&harness.lexer);
    TEST_ASSERT_INT(TRYTR_TOKEN_INTEGER, token.kind);
    TEST_ASSERT_INT((uint64_t)INT64_MAX + 1u, token.integer_value);
    token = trytr_lexer_next(&harness.lexer);
    TEST_ASSERT_INT(TRYTR_TOKEN_INTEGER, token.kind);
    TEST_ASSERT_INT(INT64_MAX, token.integer_value);
    token = trytr_lexer_next(&harness.lexer);
    TEST_ASSERT_INT(TRYTR_TOKEN_INVALID, token.kind);
    TEST_ASSERT_SIZE(1u, harness.diagnostics.count);
    TEST_ASSERT_STRING("TRYTR-E-INTEGER", harness.diagnostics.items[0].code);
    harness_destroy(&harness);
    return true;
}

static bool test_unterminated_comment_is_bounded(void)
{
    static const char source[] = "/* never closed";
    LexerHarness harness;
    harness_init(&harness, source, sizeof(source) - 1u);

    TEST_ASSERT_INT(TRYTR_TOKEN_EOF, trytr_lexer_next(&harness.lexer).kind);
    TEST_ASSERT_SIZE(1u, harness.diagnostics.count);
    TEST_ASSERT_STRING("TRYTR-E-COMMENT", harness.diagnostics.items[0].code);
    TEST_ASSERT_SIZE(0u, harness.diagnostics.items[0].location.offset);
    harness_destroy(&harness);
    return true;
}

static bool test_non_ascii_structural_character_is_rejected(void)
{
    static const char source[] = "\xCE\xB1";
    LexerHarness harness;
    harness_init(&harness, source, sizeof(source) - 1u);

    TEST_ASSERT(trytr_utf8_validate(source, sizeof(source) - 1u, &harness.diagnostics));
    TEST_ASSERT_INT(TRYTR_TOKEN_INVALID, trytr_lexer_next(&harness.lexer).kind);
    TEST_ASSERT_SIZE(1u, harness.diagnostics.count);
    TEST_ASSERT_STRING("TRYTR-E-CHAR", harness.diagnostics.items[0].code);
    harness_destroy(&harness);
    return true;
}

static bool test_raw_block_preserves_nested_braces_and_trims_outer_space(void)
{
    static const char source[] = "@math { \n I_{DSS} {x} // text \n } @text";
    LexerHarness harness;
    harness_init(&harness, source, sizeof(source) - 1u);

    TEST_ASSERT_INT(TRYTR_TOKEN_AT, trytr_lexer_next(&harness.lexer).kind);
    TEST_ASSERT_INT(TRYTR_TOKEN_IDENTIFIER, trytr_lexer_next(&harness.lexer).kind);
    TEST_ASSERT_INT(TRYTR_TOKEN_LBRACE, trytr_lexer_next(&harness.lexer).kind);
    char *text = NULL;
    size_t length = 0u;
    TEST_ASSERT(trytr_lexer_raw_block(&harness.lexer, &harness.arena, &text, &length));
    TEST_ASSERT_SIZE(sizeof("I_{DSS} {x} // text") - 1u, length);
    TEST_ASSERT(memcmp(text, "I_{DSS} {x} // text", length) == 0);
    TEST_ASSERT_INT(TRYTR_TOKEN_AT, trytr_lexer_next(&harness.lexer).kind);
    TEST_ASSERT_SIZE(0u, harness.diagnostics.count);
    harness_destroy(&harness);
    return true;
}

static const TrytrTestCase tests[] = {
    {"structural tokens, comments, and locations", test_structural_tokens_comments_and_locations},
    {"UTF-8 accepts scalar values", test_utf8_validation_accepts_scalar_values},
    {"UTF-8 rejects malformed sequences", test_utf8_validation_rejects_malformed_sequences},
    {"UTF-8 error location", test_utf8_error_has_source_location},
    {"integer magnitude and overflow", test_integer_magnitude_and_overflow},
    {"unterminated comment", test_unterminated_comment_is_bounded},
    {"non-ASCII structural character", test_non_ascii_structural_character_is_rejected},
    {"nested raw block", test_raw_block_preserves_nested_braces_and_trims_outer_space}
};

TEST_MAIN(tests)
