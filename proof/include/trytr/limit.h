#ifndef TRYTR_LIMIT_H
#define TRYTR_LIMIT_H

#include "trytr/proof.h"

/*
 * Check a polynomial epsilon-delta certificate in the documented LaTeX-like
 * subset. The checked theorem is over the real numbers, using the standard
 * absolute-value laws for polynomials.
 */
TrytrProofResult trytr_limit_check(const char *source);

#endif
