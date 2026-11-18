#include "trytr/proof.h"
#include "trytr/algebra.h"
#include "trytr/limit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROOF_FILE_LIMIT 65536L

static bool has_suffix(const char *text, const char *suffix)
{
    const size_t text_length = strlen(text);
    const size_t suffix_length = strlen(suffix);
    return text_length >= suffix_length &&
           memcmp(text + text_length - suffix_length, suffix, suffix_length) == 0;
}

static int check_algebra_document(const char *path, char *source, size_t size)
{
    size_t cursor = 0U;
    size_t line_number = 0U;
    size_t checked = 0U;
    int exit_status = 0;
    while (cursor < size) {
        const size_t start = cursor;
        while (cursor < size && source[cursor] != '\n') {
            ++cursor;
        }
        const size_t end = cursor;
        if (cursor < size) {
            source[cursor++] = '\0';
        }
        ++line_number;
        source[end] = '\0';
        char *line = source + start;
        while (*line == ' ' || *line == '\t' || *line == '\r') {
            ++line;
        }
        size_t length = strlen(line);
        while (length > 0U && (line[length - 1U] == ' ' || line[length - 1U] == '\t' ||
                               line[length - 1U] == '\r')) {
            line[--length] = '\0';
        }
        if (length == 0U || line[0] == '#' || line[0] == '%' ||
            strcmp(line, "\\begin{align}") == 0 || strcmp(line, "\\begin{align*}") == 0 ||
            strcmp(line, "\\end{align}") == 0 || strcmp(line, "\\end{align*}") == 0 ||
            strcmp(line, "$$") == 0 || strcmp(line, "\\[") == 0 ||
            strcmp(line, "\\]") == 0) {
            continue;
        }

        /* Alignment ampersands and a final LaTeX row break are presentation only. */
        size_t write = 0U;
        for (size_t read = 0U; read < length; ++read) {
            if (line[read] != '&') {
                line[write++] = line[read];
            }
        }
        line[write] = '\0';
        if (write >= 2U && line[write - 1U] == '\\' && line[write - 2U] == '\\') {
            line[write - 2U] = '\0';
        }
        if (line[0] == '\0' || strcmp(line, "\\\\") == 0) {
            continue;
        }
        const TrytrAlgebraResult result = trytr_algebra_check_rational(line);
        ++checked;
        if (result.status == TRYTR_ALGEBRA_VERIFIED) {
            printf("%s:%zu: %s\n", path, line_number, result.message);
        } else if (result.status == TRYTR_ALGEBRA_CONDITIONAL) {
            fprintf(stderr, "%s:%zu: conditional: %s", path, line_number, result.message);
            for (size_t i = 0u; i < result.condition_count; ++i) {
                const TrytrAlgebraCondition condition = result.conditions[i];
                fprintf(stderr, "%s(%.*s) != 0", i == 0u ? ": " : "; ",
                        (int)condition.length, line + condition.offset);
            }
            fputc('\n', stderr);
            exit_status = 2;
        } else if (result.status == TRYTR_ALGEBRA_NOT_IDENTITY) {
            fprintf(stderr, "%s:%zu: not verified: %s\n", path, line_number,
                    result.message);
            if (exit_status < 1) {
                exit_status = 1;
            }
        } else {
            fprintf(stderr, "%s:%zu: unable to check: %s\n", path, line_number,
                    result.message);
            exit_status = 2;
        }
    }
    if (checked == 0U) {
        fprintf(stderr, "%s: no equation rows to check\n", path);
        return 2;
    }
    return exit_status;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: trytr-proof FILE\n"
                        "  .trproof: Peano equality proof\n"
                        "  .trlimit: polynomial limit certificate\n"
                        "  .trmath: exact polynomial/rational identity review\n");
        return 2;
    }
    FILE *input = fopen(argv[1], "rb");
    if (input == NULL) {
        perror(argv[1]);
        return 2;
    }
    if (fseek(input, 0L, SEEK_END) != 0) {
        perror("seek");
        (void)fclose(input);
        return 2;
    }
    const long file_size = ftell(input);
    if (file_size < 0L || file_size > PROOF_FILE_LIMIT || fseek(input, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "proof file must be at most 64 KiB\n");
        (void)fclose(input);
        return 2;
    }
    const size_t size = (size_t)file_size;
    char *source = malloc(size + 1U);
    if (source == NULL) {
        fprintf(stderr, "unable to allocate proof input\n");
        (void)fclose(input);
        return 2;
    }
    const size_t read_size = fread(source, 1U, size, input);
    const bool read_ok = read_size == size && ferror(input) == 0;
    (void)fclose(input);
    if (!read_ok) {
        fprintf(stderr, "unable to read proof file\n");
        free(source);
        return 2;
    }
    if (memchr(source, '\0', size) != NULL) {
        fprintf(stderr, "proof file contains a NUL byte\n");
        free(source);
        return 2;
    }
    source[size] = '\0';
    if (has_suffix(argv[1], ".trmath")) {
        const int status = check_algebra_document(argv[1], source, size);
        free(source);
        return status;
    }
    size_t first = 0U;
    while (first < size && (source[first] == ' ' || source[first] == '\t' ||
                            source[first] == '\r' || source[first] == '\n')) {
        ++first;
    }
    const TrytrProofResult result =
        first < size && source[first] == '\\' ? trytr_limit_check(source)
                                                : trytr_proof_check(source);
    free(source);
    if (!result.valid) {
        fprintf(stderr, "%s:%zu: %s\n", argv[1], result.offset, result.message);
        return 1;
    }
    printf("%s: %s\n", argv[1], result.message);
    return 0;
}
