/*
 * Xournal++ Calc
 *
 * Tests for the Mathcad-style calculation engine
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>
#include <numbers>
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

TEST(CalcEngine, unitPowersWithoutCaret) {
    Environment env;
    auto lines = evaluateText("V := 2 m3\nV = ? L\nA := 5 cm2\nA = ? mm2\nx2 := 3\ny := x2 + 1 =", env);
    EXPECT_EQ(lines[1].display, "V = 2000 L");
    EXPECT_EQ(lines[3].display, "A = 500 mm2");  // shown as mm² by the markup
    EXPECT_EQ(lines[5].display, "y := x2 + 1 = 4");
    // A variable of the user named like the unit takes priority
    Environment env2;
    auto l2 = evaluateText("m := 2\nz := m3 =", env2);
    EXPECT_EQ(l2[1].kind, LineResult::Kind::Error);
}

TEST(CalcEngine, unitsIgnoreCase) {
    EXPECT_EQ(canonicalUnitName("kpa"), "kPa");
    EXPECT_EQ(canonicalUnitName("kn"), "kN");
    EXPECT_EQ(canonicalUnitName("MPA"), "MPa");
    EXPECT_EQ(canonicalUnitName("mm"), "mm");         // exact names are never changed
    EXPECT_EQ(canonicalUnitName("n"), std::nullopt);  // single letters stay case sensitive
    EXPECT_EQ(canonicalUnitName("a"), std::nullopt);
    Environment env;
    auto lines = evaluateText("P := 3 kn\nq := 1.5 kpa\nP = ? N\nA := 2 cm2\nA = ? MM2", env);
    EXPECT_EQ(lines[2].display, "P = 3000 N");
    EXPECT_EQ(lines[4].display, "A = 200 MM2");
    EXPECT_NEAR(env.vars.at("q").value, 1500, 1e-9);
}

TEST(CalcEngine, subscriptWithoutUnderscore) {
    Environment env;
    auto lines = evaluateText("A_s := 5 cm2\nAs = ? mm2", env);
    EXPECT_EQ(lines[1].display, "As = 500 mm2");
}

TEST(CalcEngine, comparisons) {
    Environment env;
    auto lines =
            evaluateText("s := 180 MPa\nfy := 420 MPa\ns ≤ fy =\ns > fy =\n0 < s ≤ fy =\ns <= 0.4 fy =\ns ≠ fy =", env);
    EXPECT_EQ(lines[2].result, "✓ cumple");
    EXPECT_EQ(lines[3].result, "✗ no cumple");
    EXPECT_EQ(lines[4].result, "✓ cumple");
    EXPECT_EQ(lines[5].result, "✗ no cumple");
    EXPECT_EQ(lines[6].result, "✓ cumple");
    EXPECT_EQ(evaluateLine("s ≤ 2 m =", env).kind, Kind::Error);  // different units
}

TEST(CalcEngine, plusMinus) {
    Environment env;
    auto lines = evaluateText(
            "a := 1\nb := -1\nc := -6\nx := (-b ± sqrt(b^2 - 4*a*c)) / (2*a) =\nx_2 =\nL := 2 m ± 3 cm = ? cm", env);
    EXPECT_EQ(lines[3].result, "3 ; -2");
    EXPECT_EQ(lines[3].display, "x := (-b ± sqrt(b^2 - 4*a*c)) / (2*a) = 3 ; -2");
    EXPECT_EQ(lines[4].result, "-2");
    EXPECT_EQ(lines[5].result, "203 cm ; 197 cm");
}

TEST(CalcEngine, deepNestingIsAnErrorNotACrash) {
    Environment env;
    // Reasonable nesting still works
    auto ok = evaluateLine("x := " + std::string(50, '(') + "2" + std::string(50, ')') + " =", env);
    EXPECT_EQ(ok.kind, Kind::Evaluation) << ok.error;
    EXPECT_EQ(ok.result, "2");
    // Thousands of levels would overflow the stack: they are refused
    for (const std::string& line: {"x := " + std::string(20000, '(') + "1" + std::string(20000, ')') + " =",
                                   "y := " + std::string(20000, '-') + "1 =", [] {
                                       std::string p = "z := 2";
                                       for (int i = 0; i < 20000; i++) {
                                           p += "^-1";
                                       }
                                       return p + " =";
                                   }()}) {
        auto r = evaluateLine(line, env);
        EXPECT_EQ(r.kind, Kind::Error);
        EXPECT_NE(r.error.find("anidada"), std::string::npos);
    }
}

TEST(CalcEngine, primeAfterGreekLetters) {
    Environment env;
    // φ′ (prime, typed as a tilde after the letter), φ´ and φ' are the same variable
    auto lines = evaluateText("φ′ := 30 deg\nσ′ := 120 kPa\nφ' =\nσ´ = ? kPa\nτ := σ′·tan(φ′) =", env);
    EXPECT_EQ(lines[2].kind, Kind::Evaluation) << lines[2].error;
    EXPECT_EQ(lines[3].result, "120 kPa");
    EXPECT_EQ(lines[4].kind, Kind::Evaluation) << lines[4].error;
    EXPECT_NE(lines[4].result.find("69.28"), std::string::npos) << lines[4].result;
}

TEST(CalcEngine, spanishFunctions) {
    Environment env;
    EXPECT_DOUBLE_EQ(evaluateExpression("RAIZ(16)", env).value, 4);
    EXPECT_DOUBLE_EQ(evaluateExpression("raiz(16)", env).value, 4);
    EXPECT_DOUBLE_EQ(evaluateExpression("Raíz(16)", env).value, 4);
    EXPECT_NEAR(evaluateExpression("RAIZ3(27)", env).value, 3, 1e-12);
    EXPECT_NEAR(evaluateExpression("RAIZ3(-8)", env).value, -2, 1e-12);
    EXPECT_NEAR(evaluateExpression("SENO(30 deg)", env).value, 0.5, 1e-12);
    EXPECT_NEAR(evaluateExpression("seno(30 deg)", env).value, 0.5, 1e-12);
    EXPECT_NEAR(evaluateExpression("SEN(30 deg)", env).value, 0.5, 1e-12);
    EXPECT_NEAR(evaluateExpression("COS(60 deg)", env).value, 0.5, 1e-12);
    EXPECT_NEAR(evaluateExpression("TAN(45 deg)", env).value, 1, 1e-12);
    EXPECT_NEAR(evaluateExpression("ASENO(0.5)", env).value, std::numbers::pi / 6, 1e-12);
    EXPECT_NEAR(evaluateExpression("ATAN2(1; 0)", env).value, 0, 1e-12);  // Excel: ATAN2(x; y)
    EXPECT_NEAR(evaluateExpression("ATAN2(0; 1)", env).value, std::numbers::pi / 2, 1e-12);
    EXPECT_NEAR(evaluateExpression("atan2(1, 0)", env).value, std::numbers::pi / 2, 1e-12);  // old: atan2(y, x)
    EXPECT_NEAR(evaluateExpression("LN(EXP(2))", env).value, 2, 1e-12);
    EXPECT_NEAR(evaluateExpression("LOG10(1000)", env).value, 3, 1e-12);
    EXPECT_NEAR(evaluateExpression("LOG(1000)", env).value, 3, 1e-12);
    EXPECT_NEAR(evaluateExpression("LOG(8; 2)", env).value, 3, 1e-12);
    EXPECT_EQ(formatQuantity(evaluateExpression("MAX(2 m; 30 cm; 1 m)", env)), "2 m");
    EXPECT_EQ(formatQuantity(evaluateExpression("MIN(2 m; 30 cm)", env)), "0.3 m");
    EXPECT_EQ(formatQuantity(evaluateExpression("ABS(-3 kN)", env)), "3 kN");
    EXPECT_DOUBLE_EQ(evaluateExpression("REDONDEAR(2.675; 2)", env).value, 2.68);
    EXPECT_DOUBLE_EQ(evaluateExpression("REDONDEAR(2.5)", env).value, 3);
    EXPECT_DOUBLE_EQ(evaluateExpression("REDONDEAR(1234; -2)", env).value, 1200);
    EXPECT_DOUBLE_EQ(evaluateExpression("REDONDEAR.MENOS(3.789; 1)", env).value, 3.7);
    EXPECT_DOUBLE_EQ(evaluateExpression("REDONDEAR.MENOS(-3.789; 1)", env).value, -3.7);
    EXPECT_DOUBLE_EQ(evaluateExpression("REDONDEAR.MAS(3.21; 1)", env).value, 3.3);
    EXPECT_DOUBLE_EQ(evaluateExpression("redondear.más(-3.21)", env).value, -4);
    EXPECT_DOUBLE_EQ(evaluateExpression("floor(-2.5)", env).value, -3);
    EXPECT_DOUBLE_EQ(evaluateExpression("ceil(2.1)", env).value, 3);
    EXPECT_THROW(evaluateExpression("REDONDEAR(2 m; 1)", env), CalcError);
    EXPECT_THROW(evaluateExpression("LOG(-1)", env), CalcError);
    EXPECT_THROW(evaluateExpression("REDONDEAR(2; 0.5)", env), CalcError);

    // A variable written in other letters than the function is still a variable
    evaluateLine("Max := 3", env);
    EXPECT_DOUBLE_EQ(evaluateExpression("Max (1 + 1)", env).value, 6);
    EXPECT_DOUBLE_EQ(evaluateExpression("MAX(1; 2)", env).value, 2);
    // min alone is still the minute
    EXPECT_DOUBLE_EQ(evaluateExpression("2 min", env).value, 120);
}

TEST(CalcEngine, functionCatalog) {
    auto names = [](std::string_view p) {
        std::string out;
        for (const auto* f: functionsStartingWith(p)) {
            out += std::string(out.empty() ? "" : " ") + f->name;
        }
        return out;
    };
    EXPECT_EQ(names("se"), "SENO");
    EXPECT_EQ(names("RA"), "RAIZ RAIZ3");
    EXPECT_EQ(names("raí"), "RAIZ RAIZ3");
    EXPECT_EQ(names("redondear."), "REDONDEAR.MENOS REDONDEAR.MAS");
    EXPECT_EQ(names("lo"), "LOG10 LOG");
    EXPECT_EQ(names(""), "");
    EXPECT_EQ(names("xyz"), "");
    for (const auto& f: functionCatalog()) {
        EXPECT_TRUE(functionId(f.name).has_value()) << f.name;
    }
}
