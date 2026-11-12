#include "lexer.h"
#include "limits.h"

#include <stdint.h>
#include <string.h>

static bool ascii_digit(unsigned char value)
{
    return value >= (unsigned char)'0' && value <= (unsigned char)'9';
}

static bool ascii_identifier_start(unsigned char value)
{
    return (value >= (unsigned char)'A' && value <= (unsigned char)'Z') ||
           (value >= (unsigned char)'a' && value <= (unsigned char)'z') ||
           value == (unsigned char)'_';
}

static bool ascii_identifier_continue(unsigned char value)
{
    return ascii_identifier_start(value) || ascii_digit(value) || value == (unsigned char)'-';
}

static bool ascii_whitespace(unsigned char value)
{
    return value == (unsigned char)' ' || value == (unsigned char)'\t' ||
           value == (unsigned char)'\n' || value == (unsigned char)'\r' ||
           value == (unsigned char)'\v' || value == (unsigned char)'\f';
}

static size_t utf8_width_at(const char *source, size_t length, size_t offset)
{
    if (source == NULL || offset >= length) return 1u;
    const unsigned char first = (unsigned char)source[offset];
    size_t width = first >= 0xC2u && first <= 0xDFu ? 2u :
                   first >= 0xE0u && first <= 0xEFu ? 3u :
                   first >= 0xF0u && first <= 0xF4u ? 4u : 1u;
    if (width == 1u || width > length - offset) return 1u;
    for (size_t index = 1u; index < width; ++index) {
        if (((unsigned char)source[offset + index] & 0xC0u) != 0x80u) return 1u;
    }
    return width;
}

static TrytrSourceLocation location_at(const char *source, size_t length, size_t offset)
{
    TrytrSourceLocation location = {.offset = offset, .line = 1u, .column = 1u};
    if (source == NULL) return location;
    if (offset > length) offset = length;
    for (size_t index = 0u; index < offset;) {
        if (source[index] == '\n') {
            ++location.line;
            location.column = 1u;
        } else {
            ++location.column;
        }
        index += utf8_width_at(source, length, index);
    }
    location.offset = offset;
    return location;
}

bool trytr_utf8_validate(
    const char *source,
    size_t length,
    TrytrDiagnosticList *diagnostics
)
{
    if (source == NULL && length != 0u) {
        (void)trytr_diagnostic_add(
            diagnostics,
            TRYTR_DIAGNOSTIC_ERROR,
            "TRYTR-E-UTF8",
            (TrytrSourceLocation){.offset = 0u, .line = 1u, .column = 1u},
            "source is not well-formed UTF-8"
        );
        return false;
    }

    size_t offset = 0u;
    while (offset < length) {
        const unsigned char first = (unsigned char)source[offset];
        if (first <= 0x7Fu) {
            ++offset;
            continue;
        }

        uint32_t codepoint;
        uint32_t minimum;
        size_t continuation_count;
        if (first >= 0xC2u && first <= 0xDFu) {
            codepoint = (uint32_t)(first & 0x1Fu);
            minimum = 0x80u;
            continuation_count = 1u;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            codepoint = (uint32_t)(first & 0x0Fu);
            minimum = 0x800u;
            continuation_count = 2u;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            codepoint = (uint32_t)(first & 0x07u);
            minimum = 0x10000u;
            continuation_count = 3u;
        } else {
            goto invalid_utf8;
        }

        if (continuation_count > length - offset - 1u) goto invalid_utf8;
        for (size_t index = 1u; index <= continuation_count; ++index) {
            const unsigned char next = (unsigned char)source[offset + index];
            if ((next & 0xC0u) != 0x80u) goto invalid_utf8;
            codepoint = (codepoint << 6u) | (uint32_t)(next & 0x3Fu);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
            goto invalid_utf8;
        }
        offset += continuation_count + 1u;
        continue;

    invalid_utf8:
        (void)trytr_diagnostic_add(
            diagnostics,
            TRYTR_DIAGNOSTIC_ERROR,
            "TRYTR-E-UTF8",
            location_at(source, length, offset),
            "source is not well-formed UTF-8"
        );
        return false;
    }
    return true;
}

