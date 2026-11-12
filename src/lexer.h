#ifndef TRYTR_SRC_LEXER_H
#define TRYTR_SRC_LEXER_H

#include "arena.h"
#include "diagnostic.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum TrytrTokenKind {
    TRYTR_TOKEN_EOF = 0,
    TRYTR_TOKEN_AT,
    TRYTR_TOKEN_HASH,
    TRYTR_TOKEN_IDENTIFIER,
    TRYTR_TOKEN_INTEGER,
    TRYTR_TOKEN_LBRACE,
    TRYTR_TOKEN_RBRACE,
    TRYTR_TOKEN_LPAREN,
    TRYTR_TOKEN_RPAREN,
    TRYTR_TOKEN_COLON,
    TRYTR_TOKEN_COMMA,
    TRYTR_TOKEN_PLUS,
    TRYTR_TOKEN_MINUS,
    TRYTR_TOKEN_STAR,
    TRYTR_TOKEN_SLASH,
    TRYTR_TOKEN_INVALID
} TrytrTokenKind;

typedef struct TrytrToken {
    TrytrTokenKind kind;
    const char *lexeme;
    size_t length;
    uint64_t integer_value;
    TrytrSourceLocation location;
} TrytrToken;

typedef struct TrytrLexer {
    const char *source;
    size_t length;
    size_t position;
    size_t line;
    size_t column;
    TrytrDiagnosticList *diagnostics;
} TrytrLexer;

bool trytr_utf8_validate(const char *source, size_t length, TrytrDiagnosticList *diagnostics);
void trytr_lexer_init(TrytrLexer *lexer, const char *source, size_t length, TrytrDiagnosticList *diagnostics);
TrytrToken trytr_lexer_next(TrytrLexer *lexer);
bool trytr_lexer_raw_block(TrytrLexer *lexer, TrytrArena *arena, char **out_text, size_t *out_length);

#endif
