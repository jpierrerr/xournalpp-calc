#include "CalcEngine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <numbers>
#include <unordered_map>

namespace xoj::calc {

// ---------------------------------------------------------------------------------------------------------------------
// Dimensions
// ---------------------------------------------------------------------------------------------------------------------

bool Dim::isDimensionless() const {
    return std::all_of(e.begin(), e.end(), [](int x) { return x == 0; });
}

Dim Dim::operator*(const Dim& o) const {
    Dim r;
    for (size_t i = 0; i < e.size(); i++) {
        r.e[i] = e[i] + o.e[i];
    }
    return r;
}

Dim Dim::operator/(const Dim& o) const {
    Dim r;
    for (size_t i = 0; i < e.size(); i++) {
        r.e[i] = e[i] - o.e[i];
    }
    return r;
}

Dim Dim::pow(int n) const {
    Dim r;
    for (size_t i = 0; i < e.size(); i++) {
        r.e[i] = e[i] * n;
    }
    return r;
}

namespace {

constexpr Dim D(int m, int kg, int s, int A = 0, int K = 0, int mol = 0, int cd = 0) {
    return Dim{{m, kg, s, A, K, mol, cd}};
}

constexpr Dim DIMLESS = D(0, 0, 0);
constexpr Dim LENGTH = D(1, 0, 0);
constexpr Dim AREA = D(2, 0, 0);
constexpr Dim VOLUME = D(3, 0, 0);
constexpr Dim MASS = D(0, 1, 0);
constexpr Dim TIME = D(0, 0, 1);
constexpr Dim FORCE = D(1, 1, -2);
constexpr Dim PRESSURE = D(-1, 1, -2);
constexpr Dim ENERGY = D(2, 1, -2);
constexpr Dim POWER = D(2, 1, -3);

constexpr double G0 = 9.80665;  // standard gravity, m/s²
constexpr double INCH = 0.0254;
constexpr double LBM = 0.45359237;
constexpr double LBF = LBM * G0;

struct UnitDef {
    const char* name;
    double factor;
    Dim dim;
};

// Names are matched exactly (case sensitive). User variables take precedence over units, like in Mathcad.
const UnitDef UNITS[] = {
        // length
        {"m", 1, LENGTH},
        {"km", 1e3, LENGTH},
        {"cm", 1e-2, LENGTH},
        {"mm", 1e-3, LENGTH},
        {"μm", 1e-6, LENGTH},
        {"um", 1e-6, LENGTH},
        {"in", INCH, LENGTH},
        {"ft", 12 * INCH, LENGTH},
        {"yd", 36 * INCH, LENGTH},
        {"mi", 1609.344, LENGTH},
        // area / volume
        {"ha", 1e4, AREA},
        {"L", 1e-3, VOLUME},
        {"mL", 1e-6, VOLUME},
        // mass ("g" is gravity, as in Mathcad; gram is "gm")
        {"kg", 1, MASS},
        {"gm", 1e-3, MASS},
        {"mg", 1e-6, MASS},
        {"t", 1e3, MASS},
        {"ton", 1e3, MASS},
        {"lb", LBM, MASS},
        // time
        {"s", 1, TIME},
        {"ms", 1e-3, TIME},
        {"min", 60, TIME},
        {"h", 3600, TIME},
        {"hr", 3600, TIME},
        {"day", 86400, TIME},
        {"yr", 365.25 * 86400, TIME},
        // force
        {"N", 1, FORCE},
        {"kN", 1e3, FORCE},
        {"MN", 1e6, FORCE},
        {"kgf", G0, FORCE},
        {"tf", 1e3 * G0, FORCE},
        {"tonf", 1e3 * G0, FORCE},
        {"lbf", LBF, FORCE},
        {"kip", 1e3 * LBF, FORCE},
        // pressure / stress
        {"Pa", 1, PRESSURE},
        {"kPa", 1e3, PRESSURE},
        {"MPa", 1e6, PRESSURE},
        {"GPa", 1e9, PRESSURE},
        {"bar", 1e5, PRESSURE},
        {"atm", 101325, PRESSURE},
        {"psi", LBF / (INCH * INCH), PRESSURE},
        {"ksi", 1e3 * LBF / (INCH * INCH), PRESSURE},
        // energy / power
        {"J", 1, ENERGY},
        {"kJ", 1e3, ENERGY},
        {"MJ", 1e6, ENERGY},
        {"cal", 4.184, ENERGY},
        {"kcal", 4184, ENERGY},
        {"kWh", 3.6e6, ENERGY},
        {"W", 1, POWER},
        {"kW", 1e3, POWER},
        {"MW", 1e6, POWER},
        {"hp", 745.69987158227022, POWER},
        // frequency
        {"Hz", 1, D(0, 0, -1)},
        {"kHz", 1e3, D(0, 0, -1)},
        {"rpm", 1.0 / 60, D(0, 0, -1)},
        // angles (dimensionless)
        {"rad", 1, DIMLESS},
        {"deg", std::numbers::pi / 180, DIMLESS},
        {"°", std::numbers::pi / 180, DIMLESS},
        // electricity
        {"A", 1, D(0, 0, 0, 1)},
        {"mA", 1e-3, D(0, 0, 0, 1)},
        {"V", 1, D(2, 1, -3, -1)},
        {"kV", 1e3, D(2, 1, -3, -1)},
        {"Ω", 1, D(2, 1, -3, -2)},
        {"ohm", 1, D(2, 1, -3, -2)},
        {"C", 1, D(0, 0, 1, 1)},
        // others
        {"K", 1, D(0, 0, 0, 0, 1)},
        {"mol", 1, D(0, 0, 0, 0, 0, 1)},
        {"cd", 1, D(0, 0, 0, 0, 0, 0, 1)},
        // constants
        {"π", std::numbers::pi, DIMLESS},
        {"pi", std::numbers::pi, DIMLESS},
        {"g", G0, D(1, 0, -2)},
        {"%", 0.01, DIMLESS},
};

const std::unordered_map<std::string_view, const UnitDef*>& unitMap() {
    static const auto map = [] {
        std::unordered_map<std::string_view, const UnitDef*> m;
        for (const auto& u: UNITS) {
            m.emplace(u.name, &u);
        }
        return m;
    }();
    return map;
}

// Units used when a result is displayed without an explicit unit
struct DisplayUnit {
    Dim dim;
    const char* name;
    bool prefixed;  ///< use k, M, G so that the number stays below 1000 (e.g. 112500 N·m → 112.5 kN·m)
};
const DisplayUnit DISPLAY_UNITS[] = {
        {FORCE, "N", true},           {PRESSURE, "Pa", true},
        {ENERGY, "N·m", true},  // moments are far more common than energies in engineering sheets
        {POWER, "W", true},           {D(1, 0, -1), "m/s", false},
        {D(1, 0, -2), "m/s²", false}, {D(-3, 1, 0), "kg/m³", false},
        {D(0, 1, -2), "N/m", true},   {D(-2, 1, -2), "N/m³", true},
        {D(-1, 0, 0), "1/m", false},  {D(0, 0, -1), "1/s", false},
};

const DisplayUnit* findDisplayUnit(const Dim& d) {
    for (const auto& du: DISPLAY_UNITS) {
        if (du.dim == d) {
            return &du;
        }
    }
    return nullptr;
}

const char* const SUPERSCRIPT_DIGITS[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
const char* const BASE_UNIT_NAMES[] = {"m", "kg", "s", "A", "K", "mol", "cd"};

std::string superscript(int n) {
    std::string s;
    if (n < 0) {
        s += "⁻";
        n = -n;
    }
    for (char c: std::to_string(n)) {
        s += SUPERSCRIPT_DIGITS[c - '0'];
    }
    return s;
}

}  // namespace

std::optional<std::string> canonicalUnitName(std::string_view name) {
    const auto& map = unitMap();
    if (map.count(name)) {
        return std::string(name);
    }
    // Case-insensitive match ("kpa" → kPa, "kn" → kN) for names of two or more ASCII letters, only when exactly one
    // unit matches. Single letters are left alone so that "a", "n" or "v" do not silently become A, N or V
    if (name.size() < 2 || !std::all_of(name.begin(), name.end(),
                                        [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; })) {
        return std::nullopt;
    }
    auto lower = [](std::string_view v) {
        std::string r(v);
        std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
        return r;
    };
    const std::string key = lower(name);
    std::optional<std::string> found;
    for (const auto& u: UNITS) {
        if (lower(u.name) == key) {
            if (found) {
                return std::nullopt;  // ambiguous
            }
            found = u.name;
        }
    }
    return found;
}

std::optional<Quantity> findUnit(std::string_view name) {
    const auto& map = unitMap();
    auto canonical = canonicalUnitName(name);
    if (!canonical) {
        return std::nullopt;
    }
    const auto* u = map.at(*canonical);
    return Quantity{u->factor, u->dim};
}

std::optional<std::pair<std::string, int>> splitUnitPower(std::string_view name) {
    size_t d = name.size();
    while (d > 0 && std::isdigit(static_cast<unsigned char>(name[d - 1]))) {
        d--;
    }
    if (d == 0 || d == name.size() || name.size() - d > 2 || name[d] == '0') {
        return std::nullopt;
    }
    auto base = canonicalUnitName(name.substr(0, d));
    if (!base) {
        return std::nullopt;
    }
    return std::make_pair(*base, std::stoi(std::string(name.substr(d))));
}

std::string formatNumber(double v) {
    if (std::isnan(v)) {
        return "NaN";
    }
    if (std::isinf(v)) {
        return v > 0 ? "∞" : "-∞";
    }
    if (v == 0) {
        return "0";
    }
    double a = std::abs(v);
    char buf[64];
    auto trim = [](std::string s) {
        if (s.find('.') != std::string::npos) {
            while (!s.empty() && s.back() == '0') {
                s.pop_back();
            }
            if (!s.empty() && s.back() == '.') {
                s.pop_back();
            }
        }
        return s;
    };
    if (a >= 1 && a < 1e7) {
        std::snprintf(buf, sizeof(buf), "%.3f", v);
        return trim(buf);
    }
    if (a >= 1e-3 && a < 1) {
        // 4 significant digits
        int decimals = 3 - static_cast<int>(std::floor(std::log10(a)));
        std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        return trim(buf);
    }
    int exp = static_cast<int>(std::floor(std::log10(a)));
    double mant = v / std::pow(10.0, exp);
    if (std::abs(mant) >= 9.9995) {  // rounding pushed the mantissa to 10
        mant /= 10;
        exp++;
    }
    std::snprintf(buf, sizeof(buf), "%.3f", mant);
    return trim(buf) + "·10" + superscript(exp);
}

std::string formatDim(const Dim& d) {
    std::string num, den;
    for (size_t i = 0; i < d.e.size(); i++) {
        int x = d.e[i];
        if (x == 0) {
            continue;
        }
        std::string& target = x > 0 ? num : den;
        if (!target.empty()) {
            target += "·";
        }
        target += BASE_UNIT_NAMES[i];
        if (std::abs(x) != 1) {
            target += superscript(std::abs(x));
        }
    }
    if (num.empty() && den.empty()) {
        return "";
    }
    if (den.empty()) {
        return num;
    }
    if (den.find("·") != std::string::npos) {
        den = "(" + den + ")";
    }
    return (num.empty() ? "1" : num) + "/" + den;
}

/// Name of a dimension for messages: a familiar unit if there is one ("Pa"), otherwise SI base units
static std::string dimName(const Dim& d) {
    if (d.isDimensionless()) {
        return "(sin unidades)";
    }
    // pressure and N/m² share a dimension: prefer the first entry
    for (const auto& du: DISPLAY_UNITS) {
        if (du.dim == d) {
            return du.name;
        }
    }
    return formatDim(d);
}

std::string formatQuantity(const Quantity& q) {
    if (q.dim.isDimensionless()) {
        return formatNumber(q.value);
    }
    if (const DisplayUnit* du = findDisplayUnit(q.dim)) {
        double v = q.value;
        const char* prefix = "";
        if (du->prefixed) {
            static const std::pair<double, const char*> PREFIXES[] = {{1e9, "G"}, {1e6, "M"}, {1e3, "k"}};
            for (const auto& [f, p]: PREFIXES) {
                if (std::abs(q.value) >= f) {
                    v = q.value / f;
                    prefix = p;
                    break;
                }
            }
        }
        return formatNumber(v) + " " + prefix + du->name;
    }
    return formatNumber(q.value) + " " + formatDim(q.dim);
}

// ---------------------------------------------------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------------------------------------------------

namespace {

enum class Tok { Num, Ident, Str, Op, LParen, RParen, Comma, Assign, Eq, Question, End };

struct Token {
    Tok type;
    std::string text;
    double num = 0;
};

bool startsWith(std::string_view s, size_t pos, std::string_view prefix) {
    return s.substr(pos, prefix.size()) == prefix;
}

// Multi-byte characters that act as operators instead of identifier characters
struct Symbol {
    const char* utf8;
    Tok type;
    const char* op;
};
const Symbol SYMBOLS[] = {
        {"·", Tok::Op, "*"}, {"×", Tok::Op, "*"},  {"⋅", Tok::Op, "*"},       {"∙", Tok::Op, "*"},
        {"÷", Tok::Op, "/"}, {"−", Tok::Op, "-"},  {"√", Tok::Ident, "sqrt"}, {"≔", Tok::Assign, ":="},
        {"±", Tok::Op, "±"}, {"≤", Tok::Op, "<="}, {"≥", Tok::Op, ">="},      {"≠", Tok::Op, "!="},
};

int superscriptValue(std::string_view s, size_t pos, size_t& len) {
    for (int i = 0; i < 10; i++) {
        if (startsWith(s, pos, SUPERSCRIPT_DIGITS[i])) {
            len = std::strlen(SUPERSCRIPT_DIGITS[i]);
            return i;
        }
    }
    return -1;
}

bool isIdentStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
bool isIdentChar(unsigned char c) { return std::isalnum(c) || c == '_' || c == '\'' || c >= 0x80; }

std::vector<Token> tokenize(std::string_view s) {
    std::vector<Token> out;
    size_t i = 0;
    auto isSymbolAt = [&](size_t p) -> const Symbol* {
        for (const auto& sym: SYMBOLS) {
            if (startsWith(s, p, sym.utf8)) {
                return &sym;
            }
        }
        return nullptr;
    };
    auto isSuperscriptAt = [&](size_t p) {
        size_t l;
        return superscriptValue(s, p, l) >= 0 || startsWith(s, p, "⁻");
    };

    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isspace(c)) {
            i++;
            continue;
        }
        if (const Symbol* sym = isSymbolAt(i)) {
            out.push_back({sym->type, sym->op});
            i += std::strlen(sym->utf8);
            continue;
        }
        // Text between quotes (a cell reference or a file name): "Hoja1!C5", also with typographic quotes
        if (c == '"' || startsWith(s, i, "“") || startsWith(s, i, "”")) {
            size_t open = c == '"' ? 1 : std::strlen("“");
            size_t start = i + open;
            size_t end = start;
            size_t close = 0;
            for (; end < s.size(); end++) {
                if (s[end] == '"') {
                    close = 1;
                    break;
                }
                if (startsWith(s, end, "”") || startsWith(s, end, "“")) {
                    close = std::strlen("”");
                    break;
                }
            }
            if (close == 0) {
                throw CalcError("faltan las comillas de cierre");
            }
            out.push_back({Tok::Str, std::string(s.substr(start, end - start))});
            i = end + close;
            continue;
        }
        if (isSuperscriptAt(i)) {
            // x² → x ^ 2
            bool neg = false;
            if (startsWith(s, i, "⁻")) {
                neg = true;
                i += std::strlen("⁻");
            }
            int value = 0;
            size_t len = 0;
            int d;
            bool any = false;
            while (i < s.size() && (d = superscriptValue(s, i, len)) >= 0) {
                value = value * 10 + d;
                i += len;
                any = true;
            }
            if (!any) {
                throw CalcError("exponente incompleto");
            }
            out.push_back({Tok::Op, "^"});
            out.push_back({Tok::Num, "", neg ? -value : static_cast<double>(value)});
            continue;
        }
        if (std::isdigit(c) || (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            size_t start = i;
            while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) {
                i++;
            }
            // exponent: only if followed by a digit, so that "2 e" or "3m" are not affected
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                size_t j = i + 1;
                if (j < s.size() && (s[j] == '+' || s[j] == '-')) {
                    j++;
                }
                if (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) {
                    i = j;
                    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
                        i++;
                    }
                }
            }
            std::string txt(s.substr(start, i - start));
            char* end = nullptr;
            double v = std::strtod(txt.c_str(), &end);
            if (end == nullptr || *end != '\0') {
                throw CalcError("número inválido: " + txt);
            }
            out.push_back({Tok::Num, txt, v});
            continue;
        }
        if (isIdentStart(c)) {
            size_t start = i;
            while (i < s.size() && isIdentChar(static_cast<unsigned char>(s[i])) && !isSymbolAt(i) &&
                   !isSuperscriptAt(i)) {
                i++;
            }
            out.push_back({Tok::Ident, std::string(s.substr(start, i - start))});
            continue;
        }
        if (startsWith(s, i, ":=")) {
            out.push_back({Tok::Assign, ":="});
            i += 2;
            continue;
        }
        switch (c) {
            case '+':
            case '-':
            case '*':
            case '/':
            case '^':
                out.push_back({Tok::Op, std::string(1, static_cast<char>(c))});
                break;
            case '%':
                out.push_back({Tok::Ident, "%"});
                break;
            case '<':
            case '>':
                if (i + 1 < s.size() && s[i + 1] == '=') {
                    out.push_back({Tok::Op, std::string(1, static_cast<char>(c)) + "="});
                    i++;
                } else {
                    out.push_back({Tok::Op, std::string(1, static_cast<char>(c))});
                }
                break;
            case '(':
            case '[':
                out.push_back({Tok::LParen, "("});
                break;
            case ')':
            case ']':
                out.push_back({Tok::RParen, ")"});
                break;
            case ',':
            case ';':
                out.push_back({Tok::Comma, ","});
                break;
            case '=':
                out.push_back({Tok::Eq, "="});
                break;
            case '?':
                out.push_back({Tok::Question, "?"});
                break;
            default:
                throw CalcError(std::string("carácter no reconocido: ") + static_cast<char>(c));
        }
        i++;
    }
    out.push_back({Tok::End, ""});
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Parser / evaluator (recursive descent, evaluates directly)
// ---------------------------------------------------------------------------------------------------------------------

