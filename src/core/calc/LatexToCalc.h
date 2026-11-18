/*
 * Xournal++ Calc
 *
 * Conversion of the LaTeX produced by the handwriting recognizer into the plain syntax of the calculation regions
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <string_view>

namespace xoj::calc {

/**
 * Convert a LaTeX formula (as produced by a formula recognizer, with tokens separated by spaces) into the syntax of a
 * calculation region:
 *     "F = 3 0 0 \mathrm { k N }"        → "F := 300 kN"
 *     "M = \frac { F \cdot L } { 4 } ="  → "M := F·L/4 ="
 *     "\sigma _ { c } = 2 1 \, M P a"    → "σ_c := 21 MPa"
 *     "x ^ { 2 } + \sqrt { a + b }"      → "x^2 + √(a + b)"
 *
 * A lone "=" after a plain name with something on its right becomes a definition ":=", as in Mathcad.
 * Several lines (separated by "\\") give several lines separated by '\n'.
 */
std::string latexToCalc(std::string_view latex);

/// Same, but keeping "=" as written (used for plain text conversions and tests)
std::string latexToCalcLiteral(std::string_view latex);

}  // namespace xoj::calc
