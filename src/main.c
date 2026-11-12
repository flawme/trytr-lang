#include "trytr/document.h"
#include "trytr/trytr.h"
#include "svg_render.h"

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#define TRYTR_CLI_SOURCE_LIMIT (1024u * 1024u)

static void print_usage(FILE *stream)
{
    fputs("usage: trytr [--version] [--help] [--dump-ir|--render-svg] [FILE|-]\n", stream);
}

static bool ignore_element(const TrytrDocumentElement *element, void *user_data)
{
    (void)element;
    (void)user_data;
    return true;
}

static const char *element_kind_name(TrytrElementKind kind)
{
    switch (kind) {
        case TRYTR_ELEMENT_HEADING: return "heading";
        case TRYTR_ELEMENT_TEXT: return "text";
        case TRYTR_ELEMENT_MATH: return "math";
        case TRYTR_ELEMENT_RECT: return "rect";
        case TRYTR_ELEMENT_LINE: return "line";
        case TRYTR_ELEMENT_CIRCLE: return "circle";
        case TRYTR_ELEMENT_PATH: return "path";
        case TRYTR_ELEMENT_GROUP_BEGIN: return "group-begin";
        case TRYTR_ELEMENT_GROUP_END: return "group-end";
        case TRYTR_ELEMENT_ELLIPSE: return "ellipse";
    }
    return "unknown";
}

static bool print_escaped(const char *text, size_t length)
{
    if (text == NULL && length != 0u) return false;
    if (fputc('"', stdout) == EOF) return false;
    for (size_t index = 0u; index < length; ++index) {
        const unsigned char byte = (unsigned char)text[index];
        switch (byte) {
            case '"': if (fputs("\\\"", stdout) == EOF) return false; break;
            case '\\': if (fputs("\\\\", stdout) == EOF) return false; break;
            case '\n': if (fputs("\\n", stdout) == EOF) return false; break;
            case '\r': if (fputs("\\r", stdout) == EOF) return false; break;
            case '\t': if (fputs("\\t", stdout) == EOF) return false; break;
            default:
                if (byte < 0x20u) {
                    if (fprintf(stdout, "\\u%04x", (unsigned int)byte) < 0) return false;
                } else if (fputc((int)byte, stdout) == EOF) {
                    return false;
                }
                break;
        }
    }
    return fputc('"', stdout) != EOF;
}

static bool print_ir_element(const TrytrDocumentElement *element, void *user_data)
{
    (void)user_data;
    if (printf("%04zu %s", element->element_index,
               element_kind_name(element->kind)) < 0) return false;
    if (element->id != NULL) {
        if (fputs(" id=", stdout) == EOF || !print_escaped(element->id, strlen(element->id))) {
            return false;
        }
    }
    if (element->kind == TRYTR_ELEMENT_RECT) {
        if (printf(" x=%" PRId64 " y=%" PRId64 " width=%" PRId64 " height=%" PRId64,
                   element->x, element->y, element->width, element->height) < 0) return false;
    } else if (element->kind == TRYTR_ELEMENT_LINE) {
        if (printf(" x1=%" PRId64 " y1=%" PRId64 " x2=%" PRId64 " y2=%" PRId64,
                   element->x, element->y, element->x2, element->y2) < 0) return false;
    } else if (element->kind == TRYTR_ELEMENT_CIRCLE) {
        if (printf(" cx=%" PRId64 " cy=%" PRId64 " radius=%" PRId64,
                   element->x, element->y, element->radius) < 0) return false;
    } else if (element->kind == TRYTR_ELEMENT_ELLIPSE) {
        if (printf(" cx=%" PRId64 " cy=%" PRId64 " rx=%" PRId64 " ry=%" PRId64,
                   element->x, element->y, element->radius_x, element->radius_y) < 0) return false;
    } else if (element->kind == TRYTR_ELEMENT_PATH) {
        if (fputs(" d=", stdout) == EOF ||
            !print_escaped(element->text, element->text_length)) return false;
    } else if (element->kind == TRYTR_ELEMENT_GROUP_BEGIN) {
        if (printf(" translate_x=%" PRId64 " translate_y=%" PRId64,
                   element->x, element->y) < 0) return false;
    } else if (element->kind == TRYTR_ELEMENT_GROUP_END) {
        /* The matching group ID and source index identify the closed scope. */
    } else {
        if (fputs(" text=", stdout) == EOF ||
            !print_escaped(element->text, element->text_length)) return false;
    }
    return fputc('\n', stdout) != EOF;
}

static const char *severity_name(TrytrDiagnosticSeverity severity)
{
    switch (severity) {
        case TRYTR_DIAGNOSTIC_INFO: return "info";
        case TRYTR_DIAGNOSTIC_WARNING: return "warning";
        case TRYTR_DIAGNOSTIC_ERROR: return "error";
        case TRYTR_DIAGNOSTIC_FATAL: return "fatal";
    }
    return "unknown";
}