void trytr_lexer_init(
    TrytrLexer *lexer,
    const char *source,
    size_t length,
    TrytrDiagnosticList *diagnostics
)
{
    if (lexer == NULL) return;
    *lexer = (TrytrLexer){
        .source = source == NULL ? "" : source,
        .length = source == NULL ? 0u : length,
        .position = 0u,
        .line = 1u,
        .column = 1u,
        .diagnostics = diagnostics
    };
}

static TrytrSourceLocation lexer_location(const TrytrLexer *lexer)
{
    return (TrytrSourceLocation){
        .offset = lexer->position,
        .line = lexer->line,
        .column = lexer->column
    };
}

static void lexer_advance(TrytrLexer *lexer)
{
    if (lexer->position >= lexer->length) return;
    const size_t width = utf8_width_at(lexer->source, lexer->length, lexer->position);
    if (lexer->source[lexer->position] == '\n') {
        ++lexer->line;
        lexer->column = 1u;
    } else {
        ++lexer->column;
    }
    lexer->position += width;
}

static TrytrToken lexer_token(
    const TrytrLexer *lexer,
    TrytrTokenKind kind,
    size_t start,
    TrytrSourceLocation location
)
{
    return (TrytrToken){
        .kind = kind,
        .lexeme = lexer->source + start,
        .length = lexer->position - start,
        .integer_value = 0u,
        .location = location
    };
}

static void lexer_error(
    TrytrLexer *lexer,
    const char *code,
    TrytrSourceLocation location,
    const char *message
)
{
    (void)trytr_diagnostic_add(
        lexer->diagnostics,
        TRYTR_DIAGNOSTIC_ERROR,
        code,
        location,
        "%s",
        message
    );
}

static void skip_space_and_comments(TrytrLexer *lexer)
{
    for (;;) {
        while (lexer->position < lexer->length &&
               ascii_whitespace((unsigned char)lexer->source[lexer->position])) {
            lexer_advance(lexer);
        }
        if (lexer->position >= lexer->length || lexer->source[lexer->position] != '/' ||
            lexer->length - lexer->position < 2u) {
            return;
        }

        const char next = lexer->source[lexer->position + 1u];
        if (next == '/') {
            lexer_advance(lexer);
            lexer_advance(lexer);
            while (lexer->position < lexer->length &&
                   lexer->source[lexer->position] != '\n') {
                lexer_advance(lexer);
            }
            continue;
        }
        if (next != '*') return;

        const TrytrSourceLocation opening = lexer_location(lexer);
        lexer_advance(lexer);
        lexer_advance(lexer);
        bool closed = false;
        while (lexer->position < lexer->length) {
            if (lexer->source[lexer->position] == '*' &&
                lexer->length - lexer->position >= 2u &&
                lexer->source[lexer->position + 1u] == '/') {
                lexer_advance(lexer);
                lexer_advance(lexer);
                closed = true;
                break;
            }
            lexer_advance(lexer);
        }
        if (!closed) {
            lexer_error(lexer, "TRYTR-E-COMMENT", opening, "multiline comment is not closed");
            return;
        }
    }
}

static TrytrToken lex_integer(TrytrLexer *lexer)
{
    const size_t start = lexer->position;
    const TrytrSourceLocation location = lexer_location(lexer);
    uint64_t value = 0u;
    bool overflow = false;
    while (lexer->position < lexer->length &&
           ascii_digit((unsigned char)lexer->source[lexer->position])) {
        const uint64_t digit = (uint64_t)(unsigned char)(lexer->source[lexer->position] - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            overflow = true;
        } else if (!overflow) {
            value = value * 10u + digit;
        }
        lexer_advance(lexer);
    }

    TrytrToken token = lexer_token(lexer, overflow ? TRYTR_TOKEN_INVALID : TRYTR_TOKEN_INTEGER,
                                   start, location);
    token.integer_value = value;
    if (overflow) {
        lexer_error(lexer, "TRYTR-E-INTEGER", location, "integer literal is too large");
    }
    return token;
}

