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

/// Spelling of a unit as it is defined: the name itself, or the only unit that matches it ignoring case for names of
/// two or more letters ("kpa" → "kPa"). Nullopt if it is not a unit
std::optional<std::string> canonicalUnitName(std::string_view name);

/// Look up a unit or physical constant by name (e.g. "kN", "MPa", "deg", "g"; "kpa" and "kn" also work)
std::optional<Quantity> findUnit(std::string_view name);

/// A unit followed by a power written without "^", e.g. "m3" → ("m", 3), "cm2" → ("cm", 2). Nullopt if the name
/// is not of that form
std::optional<std::pair<std::string, int>> splitUnitPower(std::string_view name);

/// Format a number the way it is shown in results (e.g. "750", "0.75", "1.234·10⁻⁵")
std::string formatNumber(double v);

/// Format a dimension with SI units, e.g. "kg·m²/s²"; empty for dimensionless
std::string formatDim(const Dim& d);

/// Format a quantity in automatically chosen SI units, e.g. "750 N·m", or in a preferred unit of its dimension
std::string formatQuantity(const Quantity& q);

/**
 * Units in which results are shown when no unit is requested with "?" (set in the preferences), e.g. "kN; kN·m":
 * a force is then shown in kN (1128.9 kN, not 1.129 MN). Separated by ";" or ",". Units that are not understood are
 * ignored. Returns the units that were understood
 */
std::vector<std::string> setPreferredUnits(std::string_view spec);

/**
 * Link with cells of a spreadsheet (Excel): implemented outside of the engine (see ExcelLink.h), so that the engine
 * does not depend on Windows or on the file format.
 *
 *     excel("cargas.xlsx")                  workbook used by the following references
 *     P := xl("Cargas!C5")·kN               value of a cell (Excel has no units: they are written after it)
 *     enviar(M, "Resumen!D4", kN·m)         write M, converted to kN·m, to a cell
 *
 * A reference is "C5" (first sheet), "Hoja1!C5", "'Mi hoja'!C5" or "[otro.xlsx]Hoja1!C5". Errors are thrown as
 * CalcError.
 */
class CellLink {
public:
    virtual ~CellLink() = default;
    /// Use a workbook for the following references. Returns how it is reached, e.g. "abierto en Excel"
    virtual std::string useWorkbook(const std::string& path) = 0;
    /// Value of a cell
    virtual double read(const std::string& ref) = 0;
    /// Write a number to a cell. Returns where it went, e.g. "Resumen!D4 en Excel"
    virtual std::string write(const std::string& ref, double value) = 0;
    /// Remarks about the last operations (e.g. "valor guardado: el libro no está disponible"), cleared when read
    virtual std::vector<std::string> takeNotes() { return {}; }
};

struct Environment {
    std::map<std::string, Quantity> vars;
    CellLink* cells = nullptr;  ///< link with a spreadsheet (nullptr: xl(), excel() and enviar() give an error)
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
    std::string note;     ///< remark shown after the line in small grey letters (e.g. a cell read from the cache)
};

/// Evaluate a single statement, updating env if it is a definition
LineResult evaluateLine(std::string_view line, Environment& env);

/// Evaluate every line of a text block, in order
std::vector<LineResult> evaluateText(std::string_view text, Environment& env);

/// Evaluate an expression and return its value (used by tests)
Quantity evaluateExpression(std::string_view expr, const Environment& env);

/**
 * Functions of the calculation regions. They are named as in Excel in Spanish (RAIZ, SENO, REDONDEAR...), with ";"
 * between the arguments, and are found without distinguishing upper and lower case (raiz = RAIZ). The English names
 * of the first versions (sqrt, sin, round...) still work.
 */
struct FunctionInfo {
    const char* name;         ///< as it is offered while typing, e.g. "SENO"
    const char* syntax;       ///< e.g. "SENO(ángulo)"
    const char* description;  ///< e.g. "Seno de un ángulo"
};

/// The functions offered while typing, in the order they are shown
const std::vector<FunctionInfo>& functionCatalog();

/// The functions of the catalog whose name starts with prefix (ignoring case and accents), e.g. "se" → SENO
std::vector<const FunctionInfo*> functionsStartingWith(std::string_view prefix);

/// Internal identifier of the function a name refers to ("SENO", "seno", "sin" → "sin"), nullopt if it is none
std::optional<std::string> functionId(std::string_view name);

/// Whether name followed by "(" calls a function: a name of a function that is not, written in other letters than a
/// function, a variable of env ("Max (a + b)" multiplies a variable Max)
bool callsFunction(const std::string& name, const Environment* env);

/// Greek letter for a latin letter, using the Mathcad/Symbol font mapping (a→α, b→β, g→γ, q→θ, ...).
/// Returns an empty string if there is no mapping.
std::string greekForLatin(char c);

}  // namespace xoj::calc