static int read_source(FILE *stream, char **out_source, size_t *out_length)
{
    size_t capacity = TRYTR_CLI_SOURCE_LIMIT + 1u;
    char *source = malloc(capacity);
    if (source == NULL) {
        fputs("trytr: unable to allocate source buffer\n", stderr);
        return 2;
    }

    size_t length = 0;
    while (length < capacity) {
        size_t count = fread(source + length, 1, capacity - length, stream);
        length += count;
        if (count == 0) {
            if (ferror(stream)) {
                fputs("trytr: failed to read source\n", stderr);
                free(source);
                return 2;
            }
            break;
        }
    }

    if (length > TRYTR_CLI_SOURCE_LIMIT) {
        fputs("trytr: source exceeds the implementation size limit\n", stderr);
        free(source);
        return 2;
    }

    *out_source = source;
    *out_length = length;
    return 0;
}

static int process_source(const char *name, const char *source, size_t length,
                          bool dump_ir, bool render_svg)
{
    TrytrDocument *document = NULL;
    TrytrStatus status = trytr_document_parse(source, length, &document);
    if (document == NULL) {
        fprintf(stderr, "trytr: %s\n", trytr_status_string(status));
        return status == TRYTR_STATUS_NO_MEMORY ? 2 : 1;
    }

    TrytrStatus validation = trytr_document_validate(document);
    if (status == TRYTR_STATUS_OK) status = validation;

    for (size_t index = 0; index < trytr_document_diagnostic_count(document); ++index) {
        const TrytrDiagnostic *diagnostic = trytr_document_diagnostic_at(document, index);
        fprintf(
            stderr,
            "%s:%zu:%zu: %s[%s]: %s\n",
            name,
            diagnostic->location.line,
            diagnostic->location.column,
            severity_name(diagnostic->severity),
            diagnostic->code,
            diagnostic->message
        );
    }

    if (status == TRYTR_STATUS_OK) {
        if (render_svg) {
            char *svg = NULL;
            size_t svg_length = 0u;
            status = trytr_svg_render(document, &svg, &svg_length);
            if (status == TRYTR_STATUS_OK &&
                fwrite(svg, 1u, svg_length, stdout) != svg_length) {
                status = TRYTR_STATUS_EXECUTION_FAILED;
            }
            free(svg);
            if (status != TRYTR_STATUS_OK) {
                fprintf(stderr, "trytr: render failed: %s\n", trytr_status_string(status));
            }
        } else {
            TrytrElementVisitor visitor = dump_ir ? print_ir_element : ignore_element;
            status = trytr_document_execute(document, visitor, NULL);
            if (status != TRYTR_STATUS_OK) {
                fprintf(stderr, "trytr: execution failed: %s\n", trytr_status_string(status));
            }
        }
    }

    trytr_document_destroy(document);
    return status == TRYTR_STATUS_OK ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        if (printf("trytr %s\n", trytr_version()) < 0) return 2;
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(stdout);
        return 0;
    }
    if (argc > 3) {
        print_usage(stderr);
        return 2;
    }

    bool dump_ir = false;
    bool render_svg = false;
    const char *input_path = NULL;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--dump-ir") == 0) {
            if (dump_ir || render_svg) {
                fputs("trytr: choose only one output mode\n", stderr);
                return 2;
            }
            dump_ir = true;
        } else if (strcmp(argv[index], "--render-svg") == 0) {
            if (dump_ir || render_svg) {
                fputs("trytr: choose only one output mode\n", stderr);
                return 2;
            }
            render_svg = true;
        } else if (argv[index][0] == '-' && argv[index][1] == '-') {
            fprintf(stderr, "trytr: unknown option %s\n", argv[index]);
            print_usage(stderr);
            return 2;
        } else if (input_path == NULL) {
            input_path = argv[index];
        } else {
            print_usage(stderr);
            return 2;
        }
    }

    const char *name = input_path == NULL ? "<stdin>" : input_path;
    FILE *stream = stdin;
    if (input_path != NULL && !(input_path[0] == '-' && input_path[1] == '\0')) {
        stream = fopen(input_path, "rb");
        if (stream == NULL) {
            fprintf(stderr, "trytr: unable to open %s\n", input_path);
            return 2;
        }
    } else if (input_path != NULL) {
        name = "<stdin>";
    }

    char *source = NULL;
    size_t length = 0;
    int result = read_source(stream, &source, &length);
    if (stream != stdin) fclose(stream);
    if (result != 0) return result;

    result = process_source(name, source, length, dump_ir, render_svg);
    if (fflush(stdout) != 0) {
        fputs("trytr: failed to write output\n", stderr);
        if (result == 0) result = 2;
    }
    free(source);
    return result;
}
