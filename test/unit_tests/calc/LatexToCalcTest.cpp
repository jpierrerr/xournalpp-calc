/*
 * Xournal++ Calc
 *
 * Tests of the conversion of recognized LaTeX into calculation regions
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "calc/CalcEngine.h"
#include "calc/LatexToCalc.h"

using namespace xoj::calc;

TEST(LatexToCalc, definitionsAndUnits) {
    EXPECT_EQ(latexToCalc("F = 3 0 0 \\mathrm { k N }"), "F := 300 kN");
    EXPECT_EQ(latexToCalc("\\sigma _ { c } = 2 1 \\, M P a"), "σ_c := 21 MPa");
    EXPECT_EQ(latexToCalc("A _ { s } = 3 , 5 c m ^ { 2 }"), "A_s := 3.5 cm^2");
    EXPECT_EQ(latexToCalc("\\gamma = 2 4 \\frac { k N } { m ^ { 3 } }"), "γ := 24 kN/m^3");
    EXPECT_EQ(latexToCalc("a : = 5 m"), "a := 5 m");
    EXPECT_EQ(latexToCalc("f _ { c } ^ { \\prime } = 2 8 M P a"), "f_c' := 28 MPa");
}

TEST(LatexToCalc, structures) {
    EXPECT_EQ(latexToCalc("M = \\frac { F \\cdot L } { 4 } ="), "M := F·L/4 =");
    EXPECT_EQ(latexToCalc("x ^ { 2 } + \\sqrt { a + b }"), "x^2 + √(a + b)");
    EXPECT_EQ(latexToCalc("x = \\frac { - b \\pm \\sqrt { b ^ { 2 } - 4 a c } } { 2 a }"),
              "x := (-b ± √(b^2 - 4 a c))/(2 a)");
    EXPECT_EQ(latexToCalc("\\frac { a } { b } c"), "(a/b) c");
    EXPECT_EQ(latexToCalc("y = \\sin ( 3 0 ^ { \\circ } )"), "y := sin(30°)");
    EXPECT_EQ(latexToCalc("\\sin x + \\cos \\theta"), "sin(x) + cos(θ)");
    EXPECT_EQ(latexToCalc("\\left( a + b \\right) ^ { 2 }"), "(a + b)^2");
    EXPECT_EQ(latexToCalc("\\max ( a , b )"), "max(a, b)");
    EXPECT_EQ(latexToCalc("2 . 5 \\times 1 0 ^ { 3 }"), "2.5·10^3");
    EXPECT_EQ(latexToCalc("\\frac { a } { \\varGamma }"), "a/Γ");
}

TEST(LatexToCalc, equalsAndLines) {
    EXPECT_EQ(latexToCalc("a + b = c"), "a + b = c");
    EXPECT_EQ(latexToCalc("x = ?"), "x = ?");
    EXPECT_EQ(latexToCalc("E = m c ^ { 2 }"), "E := m c^2");
    EXPECT_EQ(latexToCalc("\\phi M _ { n } \\geq M _ { u }"), "φ M_n ≥ M_u");
    EXPECT_EQ(latexToCalc("x = 2 \\\\ y = x ^ { 3 } ="), "x := 2\ny := x^3 =");
    EXPECT_EQ(latexToCalcLiteral("x = 2"), "x = 2");
}

TEST(LatexToCalc, resultsEvaluate) {
    Environment env;
    evaluateLine(latexToCalc("F = 3 0 0 \\mathrm { k N }"), env);
    evaluateLine(latexToCalc("L = 2 . 5 m"), env);
    EXPECT_EQ(evaluateLine(latexToCalc("M = \\frac { F \\cdot L } { 4 } ="), env).result, "187.5 kN·m");
    EXPECT_EQ(evaluateLine(latexToCalc("x = \\frac { - 2 \\pm \\sqrt { 2 ^ { 2 } + 4 \\cdot 3 } } { 2 } ="), env).result,
              "1 ; -3");
}