TrytrToken trytr_lexer_next(TrytrLexer *lexer)
{
    if (lexer == NULL || lexer->source == NULL) {
        return (TrytrToken){.kind = TRYTR_TOKEN_EOF};
    }
    skip_space_and_comments(lexer);
    const size_t start = lexer->position;
    const TrytrSourceLocation location = lexer_location(lexer);
    if (start >= lexer->length) {
        return lexer_token(lexer, TRYTR_TOKEN_EOF, start, location);
    }

    const unsigned char value = (unsigned char)lexer->source[lexer->position];
    if (ascii_identifier_start(value)) {
        lexer_advance(lexer);
        while (lexer->position < lexer->length &&
               ascii_identifier_continue((unsigned char)lexer->source[lexer->position])) {
            lexer_advance(lexer);
        }
        return lexer_token(lexer, TRYTR_TOKEN_IDENTIFIER, start, location);
    }
    if (ascii_digit(value)) return lex_integer(lexer);

    lexer_advance(lexer);
    switch (value) {
        case '@': return lexer_token(lexer, TRYTR_TOKEN_AT, start, location);
        case '#': return lexer_token(lexer, TRYTR_TOKEN_HASH, start, location);
        case '{': return lexer_token(lexer, TRYTR_TOKEN_LBRACE, start, location);
        case '}': return lexer_token(lexer, TRYTR_TOKEN_RBRACE, start, location);
        case '(': return lexer_token(lexer, TRYTR_TOKEN_LPAREN, start, location);
        case ')': return lexer_token(lexer, TRYTR_TOKEN_RPAREN, start, location);
        case ':': return lexer_token(lexer, TRYTR_TOKEN_COLON, start, location);
        case ',': return lexer_token(lexer, TRYTR_TOKEN_COMMA, start, location);
        case '+': return lexer_token(lexer, TRYTR_TOKEN_PLUS, start, location);
        case '-': return lexer_token(lexer, TRYTR_TOKEN_MINUS, start, location);
        case '*': return lexer_token(lexer, TRYTR_TOKEN_STAR, start, location);
        case '/': return lexer_token(lexer, TRYTR_TOKEN_SLASH, start, location);
        default:
            lexer_error(lexer, "TRYTR-E-CHAR", location,
                        "unexpected character in structural syntax");
            return lexer_token(lexer, TRYTR_TOKEN_INVALID, start, location);
    }
}

bool trytr_lexer_raw_block(
    TrytrLexer *lexer,
    TrytrArena *arena,
    char **out_text,
    size_t *out_length
)
{
    if (lexer == NULL || arena == NULL || out_text == NULL || out_length == NULL ||
        lexer->position > lexer->length) {
        return false;
    }
    *out_text = NULL;
    *out_length = 0u;

    const size_t content_start = lexer->position;
    const TrytrSourceLocation opening = lexer_location(lexer);
    size_t depth = 1u;
    bool too_deep = false;

    while (lexer->position < lexer->length) {
        const char value = lexer->source[lexer->position];
        if (value == '{') {
            if (depth >= TRYTR_DEFAULT_DEPTH_LIMIT && !too_deep) {
                lexer_error(lexer, "TRYTR-E-LIMIT", lexer_location(lexer),
                            "raw block nesting limit exceeded");
                too_deep = true;
            }
            ++depth;
            lexer_advance(lexer);
            continue;
        }
        if (value == '}') {
            const size_t content_end = lexer->position;
            lexer_advance(lexer);
            --depth;
            if (depth == 0u) {
                size_t trimmed_start = content_start;
                size_t trimmed_end = content_end;
                while (trimmed_start < trimmed_end &&
                       ascii_whitespace((unsigned char)lexer->source[trimmed_start])) {
                    ++trimmed_start;
                }
                while (trimmed_end > trimmed_start &&
                       ascii_whitespace((unsigned char)lexer->source[trimmed_end - 1u])) {
                    --trimmed_end;
                }
                const size_t text_length = trimmed_end - trimmed_start;
                char *text = trytr_arena_copy_string(arena, lexer->source + trimmed_start,
                                                     text_length);
                if (text == NULL) {
                    lexer_error(lexer, "TRYTR-E-MEMORY", opening,
                                "unable to allocate raw block text");
                    return false;
                }
                *out_text = text;
                *out_length = text_length;
                return !too_deep;
            }
            continue;
        }
        lexer_advance(lexer);
    }

    lexer_error(lexer, "TRYTR-E-SYNTAX", opening, "raw text block is not closed");
    return false;
}
