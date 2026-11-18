/*
 * Xournal++ Calc
 *
 * Mathcad-style calculation engine with physical units.
 *
 * The engine is independent of GTK and of the document model: it evaluates
 * plain text statements such as
 *
 *     L := 2.5 m
 *     F := 300 kN
 *     M := F·L =
 *     M = ? kN·m
 *
 * Variables defined with ":=" are stored in an Environment, so that later
 * statements (further down the page) can use them.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace xoj::calc {

/// Exponents of the 7 SI base dimensions: m, kg, s, A, K, mol, cd
struct Dim {
    std::array<int, 7> e{};

    bool operator==(const Dim& o) const = default;
    bool isDimensionless() const;
    Dim operator*(const Dim& o) const;
    Dim operator/(const Dim& o) const;
    Dim pow(int n) const;
};

/// A value expressed in SI base units, together with its dimension
struct Quantity {
    double value = 0;
    Dim dim{};
};

class CalcError: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Look up a unit or physical constant by name (e.g. "kN", "MPa", "deg", "g")
std::optional<Quantity> findUnit(std::string_view name);

/// A unit followed by a power written without "^", e.g. "m3" → ("m", 3), "cm2" → ("cm", 2). Nullopt if the name
/// is not of that form
std::optional<std::pair<std::string, int>> splitUnitPower(std::string_view name);

/// Format a number the way it is shown in results (e.g. "750", "0.75", "1.234·10⁻⁵")
std::string formatNumber(double v);

/// Format a dimension with SI units, e.g. "kg·m²/s²"; empty for dimensionless
std::string formatDim(const Dim& d);

/// Format a quantity in automatically chosen SI units, e.g. "750 N·m"
std::string formatQuantity(const Quantity& q);

struct Environment {
    std::map<std::string, Quantity> vars;
};

struct LineResult {
    enum class Kind {
        Text,        ///< not a calculation (a comment or plain text line)
        Definition,  ///< "x := expr"; nothing is shown after it
        Evaluation,  ///< "expr =" or "x := expr ="; a result is shown
        Error        ///< the line could not be evaluated
    };
    Kind kind = Kind::Text;
    std::string source;   ///< the original line
    std::string display;  ///< the line as it is shown (source with the result filled in)
    std::string result;   ///< the result part only, e.g. "750 N·m"
    std::string error;    ///< error message (Kind::Error)
};

/// Evaluate a single statement, updating env if it is a definition
LineResult evaluateLine(std::string_view line, Environment& env);

/// Evaluate every line of a text block, in order
std::vector<LineResult> evaluateText(std::string_view text, Environment& env);

/// Evaluate an expression and return its value (used by tests)
Quantity evaluateExpression(std::string_view expr, const Environment& env);

/// Greek letter for a latin letter, using the Mathcad/Symbol font mapping (a→α, b→β, g→γ, q→θ, ...).
/// Returns an empty string if there is no mapping.
std::string greekForLatin(char c);

}  // namespace xoj::calc
