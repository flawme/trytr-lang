#define _POSIX_C_SOURCE 200809L
#include "pdf_render.h"

#if defined(TRYTR_CHROMIUM_EXECUTABLE) && defined(__unix__)
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef TRYTR_PDF_TIMEOUT_MS
#define TRYTR_PDF_TIMEOUT_MS 30000u
#endif

static bool monotonic_ms(uint64_t *value)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return false;
    *value = (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
    return true;
}

static bool wait_browser(pid_t child, int descriptor, char *diagnostic, size_t capacity, size_t *used)
{
    uint64_t start = 0u, now = 0u;
    const int flags = fcntl(descriptor, F_GETFL);
    bool ready = flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) == 0 && monotonic_ms(&start);
    bool exited = false, timed_out = false;
    struct pollfd input = {.fd = descriptor, .events = POLLIN};
    while (ready) {
        if (!monotonic_ms(&now)) break;
        if (now - start >= TRYTR_PDF_TIMEOUT_MS) { timed_out = true; break; }
        const int events = poll(&input, 1u, 20);
        if (events < 0 && errno == EINTR) continue;
        if (events < 0) break;
        if (events > 0 && (input.revents & (POLLIN | POLLHUP)) != 0) {
            /* Bound each drain so a continuously writing child cannot hide the deadline. */
            for (size_t drain = 0u; drain < 16u; ++drain) {
                char chunk[1024];
                const ssize_t count = read(descriptor, chunk, sizeof(chunk));
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) { if (count == 0) input.fd = -1; break; }
                size_t copied = (size_t)count;
                if (copied > capacity - *used) copied = capacity - *used;
                memcpy(diagnostic + *used, chunk, copied);
                *used += copied;
            }
        }
        siginfo_t info = {0};
        if (waitid(P_PID, (id_t)child, &info, WEXITED | WNOHANG | WNOWAIT) != 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (info.si_pid == child) { exited = true; break; }
    }
    /* The child has not been reaped, so its process-group ID cannot be reused. */
    (void)kill(-child, SIGKILL);
    (void)kill(child, SIGKILL);
    (void)close(descriptor);
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return false;
    }
    if (timed_out) fputs("trytr: Chromium PDF export timed out\n", stderr);
    return exited && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

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
        if (setpgid(0, 0) != 0) _exit(126);
        (void)close(diagnostic_pipe[0]);
        (void)dup2(diagnostic_pipe[1], STDERR_FILENO);
        if (diagnostic_pipe[1] != STDERR_FILENO) (void)close(diagnostic_pipe[1]);
        int null_descriptor = open("/dev/null", O_WRONLY);
        if (null_descriptor >= 0) {
            (void)dup2(null_descriptor, STDOUT_FILENO);
            if (null_descriptor != STDOUT_FILENO) (void)close(null_descriptor);
        }
        execv(TRYTR_CHROMIUM_EXECUTABLE, arguments);
        _exit(127);
    }
    (void)close(diagnostic_pipe[1]);
    char diagnostic[4096];
    size_t used = 0u;
    const bool ok = wait_browser(child, diagnostic_pipe[0], diagnostic, sizeof(diagnostic), &used);
    if (!ok) {
        fputs("trytr: Chromium PDF export failed:\n", stderr);
        if (used != 0u) (void)fwrite(diagnostic, 1u, used, stderr);
    }
    return ok;
}

#endif