class Parser {
public:
    /// unitsOnly: identifiers are resolved as units only (used for "= ? kN·m")
    Parser(const std::vector<Token>& toks, size_t pos, const Environment* env): toks(toks), pos(pos), env(env) {}

    /// Comparisons (<, >, ≤, ≥, ≠), lowest precedence: the result is 1 (it holds) or 0. Chains like "0 < x ≤ 5" hold
    /// when every comparison holds
    Quantity parseComparison() {
        Quantity q = parseExpression();
        bool holds = true;
        while (isComparison(peek())) {
            std::string op = next().text;
            Quantity r = parseExpression();
            // A bare 0 can be compared with anything ("0 < s ≤ fy")
            const bool zero = (q.value == 0 && q.dim == DIMLESS) || (r.value == 0 && r.dim == DIMLESS);
            if (!(q.dim == r.dim) && !zero) {
                throw CalcError("no se pueden comparar " + dimName(q.dim) + " y " + dimName(r.dim));
            }
            const double tol = 1e-12 * std::max({1.0, std::abs(q.value), std::abs(r.value)});
            const double d = q.value - r.value;
            bool ok = op == "<"  ? d < -tol :
                      op == ">"  ? d > tol :
                      op == "<=" ? d <= tol :
                      op == ">=" ? d >= -tol :
                                   std::abs(d) > tol;  // !=
            holds = holds && ok;
            comparison = true;
            q = r;
        }
        return comparison ? Quantity{holds ? 1.0 : 0.0, DIMLESS} : q;
    }

