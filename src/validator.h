#ifndef TRYTR_SRC_VALIDATOR_H
#define TRYTR_SRC_VALIDATOR_H

#include "ast.h"
#include "diagnostic.h"

void trytr_validator_validate(
    const TrytrNode *root,
    TrytrDiagnosticList *diagnostics
);

#endif
