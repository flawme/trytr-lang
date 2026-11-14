# Math library

Trytr's numerical code lives in a separate `trytr_math` C library. The document
core links to it, while each mathematical domain has its own small header and
source file:

| Domain | Public header | Implementation | Main representation |
| --- | --- | --- | --- |
| Real scalars | `include/trytr/math/real.h` | `src/math/real.c` | `double` (`TrytrReal`) |
| Complex numbers | `include/trytr/math/complex.h` | `src/math/complex.c` | `(real, imaginary)` pair |
| 2D vectors | `include/trytr/math/vector2.h` | `src/math/vector2.c` | `(x, y)` pair |
| 2D points | `include/trytr/math/point2.h` | `src/math/point2.c` | affine location `(x, y)` |
| 2 by 2 linear maps | `include/trytr/math/matrix2.h` | `src/math/matrix2.c` | row-major four scalars |
| 2D affine transforms | `include/trytr/math/affine2.h` | `src/math/affine2.c` | linear map plus translation |
| Curves | `include/trytr/math/curve.h` | `src/math/curve_interpolate.c`, `src/math/curve_raster.c` | points and cubic Bézier segments |
| Math notation | `include/trytr/math/notation.h` | `src/math/notation.c` (document core) | document-owned expression tree with source spans |

The notation tree is part of the document core rather than the standalone
`trytr_math` library because its nodes share the document's bounded arena and
are valid for exactly the lifetime of the parsed document. See the
[notation API notes](math-notation.md) for the currently recognized structure.

The modules are typed by meaning, not just storage. A 2D vector is isomorphic
to the complex value `x + yi`, but the public API keeps vector dot and cross
products as vector operations. Conversion between the two types is explicit.
Rotation uses multiplication by a unit complex number, which directly encodes
the angle and avoids duplicating a second rotation convention. The NIST
[Digital Library of Mathematical Functions](https://dlmf.nist.gov/1.9) gives
the rectangular and polar definitions used here. Netlib's
[BLAS reference](https://www.netlib.org/lapack/lug/node145.html) distinguishes
ordinary complex dot products from conjugating dot products, which is another
reason not to overload one operation based on representation.

Angles passed to the real trigonometric functions and complex polar constructor
are radians. `trytr_real_sqrt` follows the C math library's domain behavior;
for example, the square root of a negative real value is NaN. The complex
module separately provides the principal complex square root. It rescales the
input by a power of two before evaluating the real and imaginary parts, so a
finite input near the ends of the `double` range can still produce a finite
root. The sign of an input imaginary zero selects the side of the negative-real
branch cut. The principal-value convention follows the NIST
[DLMF definitions](https://dlmf.nist.gov/4.2); operand scaling for this
operation is discussed by Ercegovac and Muller in
[Complex Square Root with Operand Prescaling](https://perso.ens-lyon.fr/jean-michel.muller/ComplexSqrt-JVLSI.pdf).
The real scalar portion is exposed to `.trytr` through `@expr` and formula
plots. Complex, vector, matrix, affine, and curve operations remain available
to C callers but are not yet typed values or operators in document expressions.

Operations with output parameters initialize the output to zero before checking
their inputs. Complex division and matrix inversion return `false` for invalid
inputs, zero divisors or singular matrices, and results outside the finite
`double` range. Complex division scales both inputs by binary exponents before
forming the quotient; this avoids the unnecessary overflow from directly
squaring very large denominator components. The numerical motivation is
discussed in the research paper [A Robust Complex Division in
Scilab](https://arxiv.org/abs/1210.4539). Vector length uses `hypot`, and
normalization scales components first so the squared components stay bounded.
Two-point curves use safe convex interpolation for their control points, even
when the endpoints span nearly the full finite `double` range. For larger
curves, construction reports failure if finite input coordinates would produce
non-finite controls, so callers can avoid emitting corrupted geometry.
Vector dot and cross products use a fast fused multiply-add path when their
products are ordinary finite values. If a product would overflow or underflow,
they switch to exponent-scaled products and retain the multiplication residual
with `fma`, allowing large terms to cancel into a finite result. This follows
the error-free product and sum techniques studied by Ogita, Rump, and Oishi in
[Accurate Sum and Dot Product](https://epubs.siam.org/doi/10.1137/030601818);
the C `fma` residual pattern is also described in the [GNU C language
manual](https://www.gnu.org/software/c-intro-and-ref/manual/c-intro-and-ref.html).
The 2 by 2 determinant likewise uses compensated products near singularity and
row exponent scaling when direct products would overflow; its tests include
matrices whose determinant is the gap between adjacent floating-point values.

Curve evaluation uses de Casteljau interpolation on the unit interval instead
of directly summing Bernstein basis terms. Its repeated convex interpolations
preserve the control-point range and avoid large intermediate powers. The
method is also used for stable subdivision in Bézier geometry; see the
University of California, San Diego's [computer graphics notes](https://mathweb.ucsd.edu/~sbuss/MathCG2/SecondEdDraft.pdf).
The [University of Illinois Bézier notes](https://courses.grainger.illinois.edu/cs418/fa2025/text/bezier.html)
state the convex-hull property used by the rasterizer's flatness bound.
Centripetal interpolation computes its parameter interval from
`sqrt(hypot(dx, dy))`, avoiding a general fractional `pow` call and the
overflow-prone squared distance. Curve flattening checks the cubic control hull
against the requested screen-space tolerance and refuses requests it cannot
meet within its fixed subdivision budget. It checks that budget before calling
the consumer, so failure cannot leave half a curve in the output.

## Document expressions

The expression compiler lives separately in
`include/trytr/math/expression.h` and `src/math/expression.c`; it translates a
bounded expression to postfix instructions and evaluates it with a fixed-size
stack. It exposes real arithmetic, constants, and the real scalar functions
listed in the public header. Formula plots bind `x`, evaluate the expression
over a bounded integer interval, and keep the resulting decimal coordinates
through the SVG and text renderers.

Formula plotting uses an author-selected, uniform sample count (257 by default,
up to 1,024), then fits a cubic path through those samples. This produces a
smooth vector path while keeping work and output size predictable. Uniform
sampling can miss narrow features in a rapidly varying function; the plot
should therefore be read as an approximation at the chosen sample density.
Classical graphics work on adaptive subdivision instead sets an error tolerance
in projected screen space and adds samples where visible deviation is high;
see Forsey and Klassen, [An Adaptive Subdivision Algorithm for Crack
Prevention](https://graphicsinterface.org/wp-content/uploads/gi1990-1.pdf).
That is a useful next improvement if Trytr adds automatic sample selection.

The matrix type represents only linear maps. Translation has a separate affine
type whose composition order is explicit: `left * right` applies the right
transform first. Higher dimensional or structured matrices should get separate
types when a language feature needs them. Add a module for a concrete domain
and test it independently; do not add unrelated operators to a generic math
dispatch file.
