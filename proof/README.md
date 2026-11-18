# Trytr proof side module

This directory contains an isolated math checker without adding proof
machinery to Trytr's document parser or runtime. It has reusable algebra review
plus two narrower proof checkers. It is meant to independently check supported
claims in AI-written work and report unsupported claims clearly; it is not a
general proof assistant.

## General algebra review

Use a `.trmath` file with one complete equality per row. The checker expands and
normalizes both sides exactly over rational coefficients, so it can verify
multivariable polynomial identities without knowing which exercise they came
from:

```latex
\begin{align*}
(x+y)^2 &= x^2+2xy+y^2 \\
(a-b)(a+b) &= a^2-b^2
\end{align*}
```

It supports up to eight variables, degree 12, exact integer or rational
coefficients, decimals up to nine places, and common LaTeX forms such as
`\frac`, `\alpha`, and subscripts. Each row is reported as verified, conditional,
not an algebraic identity, or unable to check. A failed identity means the equality
does not hold for all values of its variables; it does not rule out a claim
that follows from additional assumptions. Functions and unsupported
expressions are explicitly left unchecked.

### Rational expressions and domain conditions

The general CLI/MCP checker also accepts variable denominators, nested
fractions, and bounded negative integer powers. It checks identities exactly
by representing each expression as a polynomial numerator/denominator pair
and comparing cross products. This follows the rational-function model
described in [SymPy's domain documentation](https://docs.sympy.org/latest/modules/polys/domainsintro.html#rational-function-fields),
implemented here in bounded C without a SymPy runtime dependency.

For example, `(x^2-1)/(x-1)=x+1` is **conditional** on `(x-1)` being defined and
nonzero. `x/x=1`, `0/x=0`, and `(x/x)^0=1` retain the `x != 0` requirement.
Cancellation, multiplication by zero, or a zero power never erase an original
division's domain restriction. An identically zero divisor is invalid, even
in a term multiplied by zero. The checker does not prove these nonzero
conditions from assumptions, and it does not show that both sides have equal
domains. Conditional results never count as unconditional proofs.

`trytr_algebra_check_rational` in `<trytr/algebra.h>` exposes this review to C
hosts. `TRYTR_ALGEBRA_CONDITIONAL` includes up to 32 original-source spans for
divisors that must be defined and nonzero. The original
`trytr_algebra_check` API retains its strict polynomial-only behavior.
`.trmath` CLI exit codes are 0 for unconditional identities, 1 for nonidentities,
and 2 for conditional/invalid/unsupported rows; a mixed document uses the most
conservative code. MCP reports each conditional row explicitly with `ok:false`
for the overall review.

All intermediate polynomials remain bounded to eight variables, 128 terms,
and total degree 12. Source is at most 64 KiB, nesting at most 32, and algebra
work has a one-million-operation budget. Arithmetic is exact with checked
64-bit rational coefficients; overflow or budget exhaustion is unsupported.
There is no general polynomial GCD reduction, so intermediates can hit a bound
even when the final expression would simplify. Functions, inequalities,
assumption discharge, and arbitrary prose remain unsupported.

Run the general checker with:

```sh
./build/trytr-proof examples/algebra-steps.trmath
```

## Natural-number equality

The first format checks equality theorems over natural-number terms:

```text
Term ::= 0 | succ(Term) | add(Term, Term)
```

The checker starts with the theorem's left side and verifies each explicit
rewrite against the two built-in recursive equations for addition:

```text
add(a, 0) = a
add(a, succ(b)) = succ(add(a, b))
```

Proofs cannot declare axioms or introduce arbitrary rewrite rules. A theorem is
accepted only when the checked rewrites produce its stated right side. This is
an intentionally narrow equality checker, not yet a general logic or real
analysis prover.

## First checked theorem

```text
theorem one_plus_one: add(succ(0), succ(0)) = succ(succ(0)) {
    rewrite add_succ;
    rewrite add_zero;
}
```

[`examples/one-plus-one.trytr`](examples/one-plus-one.trytr) presents the same
derivation as a Trytr document. From the repository root, render it with
`./build/debug/trytr --render-svg proof/examples/one-plus-one.trytr`.

Build and run the example from this directory:

```sh
cmake -S . -B build
cmake --build build
./build/trytr-proof examples/one-plus-one.trproof
./build/trytr-proof examples/quadratic-limit.trlimit
./build/trytr-proof examples/algebra-steps.trmath
ctest --test-dir build --output-on-failure
```

## Polynomial epsilon-delta certificates

The second format accepts a small LaTeX-like syntax for polynomial limits at an
integer point. For example, [`examples/quadratic-limit.trlimit`](examples/quadratic-limit.trlimit)
checks

```latex
\begin{limitproof}
\lim_{x\to 2}(x^2+1)=5
\delta=\min\left(1,\frac{\varepsilon}{5}\right)
\end{limitproof}
```

The checker parses integer-coefficient polynomials (addition, subtraction,
multiplication, integer powers, parentheses, and implicit coefficient
multiplication). It evaluates the polynomial at the approach point, computes
the exact polynomial quotient
`(p(x) - p(a)) / (x - a)`, and independently derives a coefficient bound for
that quotient on `|x-a| < 1`. It accepts the certificate only if the positive
denominator in `min(1, epsilon/C)` is at least that bound. This validates the
standard epsilon-delta argument using the real absolute-value triangle and
product laws.

This remains a deliberately narrow certificate checker, not a general LaTeX
parser or proof assistant. The limit point and polynomial coefficients are
integers, degree is capped at 16, and arithmetic that exceeds the checked
integer ranges is rejected. It does not check arbitrary prose or user-defined
lemmas, and it does not yet support rational functions, trigonometric or
transcendental functions, quantifier proofs, or limits at infinity. The
checker has fixed input and nesting limits.

## Browser chat demo

After building the main Trytr CLI, run `python3 proof/web/server.py` from the
repository root and open <http://127.0.0.1:8765/>. The page streams a response
from OpenRouter, sends its final math expression through Trytr's
`--render-mathml` path, and displays the result as browser-native MathML. It does
not create an SVG. Enter an OpenRouter API key in the page to make a request;
the local demo does not save the key. The server binds to loopback and uses
only Python's standard library.

Run the web validation tests from `proof/web` with
`python3 -m unittest -v test_server.py`.