    Quantity parseExpression() {
        Quantity q = parseTerm();
        while (peek().type == Tok::Op && (peek().text == "+" || peek().text == "-" || peek().text == "±")) {
            std::string op = next().text;
            if (op == "±") {
                usedPlusMinus = true;
                op = pmSign > 0 ? "+" : "-";
            }
            Quantity r = parseTerm();
            if (!(q.dim == r.dim)) {
                throw CalcError("unidades incompatibles: " + dimName(q.dim) + " " + op + " " + dimName(r.dim));
            }
            q.value = op == "+" ? q.value + r.value : q.value - r.value;
        }
        return q;
    }

    static bool isComparison(const Token& t) {
        return t.type == Tok::Op &&
               (t.text == "<" || t.text == ">" || t.text == "<=" || t.text == ">=" || t.text == "!=");
    }

    int pmSign = 1;              ///< ± is evaluated as + (1) or as - (-1)
    bool usedPlusMinus = false;  ///< the expression has a ±, so it has two results
    bool comparison = false;     ///< the expression is a comparison (result 1 or 0)

    const Token& peek() const { return toks[pos]; }
    const Token& next() { return toks[pos++]; }
    size_t position() const { return pos; }

private:
    /// Limit of nested parentheses, signs and exponents: deeper expressions would overflow the stack
    static constexpr int MAX_NESTING = 400;
    int nesting = 0;
    struct Nest {
        explicit Nest(int& n): n(n) {
            if (++n > MAX_NESTING) {
                --n;
                throw CalcError("expresión demasiado anidada");
            }
        }
        ~Nest() { --n; }
        Nest(const Nest&) = delete;
        Nest& operator=(const Nest&) = delete;
        int& n;
    };

