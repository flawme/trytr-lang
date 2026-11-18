#include "trytr/math/mathml.h"

#include "limits.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MathmlBuffer {
    char *data;
    size_t length;
    size_t capacity;
    TrytrStatus status;
} MathmlBuffer;

typedef struct MathmlRenderState {
    MathmlBuffer output;
    size_t expression_count;
} MathmlRenderState;

static bool reserve(MathmlBuffer *buffer, size_t extra)
{
    if (buffer->status != TRYTR_STATUS_OK) return false;
    if (extra > TRYTR_DEFAULT_SVG_OUTPUT_LIMIT - buffer->length) {
        buffer->status = TRYTR_STATUS_LIMIT_EXCEEDED;
        return false;
    }
    const size_t required = buffer->length + extra + 1u;
    if (required <= buffer->capacity) return true;
    size_t capacity = buffer->capacity == 0u ? 1024u : buffer->capacity;
    while (capacity < required) {
        const size_t maximum = (size_t)TRYTR_DEFAULT_SVG_OUTPUT_LIMIT + 1u;
        if (capacity > maximum / 2u) {
            capacity = maximum;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < required) {
        buffer->status = TRYTR_STATUS_LIMIT_EXCEEDED;
        return false;
    }
    char *replacement = realloc(buffer->data, capacity);
    if (replacement == NULL) {
        buffer->status = TRYTR_STATUS_NO_MEMORY;
        return false;
    }
    buffer->data = replacement;
    buffer->capacity = capacity;
    return true;
}

static bool append(MathmlBuffer *buffer, const char *text, size_t length)
{
    if (text == NULL && length != 0u) {
        buffer->status = TRYTR_STATUS_INVALID_ARGUMENT;
        return false;
    }
    if (!reserve(buffer, length)) return false;
    if (length != 0u) memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return true;
}

static bool append_cstr(MathmlBuffer *buffer, const char *text)
{
    return append(buffer, text, strlen(text));
}

static bool append_format(MathmlBuffer *buffer, const char *format, ...)
{
    if (buffer->status != TRYTR_STATUS_OK) return false;
    va_list arguments;
    va_start(arguments, format);
    va_list sizing_arguments;
    va_copy(sizing_arguments, arguments);
    const int needed = vsnprintf(NULL, 0u, format, sizing_arguments);
    va_end(sizing_arguments);
    if (needed < 0) {
        va_end(arguments);
        buffer->status = TRYTR_STATUS_EXECUTION_FAILED;
        return false;
    }
    const size_t length = (size_t)needed;
    if (!reserve(buffer, length)) {
        va_end(arguments);
        return false;
    }
    (void)vsnprintf(buffer->data + buffer->length,
                    buffer->capacity - buffer->length, format, arguments);
    va_end(arguments);
    buffer->length += length;
    return true;
}

static bool valid_utf8_sequence(const unsigned char *bytes, size_t available,
                                size_t *out_width)
{
    const unsigned char first = bytes[0];
    if (first <= 0x7Fu) {
        *out_width = 1u;
        return true;
    }
    size_t width;
    if (first >= 0xC2u && first <= 0xDFu) width = 2u;
    else if (first >= 0xE0u && first <= 0xEFu) width = 3u;
    else if (first >= 0xF0u && first <= 0xF4u) width = 4u;
    else return false;
    if (available < width) return false;
    for (size_t index = 1u; index < width; ++index) {
        if ((bytes[index] & 0xC0u) != 0x80u) return false;
    }
    if ((first == 0xE0u && bytes[1] < 0xA0u) ||
        (first == 0xEDu && bytes[1] >= 0xA0u) ||
        (first == 0xF0u && bytes[1] < 0x90u) ||
        (first == 0xF4u && bytes[1] >= 0x90u)) return false;
    *out_width = width;
    return true;
}

static bool append_xml_text(MathmlBuffer *buffer, const char *text, size_t length)
{
    static const char replacement[] = "\xEF\xBF\xBD";
    size_t offset = 0u;
    while (offset < length) {
        const unsigned char byte = (unsigned char)text[offset];
        if (byte == '&') {
            if (!append_cstr(buffer, "&amp;")) return false;
            ++offset;
        } else if (byte == '<') {
            if (!append_cstr(buffer, "&lt;")) return false;
            ++offset;
        } else if (byte == '>') {
            if (!append_cstr(buffer, "&gt;")) return false;
            ++offset;
        } else if (byte < 0x20u && byte != 0x09u && byte != 0x0Au && byte != 0x0Du) {
            if (!append(buffer, replacement, sizeof(replacement) - 1u)) return false;
            ++offset;
        } else {
            size_t width = 0u;
            if (!valid_utf8_sequence((const unsigned char *)text + offset,
                                     length - offset, &width)) {
                if (!append(buffer, replacement, sizeof(replacement) - 1u)) return false;
                ++offset;
            } else if (!append(buffer, text + offset, width)) {
                return false;
            } else {
                offset += width;
            }
        }
    }
    return true;
}

static bool append_tagged_text(MathmlBuffer *buffer, const char *tag,
                               const char *text, size_t length)
{
    return append_format(buffer, "<%s>", tag) &&
           append_xml_text(buffer, text, length) &&
           append_format(buffer, "</%s>", tag);
}

typedef struct MathSymbol {
    const char *name;
    const char *glyph;
    const char *tag;
} MathSymbol;

static const MathSymbol symbols[] = {
    {"alpha", "α", "mi"}, {"beta", "β", "mi"},
    {"gamma", "γ", "mi"}, {"delta", "δ", "mi"},
    {"epsilon", "ε", "mi"}, {"varepsilon", "ϵ", "mi"},
    {"zeta", "ζ", "mi"}, {"eta", "η", "mi"},
    {"theta", "θ", "mi"}, {"vartheta", "ϑ", "mi"},
    {"iota", "ι", "mi"}, {"kappa", "κ", "mi"},
    {"lambda", "λ", "mi"}, {"mu", "μ", "mi"},
    {"nu", "ν", "mi"}, {"xi", "ξ", "mi"},
    {"pi", "π", "mi"}, {"varpi", "ϖ", "mi"},
    {"rho", "ρ", "mi"}, {"varrho", "ϱ", "mi"},
    {"sigma", "σ", "mi"}, {"varsigma", "ς", "mi"},
    {"tau", "τ", "mi"}, {"upsilon", "υ", "mi"},
    {"phi", "φ", "mi"}, {"varphi", "ϕ", "mi"},
    {"chi", "χ", "mi"}, {"psi", "ψ", "mi"},
    {"omega", "ω", "mi"}, {"Gamma", "Γ", "mi"},
    {"Delta", "Δ", "mi"}, {"Theta", "Θ", "mi"},
    {"Lambda", "Λ", "mi"}, {"Xi", "Ξ", "mi"},
    {"Pi", "Π", "mi"}, {"Sigma", "Σ", "mi"},
    {"Phi", "Φ", "mi"}, {"Psi", "Ψ", "mi"},
    {"Omega", "Ω", "mi"}, {"infty", "∞", "mo"},
    {"partial", "∂", "mo"}, {"nabla", "∇", "mo"},
    {"sum", "∑", "mo"}, {"prod", "∏", "mo"},
    {"int", "∫", "mo"}, {"oint", "∮", "mo"},
    {"cdot", "·", "mo"}, {"times", "×", "mo"},
    {"div", "÷", "mo"}, {"approx", "≈", "mo"},
    {"ne", "≠", "mo"}, {"neq", "≠", "mo"},
    {"le", "≤", "mo"}, {"leq", "≤", "mo"},
    {"ge", "≥", "mo"}, {"geq", "≥", "mo"},
    {"in", "∈", "mo"}, {"to", "→", "mo"},
    {"rightarrow", "→", "mo"}, {"leftarrow", "←", "mo"},
    {"pm", "±", "mo"}, {"mp", "∓", "mo"},
    {"ldots", "…", "mo"}, {"cdots", "⋯", "mo"},
    {"sin", "sin", "mo"}, {"cos", "cos", "mo"},
    {"tan", "tan", "mo"}, {"log", "log", "mo"},
    {"ln", "ln", "mo"}, {"exp", "exp", "mo"},
    {"min", "min", "mo"}, {"max", "max", "mo"}
};

static const MathSymbol *lookup_symbol(const char *source, size_t length,
                                       size_t offset, size_t token_length)
{
    if (offset > length || token_length > length - offset || token_length == 0u ||
        source[offset] != '\\') return NULL;
    const size_t name_length = token_length - 1u;
    for (size_t index = 0u; index < sizeof(symbols) / sizeof(symbols[0]); ++index) {
        if (strlen(symbols[index].name) == name_length &&
            memcmp(source + offset + 1u, symbols[index].name, name_length) == 0) {
            return &symbols[index];
        }
    }
    return NULL;
}

static const TrytrMathNode *child_at(const TrytrMathNode *node, size_t index)
{
    if (node == NULL || index >= node->child_count) return NULL;
    const TrytrMathNode *child = node->first_child;
    while (index-- != 0u && child != NULL) child = child->next_sibling;
    return child;
}

static bool append_node(MathmlBuffer *buffer, const char *source, size_t length,
                        const TrytrMathNode *node, size_t depth);

static bool append_children(MathmlBuffer *buffer, const char *source,
                            size_t length, const TrytrMathNode *node,
                            size_t depth)
{
    size_t count = 0u;
    for (const TrytrMathNode *child = node->first_child; child != NULL;
         child = child->next_sibling) {
        if (++count > node->child_count ||
            !append_node(buffer, source, length, child, depth + 1u)) return false;
    }
    return count == node->child_count;
}

static bool append_node_source(MathmlBuffer *buffer, const char *source,
                               size_t length, const TrytrMathNode *node)
{
    if (node->source_offset > length || node->source_length > length - node->source_offset) {
        buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
        return false;
    }
    return append_xml_text(buffer, source + node->source_offset, node->source_length);
}

static bool append_script(MathmlBuffer *buffer, const char *source, size_t length,
                          const TrytrMathNode *node, size_t depth)
{
    const TrytrMathNode *base = child_at(node, 0u);
    const TrytrMathNode *script = child_at(node, 1u);
    if (base == NULL || script == NULL || node->child_count != 2u) {
        buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
        return false;
    }
    const TrytrMathNode *base_base = child_at(base, 0u);
    const TrytrMathNode *base_script = child_at(base, 1u);
    const bool outer_sub = node->kind == TRYTR_MATH_NODE_SUBSCRIPT;
    const bool combines = base->kind == (outer_sub
        ? TRYTR_MATH_NODE_SUPERSCRIPT : TRYTR_MATH_NODE_SUBSCRIPT) &&
        base_base != NULL && base_script != NULL && base->child_count == 2u;
    if (combines) {
        if (!append_cstr(buffer, "<msubsup>")) return false;
        if (!append_node(buffer, source, length, base_base, depth + 1u)) return false;
        if (!append_node(buffer, source, length, outer_sub ? script : base_script,
                         depth + 1u)) return false;
        if (!append_node(buffer, source, length, outer_sub ? base_script : script,
                         depth + 1u)) return false;
        return append_cstr(buffer, "</msubsup>");
    }
    const char *tag = outer_sub ? "msub" : "msup";
    return append_format(buffer, "<%s>", tag) &&
           append_node(buffer, source, length, base, depth + 1u) &&
           append_node(buffer, source, length, script, depth + 1u) &&
           append_format(buffer, "</%s>", tag);
}

static bool append_group(MathmlBuffer *buffer, const char *source, size_t length,
                         const TrytrMathNode *node, size_t depth)
{
    if (node->source_offset > length || node->source_length > length - node->source_offset) {
        buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
        return false;
    }
    const size_t start = node->source_offset;
    const size_t end = start + node->source_length;
    const char opening_character = node->source_length == 0u ? '\0' : source[start];
    const char expected_closing = opening_character == '(' ? ')' : ']';
    const bool has_opening = node->source_length != 0u &&
        (opening_character == '(' || opening_character == '[');
    const bool has_closing = has_opening && node->source_length >= 2u &&
        source[end - 1u] == expected_closing;
    if (!append_cstr(buffer, "<mrow>")) return false;
    if (has_opening) {
        const char opening = opening_character;
        if (!append_cstr(buffer, "<mo>") ||
            !append_xml_text(buffer, &opening, 1u) ||
            !append_cstr(buffer, "</mo>")) return false;
    }
    if (!append_children(buffer, source, length, node, depth)) return false;
    if (has_closing) {
        const char closing = source[end - 1u];
        if (!append_cstr(buffer, "<mo>") ||
            !append_xml_text(buffer, &closing, 1u) ||
            !append_cstr(buffer, "</mo>")) return false;
    }
    return append_cstr(buffer, "</mrow>");
}

static bool append_operator(MathmlBuffer *buffer, const char *source,
                            size_t length, const TrytrMathNode *node)
{
    if (node->operator_offset > length || node->operator_length > length - node->operator_offset) {
        buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
        return false;
    }
    const size_t offset = node->operator_offset;
    const size_t token_length = node->operator_length;
    const MathSymbol *symbol = lookup_symbol(source, length, offset, token_length);
    if (symbol != NULL) return append_tagged_text(buffer, "mo", symbol->glyph,
                                                  strlen(symbol->glyph));
    return append_tagged_text(buffer, "mo", source + offset, token_length);
}

static bool append_node(MathmlBuffer *buffer, const char *source, size_t length,
                        const TrytrMathNode *node, size_t depth)
{
    if (node == NULL) {
        buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
        return false;
    }
    if (depth > 64u) {
        buffer->status = TRYTR_STATUS_LIMIT_EXCEEDED;
        return false;
    }
    if (node->source_offset > length ||
        node->source_length > length - node->source_offset) {
        buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
        return false;
    }
    switch (node->kind) {
        case TRYTR_MATH_NODE_ROW:
            return append_cstr(buffer, "<mrow>") &&
                   append_children(buffer, source, length, node, depth) &&
                   append_cstr(buffer, "</mrow>");
        case TRYTR_MATH_NODE_IDENTIFIER:
            return append_tagged_text(buffer, "mi", source + node->source_offset,
                                      node->source_length);
        case TRYTR_MATH_NODE_NUMBER:
            return append_tagged_text(buffer, "mn", source + node->source_offset,
                                      node->source_length);
        case TRYTR_MATH_NODE_COMMAND: {
            const MathSymbol *symbol = lookup_symbol(source, length,
                node->source_offset, node->source_length);
            if (symbol != NULL) {
                return append_tagged_text(buffer, symbol->tag, symbol->glyph,
                                          strlen(symbol->glyph));
            }
            if (!append_cstr(buffer, "<mtext>")) return false;
            if (!append_node_source(buffer, source, length, node)) return false;
            return append_cstr(buffer, "</mtext>");
        }
        case TRYTR_MATH_NODE_TEXT: {
            if (node->source_offset > length || node->source_length > length - node->source_offset) {
                buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
                return false;
            }
            const char *text = source + node->source_offset;
            const char *tag = node->source_length == 1u &&
                strchr("()[],:;!|", text[0]) != NULL ? "mo" : "mtext";
            return append_tagged_text(buffer, tag, text, node->source_length);
        }
        case TRYTR_MATH_NODE_GROUP:
            return append_group(buffer, source, length, node, depth);
        case TRYTR_MATH_NODE_FRACTION: {
            const TrytrMathNode *numerator = child_at(node, 0u);
            const TrytrMathNode *denominator = child_at(node, 1u);
            if (numerator == NULL || denominator == NULL || node->child_count != 2u) {
                buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
                return false;
            }
            return append_cstr(buffer, "<mfrac>") &&
                   append_node(buffer, source, length, numerator, depth + 1u) &&
                   append_node(buffer, source, length, denominator, depth + 1u) &&
                   append_cstr(buffer, "</mfrac>");
        }
        case TRYTR_MATH_NODE_SQUARE_ROOT: {
            const TrytrMathNode *radicand = child_at(node, 0u);
            if (radicand == NULL || node->child_count != 1u) {
                buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
                return false;
            }
            return append_cstr(buffer, "<msqrt>") &&
                   append_node(buffer, source, length, radicand, depth + 1u) &&
                   append_cstr(buffer, "</msqrt>");
        }
        case TRYTR_MATH_NODE_SUBSCRIPT:
        case TRYTR_MATH_NODE_SUPERSCRIPT:
            return append_script(buffer, source, length, node, depth);
        case TRYTR_MATH_NODE_UNARY: {
            const TrytrMathNode *operand = child_at(node, 0u);
            if (operand == NULL || node->child_count != 1u) {
                buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
                return false;
            }
            return append_cstr(buffer, "<mrow>") &&
                   append_operator(buffer, source, length, node) &&
                   append_node(buffer, source, length, operand, depth + 1u) &&
                   append_cstr(buffer, "</mrow>");
        }
        case TRYTR_MATH_NODE_BINARY:
        case TRYTR_MATH_NODE_RELATION: {
            const TrytrMathNode *left = child_at(node, 0u);
            const TrytrMathNode *right = child_at(node, 1u);
            if (left == NULL || right == NULL || node->child_count != 2u) {
                buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
                return false;
            }
            return append_cstr(buffer, "<mrow>") &&
                   append_node(buffer, source, length, left, depth + 1u) &&
                   append_operator(buffer, source, length, node) &&
                   append_node(buffer, source, length, right, depth + 1u) &&
                   append_cstr(buffer, "</mrow>");
        }
        case TRYTR_MATH_NODE_RAW:
            if (!append_cstr(buffer, "<mtext>")) return false;
            if (!append_node_source(buffer, source, length, node)) return false;
            return append_cstr(buffer, "</mtext>");
    }
    buffer->status = TRYTR_STATUS_INVALID_DOCUMENT;
    return false;
}

static bool render_math_element(const TrytrDocumentElement *element,
                                void *user_data)
{
    MathmlRenderState *state = user_data;
    if (element->kind != TRYTR_ELEMENT_MATH) return true;
    if (element->text == NULL && element->text_length != 0u) {
        state->output.status = TRYTR_STATUS_INVALID_DOCUMENT;
        return false;
    }
    if (state->expression_count == 0u) {
        if (!append_cstr(&state->output,
                "<math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"block\"><mtable>")) {
            return false;
        }
    }
    if (!append_cstr(&state->output, "<mtr><mtd>")) return false;
    if (element->math_tree != NULL) {
        const char *source = element->text == NULL ? "" : element->text;
        if (!append_node(&state->output, source, element->text_length,
                         element->math_tree, 0u)) return false;
    } else {
        if (!append_tagged_text(&state->output, "mtext",
                                element->text == NULL ? "" : element->text,
                                element->text_length)) return false;
    }
    if (!append_cstr(&state->output, "</mtd></mtr>")) return false;
    ++state->expression_count;
    return true;
}

TrytrStatus trytr_mathml_render(TrytrDocument *document,
                                char **out_mathml, size_t *out_length)
{
    if (out_mathml != NULL) *out_mathml = NULL;
    if (out_length != NULL) *out_length = 0u;
    if (document == NULL || out_mathml == NULL || out_length == NULL) {
        return TRYTR_STATUS_INVALID_ARGUMENT;
    }
    MathmlRenderState state = { .output.status = TRYTR_STATUS_OK };
    TrytrStatus status = trytr_document_execute(document, render_math_element, &state);
    if (state.output.status != TRYTR_STATUS_OK) status = state.output.status;
    if (status == TRYTR_STATUS_OK && state.expression_count != 0u &&
        !append_cstr(&state.output, "</mtable></math>")) {
        status = state.output.status;
    }
    if (status == TRYTR_STATUS_OK && state.output.data == NULL) {
        state.output.data = calloc(1u, 1u);
        if (state.output.data == NULL) status = TRYTR_STATUS_NO_MEMORY;
    }
    if (status == TRYTR_STATUS_OK) {
        *out_mathml = state.output.data;
        *out_length = state.output.length;
    } else {
        free(state.output.data);
    }
    return status;
}

static TrytrStatus render_fragment(
    const TrytrDocumentElement *element, bool inline_math, char **out_mathml, size_t *out_length)
{
    if (out_mathml != NULL) *out_mathml = NULL;
    if (out_length != NULL) *out_length = 0u;
    if (element == NULL || element->kind != TRYTR_ELEMENT_MATH ||
        (element->text == NULL && element->text_length != 0u) ||
        out_mathml == NULL || out_length == NULL) return TRYTR_STATUS_INVALID_ARGUMENT;
    MathmlBuffer buffer = {.status = TRYTR_STATUS_OK};
    const char *source = element->text == NULL ? "" : element->text;
    if (!append_format(&buffer, "<math xmlns=\"http://www.w3.org/1998/Math/MathML\" display=\"%s\">",
                        inline_math ? "inline" : "block") ||
        !(element->math_tree != NULL
            ? append_node(&buffer, source, element->text_length, element->math_tree, 0u)
            : append_tagged_text(&buffer, "mtext", source, element->text_length)) ||
        !append_cstr(&buffer, "</math>")) {
        free(buffer.data);
        return buffer.status;
    }
    *out_mathml = buffer.data;
    *out_length = buffer.length;
    return TRYTR_STATUS_OK;
}

TrytrStatus trytr_mathml_render_element(
    const TrytrDocumentElement *element, char **out_mathml, size_t *out_length)
{
    return render_fragment(element, false, out_mathml, out_length);
}

TrytrStatus trytr_mathml_render_inline_run(
    const TrytrDocumentElement *element, const TrytrInlineRun *run,
    char **out_mathml, size_t *out_length)
{
    if (out_mathml != NULL) *out_mathml = NULL;
    if (out_length != NULL) *out_length = 0u;
    if (element == NULL || run == NULL || run->kind != TRYTR_INLINE_MATH ||
        (element->kind != TRYTR_ELEMENT_TEXT && element->kind != TRYTR_ELEMENT_HEADING) ||
        (element->text == NULL && element->text_length != 0u) ||
        run->source_offset > element->text_length ||
        run->source_length > element->text_length - run->source_offset ||
        run->content_offset < run->source_offset ||
        run->content_offset > run->source_offset + run->source_length ||
        run->content_length > run->source_offset + run->source_length - run->content_offset)
        return TRYTR_STATUS_INVALID_ARGUMENT;
    const TrytrDocumentElement math = {.kind = TRYTR_ELEMENT_MATH,
        .text = element->text == NULL ? "" : element->text + run->content_offset,
        .text_length = run->content_length, .math_tree = run->math_tree};
    return render_fragment(&math, true, out_mathml, out_length);
}
