#define _POSIX_C_SOURCE 200809L

#include "pdf_render.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(TRYTR_CHROMIUM_EXECUTABLE) && defined(__unix__)
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define TRYTR_PDF_OUTPUT_LIMIT (128u * 1024u * 1024u)

static bool parse_dimension(const char *svg, size_t length, const char *name,
                            uint64_t *out_value)
{
    size_t name_length = strlen(name);
    for (size_t index = 0; index + name_length + 2u < length; ++index) {
        if (memcmp(svg + index, name, name_length) != 0 ||
            svg[index + name_length] != '=' ||
            svg[index + name_length + 1u] != '"') {
            continue;
        }
        size_t cursor = index + name_length + 2u;
        uint64_t value = 0u;
        size_t digits = 0u;
        while (cursor < length && svg[cursor] >= '0' && svg[cursor] <= '9') {
            unsigned int digit = (unsigned int)(svg[cursor] - '0');
            if (value > (UINT64_MAX - digit) / 10u) return false;
            value = value * 10u + digit;
            ++cursor;
            ++digits;
        }
        if (digits == 0u || cursor >= length || svg[cursor] != '"' || value == 0u) {
            return false;
        }
        *out_value = value;
        return true;
    }
    return false;
}

static void remove_tree(const char *path)
{
    DIR *directory = opendir(path);
    if (directory == NULL) {
        (void)unlink(path);
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        size_t path_length = strlen(path);
        size_t name_length = strlen(entry->d_name);
        if (path_length > SIZE_MAX - name_length - 2u) continue;
        char *child = malloc(path_length + name_length + 2u);
        if (child == NULL) continue;
        (void)snprintf(child, path_length + name_length + 2u,
                       "%s/%s", path, entry->d_name);
        struct stat info;
        if (lstat(child, &info) == 0 && S_ISDIR(info.st_mode)) {
            remove_tree(child);
        } else {
            (void)unlink(child);
        }
        free(child);
    }
    (void)closedir(directory);
    (void)rmdir(path);
}

static bool write_html(const char *path, const char *svg, size_t svg_length,
                       uint64_t width, uint64_t height)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) return false;
    int header_length = fprintf(
        file,
        "<!doctype html><meta charset=\"utf-8\"><style>"
        "@page{size:%" PRIu64 "px %" PRIu64 "px;margin:0}"
        "html,body{margin:0;padding:0;width:%" PRIu64 "px;height:%" PRIu64
        "px;overflow:hidden}svg{display:block;width:100%%;height:100%%}"
        "</style>\n",
        width, height, width, height);
    bool ok = header_length > 0 &&
              fwrite(svg, 1u, svg_length, file) == svg_length &&
              fputc('\n', file) != EOF && fflush(file) == 0;
    if (fclose(file) != 0) ok = false;
    return ok;
}

static bool read_pdf(const char *path, char **out_pdf, size_t *out_length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long end = ok ? ftell(file) : -1L;
    if (end < 8L || (uintmax_t)end > TRYTR_PDF_OUTPUT_LIMIT ||
        fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        return false;
    }
    size_t length = (size_t)end;
    char *data = malloc(length);
    if (data == NULL) {
        (void)fclose(file);
        return false;
    }
    ok = fread(data, 1u, length, file) == length;
    if (fclose(file) != 0) ok = false;
    if (!ok) {
        free(data);
        return false;
    }

    bool has_eof_marker = false;
    size_t start = length > 1024u ? length - 1024u : 0u;
    for (size_t index = start; index + 5u <= length; ++index) {
        if (memcmp(data + index, "%%EOF", 5u) == 0) {
            has_eof_marker = true;
            break;
        }
    }
    if (memcmp(data, "%PDF-", 5u) != 0 || !has_eof_marker) {
        free(data);
        return false;
    }
    *out_pdf = data;
    *out_length = length;
    return true;
}

bool trytr_pdf_export_available(void)
{
    return true;
}

TrytrStatus trytr_pdf_render_svg(const char *svg, size_t svg_length,
                                 char **out_pdf, size_t *out_length)
{
    if (out_pdf == NULL || out_length == NULL) {
        return TRYTR_STATUS_INVALID_ARGUMENT;
    }
    *out_pdf = NULL;
    *out_length = 0u;
    if (svg == NULL || svg_length < 5u ||
        memcmp(svg, "<svg ", 5u) != 0) {
        return TRYTR_STATUS_INVALID_ARGUMENT;
    }

    uint64_t width = 0u;
    uint64_t height = 0u;
    if (!parse_dimension(svg, svg_length, "width", &width) ||
        !parse_dimension(svg, svg_length, "height", &height)) {
        return TRYTR_STATUS_INVALID_ARGUMENT;
    }

    char directory[] = "/tmp/trytr-pdf-XXXXXX";
    if (mkdtemp(directory) == NULL) return TRYTR_STATUS_EXECUTION_FAILED;
    char html_path[4096];
    char pdf_path[4096];
    char profile_path[4096];
    int html_length = snprintf(html_path, sizeof(html_path), "%s/document.html", directory);
    int pdf_length = snprintf(pdf_path, sizeof(pdf_path), "%s/document.pdf", directory);
    int profile_length = snprintf(profile_path, sizeof(profile_path), "%s/profile", directory);
    bool paths_ok = html_length >= 0 && html_length < (int)sizeof(html_path) &&
                    pdf_length >= 0 && pdf_length < (int)sizeof(pdf_path) &&
                    profile_length >= 0 && profile_length < (int)sizeof(profile_path);
    bool rendered = paths_ok && write_html(html_path, svg, svg_length, width, height) &&
                    trytr_pdf_run_chromium(html_path, pdf_path, profile_path) &&
                    read_pdf(pdf_path, out_pdf, out_length);
    remove_tree(directory);
    return rendered ? TRYTR_STATUS_OK : TRYTR_STATUS_EXECUTION_FAILED;
}

#else

bool trytr_pdf_export_available(void)
{
    return false;
}

TrytrStatus trytr_pdf_render_svg(const char *svg, size_t svg_length,
                                 char **out_pdf, size_t *out_length)
{
    (void)svg;
    (void)svg_length;
    (void)out_pdf;
    (void)out_length;
    return TRYTR_STATUS_EXECUTION_FAILED;
}

#endif
