#include <stdio.h>

int main(void)
{
    fputs("browser setup failed: test diagnostic\n", stderr);
    for (size_t i = 0u; i < 16384u; ++i) fputc('x', stderr);
    fputs("browser stdout must stay private", stdout);
    return 42;
}