    bool startsPrimary() const {
        const auto& t = peek();
        return t.type == Tok::Num || t.type == Tok::Ident || t.type == Tok::LParen;
    }

    Quantity parseTerm() {
        Quantity q = parseUnary();
        while (true) {
            if (peek().type == Tok::Op && (peek().text == "*" || peek().text == "/")) {
                bool div = next().text == "/";
                Quantity r = parseUnary();
                q = div ? divide(q, r) : multiply(q, r);
            } else {
                return q;
            }
        }
    }

    static Quantity multiply(const Quantity& a, const Quantity& b) { return {a.value * b.value, a.dim * b.dim}; }
    static Quantity divide(const Quantity& a, const Quantity& b) {
        if (b.value == 0) {
            throw CalcError("división por cero");
        }
        return {a.value / b.value, a.dim / b.dim};
    }

    Quantity parseUnary() {
        Nest nest(nesting);
        if (peek().type == Tok::Op && (peek().text == "-" || peek().text == "+" || peek().text == "±")) {
            std::string op = next().text;
            if (op == "±") {
                usedPlusMinus = true;
            }
            bool neg = op == "-" || (op == "±" && pmSign < 0);
            Quantity q = parseUnary();
            if (neg) {
                q.value = -q.value;
            }
            return q;
        }
        return parseImplicit();
    }

