/*
 * Xournal++ Calc
 *
 * Tests for the Mathcad-style calculation engine
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>
#include <string>

#include <gtest/gtest.h>

#include "calc/CalcEngine.h"

using namespace xoj::calc;
using Kind = LineResult::Kind;

static std::string display(const std::string& line, Environment& env) { return evaluateLine(line, env).display; }

TEST(CalcEngine, numbersAndPrecedence) {
    Environment env;
    EXPECT_DOUBLE_EQ(evaluateExpression("1 + 2*3", env).value, 7);
    EXPECT_DOUBLE_EQ(evaluateExpression("(1 + 2)*3", env).value, 9);
    EXPECT_DOUBLE_EQ(evaluateExpression("2^3^2", env).value, 512);
    EXPECT_DOUBLE_EQ(evaluateExpression("-2^2", env).value, -4);
    EXPECT_DOUBLE_EQ(evaluateExpression("1.5e3", env).value, 1500);
    EXPECT_DOUBLE_EQ(evaluateExpression("sqrt(16)", env).value, 4);
    EXPECT_DOUBLE_EQ(evaluateExpression("√16", env).value, 4);
    EXPECT_DOUBLE_EQ(evaluateExpression("3²", env).value, 9);
    EXPECT_DOUBLE_EQ(evaluateExpression("2·3×4", env).value, 24);
    EXPECT_NEAR(evaluateExpression("sin(30 deg)", env).value, 0.5, 1e-12);
    EXPECT_NEAR(evaluateExpression("cos(π)", env).value, -1, 1e-12);
}

TEST(CalcEngine, unitsAndDimensions) {
    Environment env;
    auto q = evaluateExpression("2.5 m * 300 N", env);
    EXPECT_DOUBLE_EQ(q.value, 750);
    EXPECT_EQ(formatQuantity(q), "750 N·m");

    EXPECT_DOUBLE_EQ(evaluateExpression("1 kN / 1 mm^2", env).value, 1e9);
    EXPECT_EQ(formatQuantity(evaluateExpression("10 kN/(2 m^2)", env)), "5 kPa");
    EXPECT_EQ(formatQuantity(evaluateExpression("9.81 m/s^2", env)), "9.81 m/s²");
    EXPECT_EQ(formatQuantity(evaluateExpression("1 m + 20 cm", env)), "1.2 m");
    EXPECT_EQ(formatQuantity(evaluateExpression("sqrt(4 m²)", env)), "2 m");
    EXPECT_NEAR(evaluateExpression("1 kgf", env).value, 9.80665, 1e-12);
    EXPECT_EQ(formatQuantity(evaluateExpression("2 m^2 kN", env)), "2000 m³·kg/s²");
    EXPECT_DOUBLE_EQ(evaluateExpression("2^-1", env).value, 0.5);
    EXPECT_EQ(formatQuantity(evaluateExpression("3 m / 2 s", env)), "1.5 m/s");

    EXPECT_EQ(formatQuantity(evaluateExpression("25 kN/m * (6 m)^2 / 8", env)), "112.5 kN·m");
    EXPECT_EQ(formatQuantity(evaluateExpression("200000 MPa", env)), "200 GPa");
    EXPECT_EQ(formatQuantity(evaluateExpression("0.5 N", env)), "0.5 N");
    EXPECT_THROW(evaluateExpression("1 m + 1 s", env), CalcError);
    EXPECT_THROW(evaluateExpression("sqrt(2 m)", env), CalcError);
    EXPECT_THROW(evaluateExpression("sin(2 m)", env), CalcError);
    EXPECT_THROW(evaluateExpression("2^(1 m)", env), CalcError);
}

TEST(CalcEngine, definitionsFlowDown) {
    Environment env;
    EXPECT_EQ(evaluateLine("L := 2.5 m", env).kind, Kind::Definition);
    EXPECT_EQ(evaluateLine("F := 300 kN", env).kind, Kind::Definition);
    EXPECT_EQ(display("M := F·L =", env), "M := F·L = 750 kN·m");
    EXPECT_EQ(display("M = ? kN·m", env), "M = 750 kN·m");
    EXPECT_EQ(display("M=?kN*m", env), "M= 750 kN*m");

    // redefinition takes effect for later lines only
    evaluateLine("L := 3 m", env);
    EXPECT_EQ(display("F·L = ? kN·m", env), "F·L = 900 kN·m");
    EXPECT_EQ(display("M = ? kN·m", env), "M = 750 kN·m");
}

TEST(CalcEngine, greekAndSubscriptNames) {
    Environment env;
    evaluateLine("σ_adm := 25 MPa", env);
    evaluateLine("A_s := 5 cm^2", env);
    evaluateLine("f'c := 21 MPa", env);
    EXPECT_EQ(display("A_s·σ_adm = ? kN", env), "A_s·σ_adm = 12.5 kN");
    EXPECT_EQ(display("f'c = ? MPa", env), "f'c = 21 MPa");
    EXPECT_EQ(display("φ := 0.9 =", env), "φ := 0.9 = 0.9");
}

TEST(CalcEngine, userVariablesShadowUnits) {
    Environment env;
    evaluateLine("h := 50 cm", env);  // "h" is also the hour unit
    EXPECT_EQ(display("2·h =", env), "2·h = 1 m");
}

TEST(CalcEngine, errors) {
    Environment env;
    auto r = evaluateLine("x + 1 =", env);
    EXPECT_EQ(r.kind, Kind::Error);
    EXPECT_EQ(r.error, "variable no definida: x");

    r = evaluateLine("a := 2 m", env);
    r = evaluateLine("a = ? kN", env);
    EXPECT_EQ(r.kind, Kind::Error);

    EXPECT_EQ(r.error, "las unidades kN no corresponden a m");

    r = evaluateLine("1 MPa + 1 m =", env);
    EXPECT_EQ(r.error, "unidades incompatibles: Pa + m");

    r = evaluateLine("a = ? furlong", env);
    EXPECT_EQ(r.kind, Kind::Error);
    EXPECT_EQ(r.error, "unidad desconocida: furlong");

    EXPECT_EQ(evaluateLine("1/0 =", env).kind, Kind::Error);
}

TEST(CalcEngine, plainTextLines) {
    Environment env;
    auto lines = evaluateText("Cálculo de la viga\nb := 30 cm\n\n# comentario = no\nb =", env);
    ASSERT_EQ(lines.size(), 5U);
    EXPECT_EQ(lines[0].kind, Kind::Text);
    EXPECT_EQ(lines[1].kind, Kind::Definition);
    EXPECT_EQ(lines[2].kind, Kind::Text);
    EXPECT_EQ(lines[3].kind, Kind::Text);
    EXPECT_EQ(lines[4].display, "b = 0.3 m");
}

TEST(CalcEngine, numberFormatting) {
    EXPECT_EQ(formatNumber(0), "0");
    EXPECT_EQ(formatNumber(750), "750");
    EXPECT_EQ(formatNumber(2.34567), "2.346");
    EXPECT_EQ(formatNumber(0.012345), "0.01235");
    EXPECT_EQ(formatNumber(-0.5), "-0.5");
    EXPECT_EQ(formatNumber(1.5e-5), "1.5·10⁻⁵");
    EXPECT_EQ(formatNumber(2.1e9), "2.1·10⁹");
}

TEST(CalcEngine, greekMapping) {
    EXPECT_EQ(greekForLatin('a'), "α");
    EXPECT_EQ(greekForLatin('q'), "θ");
    EXPECT_EQ(greekForLatin('W'), "Ω");
    EXPECT_EQ(greekForLatin('1'), "");
}
