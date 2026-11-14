# Curve and line rendering

`src/math/curve_interpolate.c` owns curve construction and evaluation, while
`src/math/curve_raster.c` owns line rasterization, subdivision, and coverage.
Plot data stays in double precision through coordinate mapping and spline construction.
The text renderer maps each curve into character-grid space before subdivision
so its flatness tolerance reflects the actual output resolution.

For strictly increasing or decreasing x values, the curve uses local,
shape-preserving cubic interpolation. Interior tangents use a spacing-weighted
harmonic mean of neighboring slopes; a zero or sign change makes the tangent
zero, which keeps each monotone interval between its input values. The tangent
values become cubic Bézier control points. This follows the monotone cubic
interpolation approach described by [Fritsch and Butland](https://doi.org/10.1137/0905021).

If x doubles back or repeats, the plot uses a centripetal Catmull–Rom path
instead. This handles loops and vertical segments where function-style
monotone interpolation does not apply. The shared cubic evaluator supports
both SVG output and the text rasterizer.

Text output adaptively subdivides each cubic with De Casteljau's construction
until its control polygon is flat within 0.2 subcell units after mapping to the
2-by-3 grid. Each flattened segment is traced there; every character stores six
independent filled subcells and maps them to Unicode block sextants. This gives
the text renderer six curve positions per character instead of one. The grid
still has finite resolution, but the path no longer turns only at whole-character
boundaries. The sextant symbols are defined by the [Unicode block sextant chart](https://www.unicode.org/Public/UCD/latest/charts/nameslist/1fb00/).
The line step accumulator follows the integer-grid approach of [Bresenham's digital
plotter algorithm](https://ibm-1401.info/Pics3/bresenham1965.pdf), with an
extra orthogonal step when a move would otherwise cross cells only at a corner.
The math module also exposes fractional line coverage using [Xiaolin Wu's
antialiasing method](https://cgg.mff.cuni.cz/~pepca/ref/WU.pdf) for raster
backends that support it. SVG keeps decimal Bézier controls as a vector path; the
[SVG path specification](https://www.w3.org/TR/SVG/paths.html#PathDataCubicBezierCommands)
defines the cubic `C` command used for this output.

Dedicated interpolation and rasterization checks live in
`tests/test_math_curve_interpolate.c` and `tests/test_math_curve_raster.c`, with
end-to-end chart checks in `tests/test_plot.c`.