    /// Implicit multiplication binds tighter than '*' and '/': "1 kN / 1 mm^2", "9.81 m/s^2"
    Quantity parseImplicit() {
        Quantity q = parsePower();
        while (startsPrimary()) {
            q = multiply(q, parsePower());
        }
        return q;
    }

    Quantity parsePower() {
        Quantity base = parsePrimary();
        if (peek().type == Tok::Op && peek().text == "^") {
            next();
            Quantity ex = parseExponent();  // right associative
            return power(base, ex);
        }
        return base;
    }

    /// The exponent is a single (possibly signed) factor: "2^-1", "m^2 kN" = (m^2)·kN
    Quantity parseExponent() {
        Nest nest(nesting);
        if (peek().type == Tok::Op && (peek().text == "-" || peek().text == "+")) {
            bool neg = next().text == "-";
            Quantity q = parseExponent();
            if (neg) {
                q.value = -q.value;
            }
            return q;
        }
        return parsePower();
    }

    static Quantity power(const Quantity& base, const Quantity& ex) {
        if (!ex.dim.isDimensionless()) {
            throw CalcError("el exponente debe ser adimensional");
        }
        if (base.dim.isDimensionless()) {
            return {std::pow(base.value, ex.value), {}};
        }
        double n = ex.value;
        if (std::abs(n - std::round(n)) < 1e-12) {
            int k = static_cast<int>(std::round(n));
            return {std::pow(base.value, k), base.dim.pow(k)};
        }
        // fractional exponent: all dimension exponents must become integers
        Dim d;
        for (size_t i = 0; i < d.e.size(); i++) {
            double x = base.dim.e[i] * n;
            if (std::abs(x - std::round(x)) > 1e-9) {
                throw CalcError("potencia fraccionaria de una cantidad con unidades " + dimName(base.dim));
            }
            d.e[i] = static_cast<int>(std::round(x));
        }
        return {std::pow(base.value, n), d};
    }

    Quantity parsePrimary() {
        Nest nest(nesting);
        const Token& t = next();
        switch (t.type) {
            case Tok::Num:
                return {t.num, {}};
            case Tok::LParen: {
                Quantity q = parseExpression();
                expect(Tok::RParen, "falta ')'");
                return q;
            }
            case Tok::Ident:
                if (peek().type == Tok::LParen && t.text == "xl") {
                    next();
                    std::string ref = parseCellRef();
                    expect(Tok::RParen, "falta ')' en xl");
                    if (!env || !env->cells) {
                        throw CalcError("xl() no está disponible aquí");
                    }
                    return {env->cells->read(ref), {}};
                }
                if (peek().type == Tok::LParen && isFunction(t.text)) {
                    next();
                    std::vector<Quantity> args;
                    if (peek().type != Tok::RParen) {
                        args.push_back(parseExpression());
                        while (peek().type == Tok::Comma) {
                            next();
                            args.push_back(parseExpression());
                        }
                    }
                    expect(Tok::RParen, "falta ')' en " + t.text);
                    return callFunction(t.text, args);
                }
                if (t.text == "sqrt") {  // "√x" without parentheses
                    return callFunction("sqrt", {parsePower()});
                }
                return lookup(t.text);
            case Tok::End:
                throw CalcError("expresión incompleta");
            default:
                throw CalcError("símbolo inesperado: " + t.text);
        }
    }

public:
    /// A cell reference: "Hoja1!C5" between quotes, or a plain cell name without quotes (C5)
    std::string parseCellRef() {
        const Token& r = next();
        if (r.type == Tok::Str) {
            return r.text;
        }
        if (r.type == Tok::Ident && isCellName(r.text)) {
            return r.text;
        }
        throw CalcError("la celda va entre comillas, p. ej. \"Hoja1!C5\"");
    }

