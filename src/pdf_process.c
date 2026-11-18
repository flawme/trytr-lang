#define _POSIX_C_SOURCE 200809L
#include "pdf_render.h"

#if defined(TRYTR_CHROMIUM_EXECUTABLE) && defined(__unix__)
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

bool trytr_pdf_run_chromium(const char *html_path, const char *pdf_path,
                         const char *profile_path)
{
    char pdf_argument[4096];
    char profile_argument[4096];
    char url[4096];
    int pdf_length = snprintf(pdf_argument, sizeof(pdf_argument),
                              "--print-to-pdf=%s", pdf_path);
    int profile_length = snprintf(profile_argument, sizeof(profile_argument),
                                  "--user-data-dir=%s", profile_path);
    int url_length = snprintf(url, sizeof(url), "file://%s", html_path);
    if (pdf_length < 0 || pdf_length >= (int)sizeof(pdf_argument) ||
        profile_length < 0 || profile_length >= (int)sizeof(profile_argument) ||
        url_length < 0 || url_length >= (int)sizeof(url)) {
        return false;
    }

    char *arguments[16];
    size_t count = 0u;
    arguments[count++] = (char *)TRYTR_CHROMIUM_EXECUTABLE;
    arguments[count++] = (char *)"--headless";
    arguments[count++] = (char *)"--disable-gpu";
    arguments[count++] = (char *)"--disable-dev-shm-usage";
    arguments[count++] = (char *)"--disable-background-networking";
    arguments[count++] = (char *)"--disable-extensions";
    arguments[count++] = (char *)"--no-first-run";
    arguments[count++] = (char *)"--no-pdf-header-footer";
    if (geteuid() == 0) arguments[count++] = (char *)"--no-sandbox";
    arguments[count++] = profile_argument;
    arguments[count++] = pdf_argument;
    arguments[count++] = url;
    arguments[count] = NULL;

    int diagnostic_pipe[2];
    if (pipe(diagnostic_pipe) != 0) return false;
    pid_t child = fork();
    if (child < 0) {
        (void)close(diagnostic_pipe[0]);
        (void)close(diagnostic_pipe[1]);
        return false;
    }
    if (child == 0) {
        (void)close(diagnostic_pipe[0]);
        (void)dup2(diagnostic_pipe[1], STDERR_FILENO);
        (void)close(diagnostic_pipe[1]);
        int null_descriptor = open("/dev/null", O_WRONLY);
        if (null_descriptor >= 0) {
            (void)dup2(null_descriptor, STDOUT_FILENO);
            (void)close(null_descriptor);
        }
        execv(TRYTR_CHROMIUM_EXECUTABLE, arguments);
        _exit(127);
    }
    (void)close(diagnostic_pipe[1]);
    char diagnostic[4096], chunk[1024];
    size_t used = 0u;
    for (;;) {
        const ssize_t count_read = read(diagnostic_pipe[0], chunk, sizeof(chunk));
        if (count_read < 0 && errno == EINTR) continue;
        if (count_read <= 0) break;
        size_t copied = (size_t)count_read;
        if (copied > sizeof(diagnostic) - used) copied = sizeof(diagnostic) - used;
        memcpy(diagnostic + used, chunk, copied);
        used += copied;
    }
    (void)close(diagnostic_pipe[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) {
        fputs("trytr: Chromium PDF export failed:\n", stderr);
        if (used != 0u) (void)fwrite(diagnostic, 1u, used, stderr);
    }
    return ok;
}

#endif
