#ifndef TRYTR_PROOF_H
#define TRYTR_PROOF_H

#include <stdbool.h>
#include <stddef.h>

typedef struct TrytrProofResult {
    bool valid;
    size_t offset;
    char message[160];
} TrytrProofResult;

/* Check one equality proof in the experimental natural-number rewrite kernel. */
TrytrProofResult trytr_proof_check(const char *source);

#endif