    static bool isCellName(const std::string& n) {
        size_t i = 0;
        while (i < n.size() && std::isalpha(static_cast<unsigned char>(n[i]))) {
            i++;
        }
        if (i == 0 || i > 3 || i == n.size()) {
            return false;
        }
        return std::all_of(n.begin() + static_cast<std::ptrdiff_t>(i), n.end(),
                           [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
    }

private:
    void expect(Tok type, const std::string& msg) {
        if (peek().type != type) {
            throw CalcError(msg);
        }
        next();
    }

    Quantity lookup(const std::string& name) const {
        if (env) {
            if (auto it = env->vars.find(name); it != env->vars.end()) {
                return it->second;
            }
        }
        if (auto u = findUnit(name)) {
            return *u;
        }
        // "m3" means m³ (unless "m" is a variable of the user)
        if (auto up = splitUnitPower(name); up && !(env && env->vars.count(up->first))) {
            Quantity u = *findUnit(up->first);
            return Quantity{std::pow(u.value, up->second), u.dim.pow(up->second)};
        }
        // "As" finds "A_s" (a subscript written with Ctrl+PgDown) when only one variable matches
        if (env && name.find('_') == std::string::npos) {
            const Quantity* match = nullptr;
            int count = 0;
            for (const auto& [var, q]: env->vars) {
                std::string plain = var;
                plain.erase(std::remove(plain.begin(), plain.end(), '_'), plain.end());
                if (plain == name && plain != var) {
                    match = &q;
                    count++;
                }
            }
            if (count == 1) {
                return *match;
            }
        }
        throw CalcError(env ? "variable no definida: " + name : "unidad desconocida: " + name);
    }

    static bool isFunction(const std::string& n) {
        static const char* const FUNCS[] = {"sqrt", "abs", "sin", "cos", "tan", "asin",  "acos",  "atan", "atan2",
                                            "ln",   "log", "exp", "min", "max", "round", "floor", "ceil", "cbrt"};
        return std::any_of(std::begin(FUNCS), std::end(FUNCS), [&](const char* f) { return n == f; });
    }

    static void requireArgs(const std::string& f, const std::vector<Quantity>& a, size_t n) {
        if (a.size() != n) {
            throw CalcError(f + " requiere " + std::to_string(n) + " argumento(s)");
        }
    }

    static double dimless(const std::string& f, const Quantity& q) {
        if (!q.dim.isDimensionless()) {
            throw CalcError("el argumento de " + f + " debe ser adimensional");
        }
        return q.value;
    }

    static Quantity callFunction(const std::string& f, const std::vector<Quantity>& a) {
        using std::numbers::pi;
        if (f == "min" || f == "max") {
            if (a.empty()) {
                throw CalcError(f + " requiere argumentos");
            }
            Quantity r = a[0];
            for (const auto& q: a) {
                if (!(q.dim == r.dim)) {
                    throw CalcError("unidades incompatibles en " + f);
                }
                if (f == "min" ? q.value < r.value : q.value > r.value) {
                    r = q;
                }
            }
            return r;
        }
        if (f == "atan2") {
            requireArgs(f, a, 2);
            if (!(a[0].dim == a[1].dim)) {
                throw CalcError("unidades incompatibles en atan2");
            }
            return {std::atan2(a[0].value, a[1].value), {}};
        }
        requireArgs(f, a, 1);
        const Quantity& x = a[0];
        if (f == "sqrt") {
            return power(x, {0.5, {}});
        }
        if (f == "cbrt") {
            return power(x, {1.0 / 3.0, {}});
        }
        if (f == "abs") {
            return {std::abs(x.value), x.dim};
        }
        if (f == "round" || f == "floor" || f == "ceil") {
            double v = dimless(f, x);
            return {f == "round" ? std::round(v) : f == "floor" ? std::floor(v) : std::ceil(v), {}};
        }
        double v = dimless(f, x);
        double r;
        if (f == "sin") {
            r = std::sin(v);
        } else if (f == "cos") {
            r = std::cos(v);
        } else if (f == "tan") {
            r = std::tan(v);
        } else if (f == "asin") {
            r = std::asin(v);
        } else if (f == "acos") {
            r = std::acos(v);
        } else if (f == "atan") {
            r = std::atan(v);
        } else if (f == "ln") {
            r = std::log(v);
        } else if (f == "log") {
            r = std::log10(v);
        } else {  // exp
            r = std::exp(v);
        }
        if (std::isnan(r)) {
            throw CalcError("argumento fuera de dominio en " + f);
        }
        (void)pi;
        return {r, {}};
    }

    const std::vector<Token>& toks;
    size_t pos;
    const Environment* env;  ///< nullptr: identifiers are units only
};

std::string trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        a++;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        b--;
    }
    return std::string(s.substr(a, b - a));
}

