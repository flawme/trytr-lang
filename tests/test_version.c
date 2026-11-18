#include "trytr/trytr.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *version = trytr_version();

    if (version == NULL) {
        fputs("trytr_version returned NULL\n", stderr);
        return 1;
    }

    if (strcmp(version, "0.1.0-alpha.1") != 0) {
        fprintf(stderr, "expected version 0.1.0-alpha.1, got %s\n", version);
        return 1;
    }

    return 0;
}
