#include "CalcEngine.h"

#include <algorithm>
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

std::optional<Quantity> findUnit(std::string_view name) {
    const auto& map = unitMap();
    if (auto it = map.find(name); it != map.end()) {
        return Quantity{it->second->factor, it->second->dim};
    }
    return std::nullopt;
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

enum class Tok { Num, Ident, Op, LParen, RParen, Comma, Assign, Eq, Question, End };

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
        {"·", Tok::Op, "*"}, {"×", Tok::Op, "*"}, {"⋅", Tok::Op, "*"},       {"∙", Tok::Op, "*"},
        {"÷", Tok::Op, "/"}, {"−", Tok::Op, "-"}, {"√", Tok::Ident, "sqrt"}, {"≔", Tok::Assign, ":="},
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

    Quantity parseExpression() {
        Quantity q = parseTerm();
        while (peek().type == Tok::Op && (peek().text == "+" || peek().text == "-")) {
            std::string op = next().text;
            Quantity r = parseTerm();
            if (!(q.dim == r.dim)) {
                throw CalcError("unidades incompatibles: " + dimName(q.dim) + " " + op + " " + dimName(r.dim));
            }
            q.value = op == "+" ? q.value + r.value : q.value - r.value;
        }
        return q;
    }

    const Token& peek() const { return toks[pos]; }
    const Token& next() { return toks[pos++]; }
    size_t position() const { return pos; }

private:
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
        if (peek().type == Tok::Op && (peek().text == "-" || peek().text == "+")) {
            bool neg = next().text == "-";
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

LineResult evaluateLine(std::string_view line, Environment& env) {
    LineResult res;
    res.source = std::string(line);
    res.display = res.source;

    std::string t = trim(line);
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
        Parser p(toks, pos, &env);
        Quantity q = p.parseExpression();
        pos = p.position();

        if (defName) {
            env.vars[*defName] = q;
            res.kind = LineResult::Kind::Definition;
        }
        if (toks[pos].type == Tok::Eq) {
            pos++;
            res.result = formatResult(q, toks, pos, t, res.display);
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
    return res;
}

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