/// The result shown after "=": either automatic units, or the units requested after "?"
std::string formatResult(const Quantity& q, const std::vector<Token>& toks, size_t& pos, std::string_view line,
                         std::string& display) {
    if (toks[pos].type == Tok::End) {
        std::string r = formatQuantity(q);
        display = trim(line) + " " + r;
        return r;
    }
    if (toks[pos].type != Tok::Question) {
        throw CalcError("después de '=' solo puede ir '? unidad'");
    }
    pos++;
    // Requested unit: the text after '?' in the original line, evaluated with units only
    size_t qpos = line.rfind('?');
    std::string unitText = trim(line.substr(qpos + 1));
    if (unitText.empty()) {
        throw CalcError("falta la unidad después de '?'");
    }
    Parser up(toks, pos, nullptr);
    Quantity unit = up.parseExpression();
    if (up.peek().type != Tok::End) {
        throw CalcError("unidad inválida: " + unitText);
    }
    if (!(unit.dim == q.dim)) {
        throw CalcError("las unidades " + unitText + " no corresponden a " + dimName(q.dim));
    }
    std::string r = formatNumber(q.value / unit.value) + " " + unitText;
    display = trim(line.substr(0, qpos)) + " " + r;
    return r;
}

/// The text of the last argument of a call written in one line, "enviar(M, \"D4\", kN·m)" → "kN·m"
std::string lastArgumentText(std::string_view t) {
    int depth = 0;
    bool quoted = false;
    size_t lastComma = std::string_view::npos;
    size_t close = std::string_view::npos;
    for (size_t i = 0; i < t.size(); i++) {
        char c = t[i];
        if (c == '"') {
            quoted = !quoted;
        } else if (quoted) {
            continue;
        } else if (c == '(' || c == '[') {
            depth++;
        } else if (c == ')' || c == ']') {
            if (--depth == 0) {
                close = i;
            }
        } else if (c == ',' && depth == 1) {
            lastComma = i;
        }
    }
    if (lastComma == std::string_view::npos || close == std::string_view::npos || close < lastComma) {
        return {};
    }
    return trim(t.substr(lastComma + 1, close - lastComma - 1));
}

/// Statements that are not written with "=": excel("libro.xlsx") and enviar(valor, "celda", unidad).
/// Returns false if the line is not one of them
bool evaluateStatement(const std::string& t, Environment& env, LineResult& res) {
    std::vector<Token> toks;
    try {
        toks = tokenize(t);
    } catch (const CalcError&) {
        return false;  // plain text that starts with the same word
    }
    if (toks.size() < 3 || toks[0].type != Tok::Ident || toks[1].type != Tok::LParen ||
        (toks[0].text != "excel" && toks[0].text != "enviar")) {
        return false;
    }
    if (!env.cells) {
        throw CalcError(toks[0].text + "() no está disponible aquí");
    }
    if (toks[0].text == "excel") {
        if (toks.size() != 5 || toks[2].type != Tok::Str || toks[3].type != Tok::RParen) {
            throw CalcError("escribe el libro entre comillas: excel(\"cargas.xlsx\")");
        }
        res.result = "→ " + env.cells->useWorkbook(toks[2].text);
    } else {
        Parser p(toks, 2, &env);
        Quantity q = p.parseExpression();
        if (p.peek().type != Tok::Comma) {
            throw CalcError("uso: enviar(valor, \"Hoja1!D4\", unidad)");
        }
        p.next();
        std::string ref = p.parseCellRef();
        std::string unitText;
        double value = q.value;
        size_t end = p.position();
        if (p.peek().type == Tok::Comma) {
            p.next();
            unitText = lastArgumentText(t);
            Parser up(toks, p.position(), nullptr);  // units only, as after "= ?"
            Quantity unit = up.parseExpression();
            if (!(unit.dim == q.dim)) {
                throw CalcError("las unidades " + unitText + " no corresponden a " + dimName(q.dim));
            }
            value = q.value / unit.value;
            end = up.position();
        } else if (!q.dim.isDimensionless()) {
            throw CalcError("indica la unidad en que se envía: enviar(valor, \"" + ref + "\", " + formatDim(q.dim) +
                            ")");
        }
        if (toks[end].type != Tok::RParen || toks[end + 1].type != Tok::End) {
            throw CalcError("uso: enviar(valor, \"Hoja1!D4\", unidad)");
        }
        std::string where = env.cells->write(ref, value);
        res.result = "→ " + where + ": " + formatNumber(value) + (unitText.empty() ? "" : " " + unitText);
    }
    res.kind = LineResult::Kind::Evaluation;
    res.display = t + "  " + res.result;
    return true;
}

}  // namespace

Quantity evaluateExpression(std::string_view expr, const Environment& env) {
    auto toks = tokenize(expr);
    Parser p(toks, 0, &env);
    Quantity q = p.parseExpression();
    if (p.peek().type != Tok::End) {
        throw CalcError("símbolo inesperado: " + p.peek().text);
    }
    return q;
}

namespace {
LineResult evaluateLineUnchecked(std::string_view line, Environment& env);
}

LineResult evaluateLine(std::string_view line, Environment& env) {
    // Anything other than a CalcError (out of memory, a file system error while reading a workbook...) must not
    // escape: this runs inside GTK callbacks, where an exception ends the program
    try {
        return evaluateLineUnchecked(line, env);
    } catch (const std::exception& e) {
        LineResult res;
        res.source = std::string(line);
        res.display = res.source;
        res.kind = LineResult::Kind::Error;
        res.error = std::string("error interno: ") + e.what();
        if (env.cells) {
            env.cells->takeNotes();
        }
        return res;
    }
}

