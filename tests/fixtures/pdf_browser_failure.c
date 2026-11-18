#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *url = argc == 0 ? "" : argv[argc - 1];
    if (strstr(url, "/hang") != NULL) { sleep(3u); return 42; }
    if (strstr(url, "/descendant") != NULL) {
        const pid_t child = fork();
        if (child == 0) { sleep(3u); _exit(0); }
        return child < 0 ? 1 : 42;
    }
    if (strstr(url, "/flood") != NULL) {
        for (;;) fputs("unbounded diagnostic output\n", stderr);
    }
    fputs("browser setup failed: test diagnostic\n", stderr);
    for (size_t i = 0u; i < 16384u; ++i) fputc('x', stderr);
    fputs("browser stdout must stay private", stdout);
    return 42;
}
