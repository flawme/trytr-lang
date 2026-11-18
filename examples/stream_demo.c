#include "trytr/stream.h"
#include "trytr/html.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool report(TrytrStream *stream, TrytrStatus status)
{
    if (status == TRYTR_STATUS_OK) return true;
    fprintf(stderr, "%s\n", trytr_status_string(status));
    for (size_t i = 0u; i < trytr_stream_diagnostic_count(stream); ++i) {
        const TrytrDiagnostic *diagnostic = trytr_stream_diagnostic_at(stream, i);
        fprintf(stderr, "%s at %zu:%zu: %s\n", diagnostic->code,
                diagnostic->location.line, diagnostic->location.column, diagnostic->message);
    }
    return false;
}

static bool append(TrytrStream *stream, const char *frame)
{
    bool changed = false;
    return report(stream, trytr_stream_begin_append(stream, trytr_stream_revision(stream))) &&
           report(stream, trytr_stream_feed(stream, frame, strlen(frame))) &&
           report(stream, trytr_stream_commit_append(stream, &changed)) && changed;
}

static bool save(TrytrStream *stream, const char *prefix, const char *name)
{
    char path[4096];
    const int count = snprintf(path, sizeof(path), "%s-%s.html", prefix, name);
    if (count < 0 || (size_t)count >= sizeof(path)) return false;
    TrytrSnapshot *snapshot = trytr_stream_acquire_snapshot(stream);
    if (snapshot == NULL) return false;
    char *html = NULL;
    size_t length = 0u;
    bool ok = report(stream, trytr_html_render(trytr_snapshot_document(snapshot), &html, &length));
    if (ok) {
        FILE *file = fopen(path, "wb");
        ok = file != NULL;
        if (file != NULL) {
            ok = fwrite(html, 1u, length, file) == length;
            if (fclose(file) != 0) ok = false;
        }
    }
    if (ok) printf("revision %" PRIu64 ": %s\n", trytr_snapshot_revision(snapshot), path);
    free(html);
    trytr_snapshot_release(snapshot);
    return ok;
}

static bool demonstrate(TrytrStream *stream, const char *prefix)
{
    if (!append(stream,
        "@param #rate(min:1,max:4,value:2,step:1){Frequency}\n"
        "@heading{A streamed Trytr explanation}") || !save(stream, prefix, "1")) return false;
    if (!append(stream,
        "@text(inline-math:true){Let \\(f(x) = \\sin(#rate x)\\). "
        "The equation is native inline math, followed by a formula plot.}") ||
        !save(stream, prefix, "2")) return false;

    const char *partial =
        "@plot(48,20,480,180)(x-min:-3,x-max:3,samples:25,markers:false){y=sin(#rate*x)";
    if (!report(stream, trytr_stream_begin_append(stream, 2u)) ||
        !report(stream, trytr_stream_feed(stream, partial, strlen(partial)))) return false;
    bool changed = true;
    const TrytrStatus incomplete = trytr_stream_commit_append(stream, &changed);
    if (incomplete != TRYTR_STATUS_INVALID_DOCUMENT || changed ||
        trytr_stream_revision(stream) != 2u) return false;
    puts("incomplete plot kept revision 2 visible");
    if (!save(stream, prefix, "pending") ||
        !report(stream, trytr_stream_feed(stream, "}", 1u)) ||
        !report(stream, trytr_stream_commit_append(stream, &changed)) || !changed ||
        !save(stream, prefix, "3")) return false;
    return report(stream, trytr_stream_finish(stream));
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s OUTPUT_PREFIX\n", argv[0]);
        return EXIT_FAILURE;
    }
    TrytrStream *stream = NULL;
    if (!report(NULL, trytr_stream_create(NULL, &stream))) return EXIT_FAILURE;
    const bool ok = demonstrate(stream, argv[1]);
    trytr_stream_destroy(stream);
    if (!ok) fputs("stream demonstration failed\n", stderr);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