namespace {
LineResult evaluateLineUnchecked(std::string_view line, Environment& env) {
    LineResult res;
    res.source = std::string(line);
    res.display = res.source;

    std::string t = trim(line);
    if (t.starts_with("excel") || t.starts_with("enviar")) {
        try {
            if (evaluateStatement(t, env, res)) {
                if (env.cells) {
                    for (auto& n: env.cells->takeNotes()) {
                        res.note += (res.note.empty() ? "" : "; ") + n;
                    }
                }
                return res;
            }
        } catch (const CalcError& e) {
            res.kind = LineResult::Kind::Error;
            res.error = e.what();
            return res;
        }
    }
    // Plain text: empty lines, comments, and lines without any calculation operator
    if (t.empty() || t[0] == '#' || t[0] == '"' ||
        (t.find(":=") == std::string::npos && t.find('=') == std::string::npos && t.find("≔") == std::string::npos)) {
        return res;
    }

    try {
        auto toks = tokenize(t);
        size_t pos = 0;
        std::optional<std::string> defName;
        if (toks.size() > 2 && toks[0].type == Tok::Ident && toks[1].type == Tok::Assign) {
            defName = toks[0].text;
            if (findUnit(*defName) && env.vars.find(*defName) == env.vars.end()) {
                // Allowed (as in Mathcad), but the unit is hidden from now on
            }
            pos = 2;
        }
        const size_t exprStart = pos;
        Parser p(toks, pos, &env);
        Quantity q = p.parseComparison();
        pos = p.position();

        // "±": a second result with "-" ("x := (-b ± √(b²-4·a·c)) / (2·a)" gives both roots)
        std::optional<Quantity> q2;
        if (p.usedPlusMinus) {
            Parser p2(toks, exprStart, &env);
            p2.pmSign = -1;
            q2 = p2.parseComparison();
        }

        if (defName) {
            env.vars[*defName] = q;
            if (q2) {
                // x is the "+" result; x_1 and x_2 (written x1/x2 with Ctrl+PgDown) are both results
                env.vars[*defName + "_1"] = q;
                env.vars[*defName + "_2"] = *q2;
            }
            res.kind = LineResult::Kind::Definition;
        }
        if (toks[pos].type == Tok::Eq) {
            pos++;
            if (p.comparison) {
                if (toks[pos].type != Tok::End) {
                    throw CalcError("una comparación no lleva unidades después de '='");
                }
                auto verdict = [](const Quantity& v) { return v.value != 0 ? std::string("✓ cumple") : "✗ no cumple"; };
                res.result = verdict(q);
                if (q2 && q2->value != q.value) {
                    res.result = "con +: " + verdict(q) + " ; con −: " + verdict(*q2);
                }
                res.display = trim(t) + " " + res.result;
            } else if (q2) {
                size_t pos2 = pos;
                std::string display2;
                std::string r1 = formatResult(q, toks, pos, t, res.display);
                std::string r2 = formatResult(*q2, toks, pos2, t, display2);
                res.result = r1 == r2 ? r1 : r1 + " ; " + r2;
                if (r1 != r2) {
                    res.display += " ; " + r2;
                }
            } else {
                res.result = formatResult(q, toks, pos, t, res.display);
            }
            res.kind = LineResult::Kind::Evaluation;
        } else if (toks[pos].type != Tok::End) {
            throw CalcError("símbolo inesperado: " + toks[pos].text);
        } else if (!defName) {
            throw CalcError("falta '=' o ':='");
        }
    } catch (const CalcError& e) {
        res.kind = LineResult::Kind::Error;
        res.error = e.what();
    }
    if (env.cells) {
        for (auto& n: env.cells->takeNotes()) {
            res.note += (res.note.empty() ? "" : "; ") + n;
        }
    }
    return res;
}

}  // namespace

std::vector<LineResult> evaluateText(std::string_view text, Environment& env) {
    std::vector<LineResult> out;
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        out.push_back(evaluateLine(text.substr(start, nl == std::string_view::npos ? nl : nl - start), env));
        if (nl == std::string_view::npos) {
            break;
        }
        start = nl + 1;
    }
    return out;
}

std::string greekForLatin(char c) {
    // Mapping of the Symbol font, also used by Mathcad's Ctrl+G
    static const std::unordered_map<char, const char*> MAP = {
            {'a', "α"}, {'b', "β"}, {'g', "γ"}, {'d', "δ"}, {'e', "ε"}, {'z', "ζ"}, {'h', "η"}, {'q', "θ"}, {'i', "ι"},
            {'k', "κ"}, {'l', "λ"}, {'m', "μ"}, {'n', "ν"}, {'x', "ξ"}, {'o', "ο"}, {'p', "π"}, {'r', "ρ"}, {'s', "σ"},
            {'t', "τ"}, {'u', "υ"}, {'f', "φ"}, {'c', "χ"}, {'y', "ψ"}, {'w', "ω"}, {'j', "ϕ"}, {'v', "ϖ"}, {'A', "Α"},
            {'B', "Β"}, {'G', "Γ"}, {'D', "Δ"}, {'E', "Ε"}, {'Z', "Ζ"}, {'H', "Η"}, {'Q', "Θ"}, {'I', "Ι"}, {'K', "Κ"},
            {'L', "Λ"}, {'M', "Μ"}, {'N', "Ν"}, {'X', "Ξ"}, {'O', "Ο"}, {'P', "Π"}, {'R', "Ρ"}, {'S', "Σ"}, {'T', "Τ"},
            {'U', "Υ"}, {'F', "Φ"}, {'C', "Χ"}, {'Y', "Ψ"}, {'W', "Ω"}};
    if (auto it = MAP.find(c); it != MAP.end()) {
        return it->second;
    }
    return "";
}

}  // namespace xoj::calc
