#include "LatexToCalc.h"

#include "CalcEngine.h"

#include <cctype>
#include <map>
#include <optional>
#include <vector>

namespace xoj::calc {

namespace {

struct Atom {
    enum class Kind {
        Operand,  ///< number, name, group in parentheses, fraction, root...
        Op,       ///< binary or unary operator, shown with spaces (+, -, =, ≤...)
        TightOp,  ///< operator shown without spaces (·, /)
        Func,     ///< function name, its argument follows
        Open,
        Close,
        Comma,
        Newline
    };
    Kind kind = Kind::Operand;
    std::string text;
    bool closed = false;    ///< a following name must not be glued to it (it has a subscript, power, group...)
    bool fraction = false;  ///< "a/b" without parentheses: wrap it when something multiplies it
    bool unit = false;      ///< a unit name: a digit right after it is its power (m 3 → m3)
};

bool isAsciiAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }

/// Last character class of a text: 'd' digit, 'a' letter (ASCII or not), 'o' other, 0 empty
char lastClass(const std::string& s) {
    if (s.empty()) {
        return 0;
    }
    char c = s.back();
    if (isDigit(c) || c == '.') {
        return 'd';
    }
    if (isAsciiAlpha(c) || (static_cast<unsigned char>(c) & 0x80) || c == '\'') {
        return 'a';
    }
    return 'o';
}

char firstClass(const std::string& s) {
    if (s.empty()) {
        return 0;
    }
    char c = s.front();
    if (isDigit(c) || c == '.') {
        return 'd';
    }
    if (isAsciiAlpha(c)) {
        return 'a';
    }
    if (static_cast<unsigned char>(c) & 0x80) {
        // √ and ° are not letters
        if (s.rfind("√", 0) == 0 || s.rfind("°", 0) == 0) {
            return 'o';
        }
        return 'a';
    }
    return 'o';
}

/// A text that needs no parentheses as an operand of "/", "^" or a function
bool isSimple(const std::string& s) {
    if (s.empty()) {
        return true;
    }
    if (s.front() == '(' && s.back() == ')') {
        // "(a)(b)" is not one group
        int depth = 0;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '(') {
                depth++;
            } else if (s[i] == ')') {
                depth--;
                if (depth == 0 && i + 1 < s.size()) {
                    return false;
                }
            }
        }
        return true;
    }
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isalnum(c) || c == '.' || c == '_' || c == '\'' || c == '^' || c >= 0x80) {
            if (s.compare(i, std::string("√").size(), "√") == 0 || s.compare(i, std::string("·").size(), "·") == 0 ||
                s.compare(i, std::string("±").size(), "±") == 0) {
                return false;
            }
            continue;
        }
        return false;
    }
    return true;
}

std::string paren(const std::string& s) { return isSimple(s) ? s : "(" + s + ")"; }

/// A numerator only needs parentheses for sums, signs and comparisons ("F·L/4" is (F·L)/4)
std::string parenNumerator(const std::string& s) {
    int depth = 0;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '(') {
            depth++;
        } else if (c == ')') {
            depth--;
        } else if (depth == 0 && (c == '+' || c == '-' || c == '=' || c == '<' || c == '>' || c == ',' ||
                                  c == ';' || (static_cast<unsigned char>(c) == 0xC2 && i + 1 < s.size() &&
                                               static_cast<unsigned char>(s[i + 1]) == 0xB1) ||
                                  (static_cast<unsigned char>(c) == 0xE2 && i + 2 < s.size() &&
                                   static_cast<unsigned char>(s[i + 1]) == 0x89))) {  // ±, ≤ ≥ ≠ ≔
            return "(" + s + ")";
        }
    }
    if (s.rfind("√", 0) == 0 && !isSimple(s.substr(std::string("√").size()))) {
        return s;
    }
    return s;
}

/// One letter: an ASCII letter or a single UTF-8 character
bool isSingleLetter(const std::string& s) {
    if (s.size() == 1) {
        return isAsciiAlpha(s[0]);
    }
    if (s.empty() || !(static_cast<unsigned char>(s[0]) & 0x80)) {
        return false;
    }
    size_t len = (static_cast<unsigned char>(s[0]) & 0xE0) == 0xC0 ? 2 : (static_cast<unsigned char>(s[0]) & 0xF0) == 0xE0 ? 3 : 4;
    return s.size() == len && s != "√" && s != "°" && s != "∞" && s != "∂";
}

const std::map<std::string, std::string>& greekMap() {
    static const std::map<std::string, std::string> m = {
            {"alpha", "α"},   {"beta", "β"},     {"gamma", "γ"},    {"delta", "δ"},  {"epsilon", "ε"},
            {"varepsilon", "ε"}, {"zeta", "ζ"},  {"eta", "η"},      {"theta", "θ"},  {"vartheta", "θ"},
            {"iota", "ι"},    {"kappa", "κ"},    {"lambda", "λ"},   {"mu", "μ"},     {"nu", "ν"},
            {"xi", "ξ"},      {"omicron", "ο"},  {"pi", "π"},       {"varpi", "π"},  {"rho", "ρ"},
            {"varrho", "ρ"},  {"sigma", "σ"},    {"varsigma", "ς"}, {"tau", "τ"},    {"upsilon", "υ"},
            {"phi", "φ"},     {"varphi", "φ"},   {"chi", "χ"},      {"psi", "ψ"},    {"omega", "ω"},
            {"Gamma", "Γ"},   {"Delta", "Δ"},    {"Theta", "Θ"},    {"Lambda", "Λ"}, {"Xi", "Ξ"},
            {"Pi", "Π"},      {"Sigma", "Σ"},    {"Upsilon", "Υ"},  {"Phi", "Φ"},    {"Psi", "Ψ"},
            {"Omega", "Ω"},   {"varDelta", "Δ"}, {"varOmega", "Ω"}, {"ell", "l"},    {"partial", "∂"},
            {"hbar", "ħ"},    {"infty", "∞"},    {"degree", "°"},   {"circ", "°"},   {"prime", "'"},
    };
    return m;
}

const std::map<std::string, std::string>& functionMap() {
    static const std::map<std::string, std::string> m = {
            {"sin", "sin"},     {"cos", "cos"},     {"tan", "tan"},     {"arcsin", "asin"}, {"arccos", "acos"},
            {"arctan", "atan"}, {"ln", "ln"},       {"log", "log"},     {"exp", "exp"},     {"min", "min"},
            {"max", "max"},     {"sinh", "sinh"},   {"cosh", "cosh"},   {"tanh", "tanh"},   {"cot", "cot"},
            {"sec", "sec"},     {"csc", "csc"},     {"lg", "log"},
    };
    return m;
}

/// Operators written as commands, with their kind
const std::map<std::string, std::pair<Atom::Kind, std::string>>& operatorMap() {
    using K = Atom::Kind;
    static const std::map<std::string, std::pair<Atom::Kind, std::string>> m = {
            {"cdot", {K::TightOp, "·"}}, {"times", {K::TightOp, "·"}},  {"ast", {K::TightOp, "·"}},
            {"bullet", {K::TightOp, "·"}}, {"div", {K::TightOp, "/"}},  {"pm", {K::Op, "±"}},
            {"mp", {K::Op, "±"}},        {"leq", {K::Op, "≤"}},       {"le", {K::Op, "≤"}},
            {"leqslant", {K::Op, "≤"}},  {"geq", {K::Op, "≥"}},       {"ge", {K::Op, "≥"}},
            {"geqslant", {K::Op, "≥"}},  {"neq", {K::Op, "≠"}},       {"ne", {K::Op, "≠"}},
            {"approx", {K::Op, "="}},    {"simeq", {K::Op, "="}},     {"equiv", {K::Op, "≔"}},
            {"coloneqq", {K::Op, "≔"}},  {"coloneq", {K::Op, "≔"}},   {"colonequals", {K::Op, "≔"}},
            {"Coloneqq", {K::Op, "≔"}},  {"lt", {K::Op, "<"}},        {"gt", {K::Op, ">"}},
            {"to", {K::Op, "→"}},        {"rightarrow", {K::Op, "→"}}, {"Rightarrow", {K::Op, "→"}},
    };
    return m;
}

/// Commands whose argument is shown as it is (fonts, text)
bool isTextCommand(const std::string& c) {
    static const char* const CMDS[] = {"mathrm", "text",   "textrm",   "mathit",      "mathbf",     "mathsf",
                                       "mathtt", "textit", "textbf",   "boldsymbol",  "bm",         "mathcal",
                                       "mbox",   "hbox",   "mathbb",   "operatorname", "mathnormal", "textnormal",
                                       "displaystyle", "unit"};
    for (const char* x: CMDS) {
        if (c == x) {
            return true;
        }
    }
    return false;
}

/// Commands that only change sizes or spacing, shown as nothing
bool isIgnoredCommand(const std::string& c) {
    static const char* const CMDS[] = {"left",   "right",   "big",    "Big",   "bigg",   "Bigg",   "bigl",
                                       "bigr",   "Bigl",    "Bigr",   "biggl", "biggr",  "quad",   "qquad",
                                       "displaystyle", "textstyle", "scriptstyle", "limits", "nolimits", "rm",
                                       "it",     "bf",      "hline",  "middle", "mathstrut", "strut"};
    for (const char* x: CMDS) {
        if (c == x) {
            return true;
        }
    }
    return false;
}

class Parser {
public:
    explicit Parser(std::string_view s): s(s) {}

    std::string run() {
        auto atoms = parseSequence(false);
        return render(atoms);
    }

private:
    std::string_view s;
    size_t pos = 0;

    void skipSpaces() {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) {
            pos++;
        }
    }

    std::string readCommandName() {
        // s[pos] == '\\'
        pos++;
        if (pos >= s.size()) {
            return "";
        }
        if (!isAsciiAlpha(s[pos])) {
            return std::string(1, s[pos++]);  // \, \; \{ \\ ...
        }
        size_t start = pos;
        while (pos < s.size() && isAsciiAlpha(s[pos])) {
            pos++;
        }
        return std::string(s.substr(start, pos - start));
    }

    /// The argument of a command or of ^ and _: a {group} or a single token
    std::vector<Atom> parseArgument() {
        skipSpaces();
        if (pos >= s.size()) {
            return {};
        }
        if (s[pos] == '{') {
            pos++;
            return parseSequence(true);
        }
        std::vector<Atom> out;
        parseOne(out);
        return out;
    }

    std::string argumentText() { return render(parseArgument()); }

    /// Optional [n] argument
    std::optional<std::string> parseOptional() {
        skipSpaces();
        if (pos < s.size() && s[pos] == '[') {
            pos++;
            std::vector<Atom> out;
            while (true) {
                skipSpaces();
                if (pos >= s.size()) {
                    break;
                }
                if (s[pos] == ']') {
                    pos++;
                    break;
                }
                parseOne(out);
            }
            return render(out);
        }
        return std::nullopt;
    }

    std::vector<Atom> parseSequence(bool untilBrace) {
        std::vector<Atom> out;
        while (true) {
            skipSpaces();
            if (pos >= s.size()) {
                break;
            }
            if (untilBrace && s[pos] == '}') {
                pos++;
                break;
            }
            if (!untilBrace && s[pos] == '}') {  // unbalanced
                pos++;
                continue;
            }
            parseOne(out);
        }
        return out;
    }

    static Atom operand(std::string t, bool closed = false) {
        Atom a;
        a.text = std::move(t);
        a.closed = closed;
        return a;
    }

    static Atom op(Atom::Kind k, std::string t) {
        Atom a;
        a.kind = k;
        a.text = std::move(t);
        return a;
    }

    Atom& lastOperandFor(std::vector<Atom>& out) {
        if (out.empty() || (out.back().kind != Atom::Kind::Operand && out.back().kind != Atom::Kind::Close)) {
            out.push_back(operand(""));
        }
        return out.back();
    }

    void attachPower(std::vector<Atom>& out, const std::string& e) {
        if (e.empty()) {
            return;
        }
        if (out.empty() || (out.back().kind != Atom::Kind::Operand && out.back().kind != Atom::Kind::Close)) {
            out.push_back(operand(""));
        }
        if (out.back().kind == Atom::Kind::Close) {
            // ( ... ) ^ 2: the power goes after the parenthesis
            out.back().text += e == "°" || e == "'" ? e : "^" + paren(e);
            return;
        }
        Atom& base = out.back();
        if (base.fraction) {
            base.text = "(" + base.text + ")";
            base.fraction = false;
        }
        if (e == "°" || e == "'" || e == "''") {
            base.text += e;
        } else {
            base.text += "^" + paren(e);
        }
        base.closed = true;
    }

    void attachSubscript(std::vector<Atom>& out, const std::string& sub) {
        std::string clean;
        for (char c: sub) {
            unsigned char u = static_cast<unsigned char>(c);
            if (std::isalnum(u) || u >= 0x80 || c == '_') {
                clean += c;
            } else if (c == ',' || c == '.') {
                clean += '_';
            }
        }
        if (clean.empty()) {
            return;
        }
        Atom& base = lastOperandFor(out);
        if (base.text.find('^') != std::string::npos) {
            // x^2_1 → x_1^2
            size_t p = base.text.find('^');
            base.text.insert(p, "_" + clean);
        } else {
            base.text += "_" + clean;
        }
        base.closed = true;
    }

    void parseOne(std::vector<Atom>& out) {
        char c = s[pos];
        if (c == '{') {
            pos++;
            auto inner = parseSequence(true);
            // A group is part of the surrounding expression (\mathrm{k N}, {x}^2)
            std::string t = render(inner);
            if (t.empty()) {
                return;
            }
            if (inner.size() == 1 && inner[0].kind == Atom::Kind::Operand) {
                out.push_back(inner[0]);
            } else {
                // Keep it together as one operand
                Atom a = operand(isSimple(t) ? t : "(" + t + ")", !isSimple(t));
                out.push_back(a);
            }
            return;
        }
        if (c == '^') {
            pos++;
            attachPower(out, argumentText());
            return;
        }
        if (c == '_') {
            pos++;
            attachSubscript(out, argumentText());
            return;
        }
        if (c == '\\') {
            parseCommand(out, readCommandName());
            return;
        }
        pos++;
        if (isDigit(c) || c == '.') {
            out.push_back(operand(std::string(1, c)));
            return;
        }
        if (isAsciiAlpha(c)) {
            out.push_back(operand(std::string(1, c)));
            return;
        }
        if (static_cast<unsigned char>(c) & 0x80) {
            // A UTF-8 character as it is (the recognizer may give "α" or "×")
            std::string u(1, c);
            while (pos < s.size() && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) {
                u += s[pos++];
            }
            if (u == "×" || u == "·" || u == "⋅") {
                out.push_back(op(Atom::Kind::TightOp, "·"));
            } else if (u == "÷") {
                out.push_back(op(Atom::Kind::TightOp, "/"));
            } else if (u == "−") {
                out.push_back(op(Atom::Kind::Op, "-"));
            } else if (u == "≤" || u == "≥" || u == "≠" || u == "±" || u == "≔") {
                out.push_back(op(Atom::Kind::Op, u));
            } else {
                out.push_back(operand(u));
            }
            return;
        }
        switch (c) {
            case '+':
            case '-':
            case '<':
            case '>':
                out.push_back(op(Atom::Kind::Op, std::string(1, c)));
                return;
            case '=':
                if (!out.empty() && out.back().kind == Atom::Kind::Op && out.back().text == ":") {
                    out.back().text = "≔";
                } else if (!out.empty() && out.back().kind == Atom::Kind::Op && out.back().text == "=") {
                    out.back().text = "≔";  // "==" as typed in the regions
                } else {
                    out.push_back(op(Atom::Kind::Op, "="));
                }
                return;
            case ':':
                out.push_back(op(Atom::Kind::Op, ":"));
                return;
            case '*':
                out.push_back(op(Atom::Kind::TightOp, "·"));
                return;
            case '/':
                out.push_back(op(Atom::Kind::TightOp, "/"));
                return;
            case '(':
            case '[':
                out.push_back(op(Atom::Kind::Open, "("));
                return;
            case ')':
            case ']':
                out.push_back(op(Atom::Kind::Close, ")"));
                return;
            case '|':
                out.push_back(op(Atom::Kind::Operand, "|"));
                return;
            case ',':
            case ';':
                out.push_back(op(Atom::Kind::Comma, ","));
                return;
            case '%':
                out.push_back(operand("%", true));
                return;
            case '\'':
                if (!out.empty() && out.back().kind == Atom::Kind::Operand) {
                    out.back().text += "'";
                }
                return;
            case '?':
                out.push_back(operand("?", true));
                return;
            case '&':
            case '~':
            case '!':
            default:
                return;
        }
    }

    void parseCommand(std::vector<Atom>& out, const std::string& cmd) {
        using K = Atom::Kind;
        if (cmd.empty() || cmd == "," || cmd == ";" || cmd == ":" || cmd == "!" || cmd == " " || cmd == ">" ||
            isIgnoredCommand(cmd)) {
            return;
        }
        if (cmd == "\\" || cmd == "newline" || cmd == "cr") {
            out.push_back(op(K::Newline, "\n"));
            return;
        }
        if (cmd == "{" || cmd == "lbrace" || cmd == "langle" || cmd == "lbrack" || cmd == "lfloor" ||
            cmd == "lceil") {
            out.push_back(op(K::Open, "("));
            return;
        }
        if (cmd == "}" || cmd == "rbrace" || cmd == "rangle" || cmd == "rbrack" || cmd == "rfloor" ||
            cmd == "rceil") {
            out.push_back(op(K::Close, ")"));
            return;
        }
        if (cmd == "%" || cmd == "percent") {
            out.push_back(operand("%", true));
            return;
        }
        if (cmd == "begin" || cmd == "end") {
            argumentText();  // environment name
            if (cmd == "begin") {
                skipSpaces();
                if (pos < s.size() && s[pos] == '{') {  // array column spec
                    size_t save = pos;
                    pos++;
                    std::string spec;
                    while (pos < s.size() && s[pos] != '}') {
                        spec += s[pos++];
                    }
                    bool isSpec = !spec.empty() && spec.find_first_not_of("lcr| ") == std::string::npos;
                    if (isSpec) {
                        pos++;
                    } else {
                        pos = save;
                    }
                }
            }
            return;
        }
        if (cmd == "frac" || cmd == "dfrac" || cmd == "tfrac" || cmd == "cfrac") {
            std::string num = argumentText();
            std::string den = argumentText();
            Atom a = operand(parenNumerator(num) + "/" + paren(den), true);
            a.fraction = true;
            out.push_back(a);
            return;
        }
        if (cmd == "sqrt") {
            auto n = parseOptional();
            std::string arg = argumentText();
            if (n && !n->empty()) {
                if (*n == "3") {
                    out.push_back(operand("cbrt(" + arg + ")", true));
                } else {
                    out.push_back(operand(paren(arg) + "^(1/" + paren(*n) + ")", true));
                }
            } else {
                out.push_back(operand(isSimple(arg) ? "√" + arg : "√(" + arg + ")", true));
            }
            return;
        }
        if (isTextCommand(cmd)) {
            std::string t = argumentText();
            if (cmd == "operatorname" && functionMap().count(t)) {
                Atom f = op(K::Func, functionMap().at(t));
                out.push_back(f);
                return;
            }
            if (!t.empty()) {
                // "\mathrm { k N }": one name
                out.push_back(operand(t, true));
            }
            return;
        }
        if (cmd == "overline" || cmd == "bar" || cmd == "hat" || cmd == "vec" || cmd == "tilde" || cmd == "dot" ||
            cmd == "underline" || cmd == "widehat" || cmd == "overrightarrow" || cmd == "acute") {
            std::string t = argumentText();
            if (!t.empty()) {
                out.push_back(operand(t));
            }
            return;
        }
        if (cmd.size() > 3 && cmd.compare(0, 3, "var") == 0 && greekMap().count(cmd.substr(3)) &&
            !greekMap().count(cmd)) {
            // \varGamma, \varDelta... are the italic capitals
            parseCommand(out, cmd.substr(3));
            return;
        }
        if (auto it = greekMap().find(cmd); it != greekMap().end()) {
            if (it->second == "°" || it->second == "'") {
                if (!out.empty() && out.back().kind == K::Operand) {
                    out.back().text += it->second;
                    out.back().closed = true;
                } else {
                    out.push_back(operand(it->second, true));
                }
            } else {
                out.push_back(operand(it->second));
            }
            return;
        }
        if (auto it = functionMap().find(cmd); it != functionMap().end()) {
            out.push_back(op(K::Func, it->second));
            return;
        }
        if (auto it = operatorMap().find(cmd); it != operatorMap().end()) {
            out.push_back(op(it->second.first, it->second.second));
            return;
        }
        // Unknown command: keep its name
        out.push_back(operand(cmd, true));
    }

    /// Letters written one after the other are a product ("m c^2"), unless together they spell a unit ("k N" → kN,
    /// "c m^2" → cm^2, "M P a" → MPa). Units are marked so that a following digit is glued as a power (m 3 → m3)
    static std::vector<Atom> mergeUnits(const std::vector<Atom>& in) {
        std::vector<Atom> out;
        for (size_t i = 0; i < in.size();) {
            auto letterAt = [&](size_t j, bool allowSuffix) -> std::string {
                const Atom& a = in[j];
                if (a.kind != Atom::Kind::Operand || a.fraction) {
                    return "";
                }
                if (!a.closed && isSingleLetter(a.text)) {
                    return a.text;
                }
                if (allowSuffix && a.text.size() >= 2 && isAsciiAlpha(a.text[0]) && a.text[1] == '^') {
                    return a.text.substr(0, 1);
                }
                return "";
            };
            // Longest run starting at i that is a unit
            size_t best = 0;
            std::string bestName;
            std::string word;
            for (size_t j = i; j < in.size(); j++) {
                std::string l = letterAt(j, true);
                if (l.empty()) {
                    break;
                }
                word += l;
                if (j > i && canonicalUnitName(word)) {
                    best = j - i + 1;
                    bestName = *canonicalUnitName(word);
                }
                if (in[j].closed) {
                    break;  // a power ends the unit
                }
            }
            if (best >= 2) {
                Atom m = in[i + best - 1];
                std::string suffix = m.text.size() > 1 && m.text[1] == '^' ? m.text.substr(1) : "";
                m.text = bestName + suffix;
                m.kind = Atom::Kind::Operand;
                m.unit = true;
                m.closed = !suffix.empty();
                out.push_back(m);
                i += best;
                continue;
            }
            out.push_back(in[i]);
            if (out.back().kind == Atom::Kind::Operand && !out.back().closed && canonicalUnitName(out.back().text)) {
                out.back().unit = true;
            }
            i++;
        }
        return out;
    }

    /// Join the atoms with the spacing used in the regions
    static std::string render(const std::vector<Atom>& input) {
        using K = Atom::Kind;
        const std::vector<Atom> atoms = mergeUnits(input);
        // Merge ":" with the next "=" into "≔", then glue digits and decimal commas
        std::string out;
        const Atom* prev = nullptr;
        bool prevGlue = false;  // the previous operand can be extended (it is a plain number or unit)
        std::vector<bool> funcParens;  // for each open parenthesis: it holds function arguments
        bool pendingFunc = false;

        auto needsFractionParens = [&](size_t i) {
            // "a/b" followed by a factor (or after "/") must be in parentheses
            if (i + 1 < atoms.size()) {
                const Atom& n = atoms[i + 1];
                if (n.kind == K::Operand || n.kind == K::Open || n.kind == K::Func) {
                    return true;
                }
            }
            return prev && prev->kind == K::TightOp && prev->text == "/";
        };

        for (size_t i = 0; i < atoms.size(); i++) {
            const Atom& a = atoms[i];
            switch (a.kind) {
                case K::Operand: {
                    std::string t = a.text;
                    if (t.empty()) {
                        break;
                    }
                    if (a.fraction && needsFractionParens(i)) {
                        t = "(" + t + ")";
                    }
                    if (pendingFunc) {
                        // "\sin x" → "sin(x)"
                        out += "(" + t + ")";
                        pendingFunc = false;
                        prev = &a;
                        prevGlue = false;
                        break;
                    }
                    if (prev && (prev->kind == K::Operand || prev->kind == K::Close)) {
                        char l = lastClass(out);
                        char f = firstClass(t);
                        bool glue = false;
                        if (prev->kind == K::Operand && prevGlue) {
                            if (l == 'd' && f == 'd') {
                                glue = true;  // 3 0 0 → 300
                            } else if (l == 'a' && f == 'd' && prev->unit && a.text.size() == 1) {
                                glue = true;  // m 3 → m3
                            }
                        }
                        if (!glue) {
                            out += ' ';
                        }
                    }
                    out += t;
                    prevGlue = !a.closed && !a.fraction;
                    prev = &a;
                    break;
                }
                case K::Op: {
                    if (a.text == ":") {
                        // a lone ":" (not followed by "="): ignore
                        break;
                    }
                    bool unary = (a.text == "-" || a.text == "+" || a.text == "±") &&
                                 (!prev || prev->kind == K::Op || prev->kind == K::TightOp || prev->kind == K::Open ||
                                  prev->kind == K::Comma || prev->kind == K::Newline);
                    if (unary) {
                        if (!out.empty() && out.back() != ' ' && out.back() != '(' && out.back() != '\n') {
                            out += ' ';
                        }
                        out += a.text;
                    } else {
                        while (!out.empty() && out.back() == ' ') {
                            out.pop_back();
                        }
                        out += " " + a.text + " ";
                    }
                    prev = &a;
                    prevGlue = false;
                    break;
                }
                case K::TightOp:
                    while (!out.empty() && out.back() == ' ') {
                        out.pop_back();
                    }
                    out += a.text;
                    prev = &a;
                    prevGlue = false;
                    break;
                case K::Func:
                    if (prev && (prev->kind == K::Operand || prev->kind == K::Close)) {
                        out += ' ';
                    }
                    out += a.text;
                    pendingFunc = true;
                    prev = &a;
                    prevGlue = false;
                    break;
                case K::Open:
                    if (pendingFunc) {
                        funcParens.push_back(true);
                        pendingFunc = false;
                    } else {
                        if (prev && (prev->kind == K::Operand || prev->kind == K::Close)) {
                            out += ' ';
                        }
                        funcParens.push_back(false);
                    }
                    out += "(";
                    prev = &a;
                    prevGlue = false;
                    break;
                case K::Close:
                    while (!out.empty() && out.back() == ' ') {
                        out.pop_back();
                    }
                    if (!funcParens.empty()) {
                        funcParens.pop_back();
                    }
                    out += a.text;
                    prev = &a;
                    prevGlue = false;
                    break;
                case K::Comma: {
                    bool inFunc = !funcParens.empty() && funcParens.back();
                    // 3,5 → 3.5 (decimal comma), except between function arguments
                    if (!inFunc && prev && prev->kind == K::Operand && lastClass(out) == 'd' && prevGlue &&
                        i + 1 < atoms.size() && atoms[i + 1].kind == K::Operand &&
                        firstClass(atoms[i + 1].text) == 'd') {
                        out += '.';
                        prev = &atoms[i + 1];
                        out += atoms[i + 1].text;
                        prevGlue = !atoms[i + 1].closed;
                        i++;
                        break;
                    }
                    while (!out.empty() && out.back() == ' ') {
                        out.pop_back();
                    }
                    out += inFunc ? ", " : "; ";
                    prev = &a;
                    prevGlue = false;
                    break;
                }
                case K::Newline:
                    while (!out.empty() && out.back() == ' ') {
                        out.pop_back();
                    }
                    out += "\n";
                    prev = &a;
                    prevGlue = false;
                    break;
            }
        }
        // Trim
        size_t b = out.find_first_not_of(' ');
        if (b == std::string::npos) {
            return "";
        }
        size_t e = out.find_last_not_of(' ');
        return out.substr(b, e - b + 1);
    }
};

/// Trim spaces at both ends of each line
std::string trimLines(const std::string& s) {
    std::string out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string line = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        size_t b = line.find_first_not_of(' ');
        size_t e = line.find_last_not_of(' ');
        if (b != std::string::npos) {
            if (!out.empty()) {
                out += '\n';
            }
            out += line.substr(b, e - b + 1);
        }
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    return out;
}

/// A plain name: letters, digits, '_' and non-ASCII letters, starting with a letter
bool isPlainName(const std::string& s) {
    if (s.empty() || isDigit(s.front())) {
        return false;
    }
    for (char c: s) {
        unsigned char u = static_cast<unsigned char>(c);
        if (!(std::isalnum(u) || c == '_' || c == '\'' || u >= 0x80)) {
            return false;
        }
    }
    return true;
}

/// "x = 3" → "x := 3" (but "x = " and "a + b = c" stay)
std::string toDefinition(const std::string& line) {
    const std::string eq = " = ";
    size_t p = line.find(eq);
    if (p == std::string::npos || line.find("≔") != std::string::npos) {
        return line;
    }
    std::string lhs = line.substr(0, p);
    std::string rest = line.substr(p + eq.size());
    if (!isPlainName(lhs) || rest.empty() || rest == "?" || rest.rfind("? ", 0) == 0) {
        return line;
    }
    return lhs + " := " + rest;
}

}  // namespace

std::string latexToCalcLiteral(std::string_view latex) {
    std::string s = trimLines(Parser(latex).run());
    // "≔" is shown as such in the regions, but ":=" is what users type
    std::string out;
    const std::string defSym = "≔";
    for (size_t i = 0; i < s.size();) {
        if (s.compare(i, defSym.size(), defSym) == 0) {
            out += ":=";
            i += defSym.size();
        } else {
            out += s[i++];
        }
    }
    // A trailing "=" on its own line part: "x =" (evaluation)
    return out;
}

std::string latexToCalc(std::string_view latex) {
    std::string s = latexToCalcLiteral(latex);
    std::string out;
    size_t start = 0;
    while (true) {
        size_t nl = s.find('\n', start);
        std::string line = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!out.empty()) {
            out += '\n';
        }
        // A trailing " =" asks for the result: "M = F·L =" → "M := F·L ="
        std::string tail;
        if (line.size() >= 2 && line.compare(line.size() - 2, 2, " =") == 0) {
            tail = " =";
            line = line.substr(0, line.size() - 2);
        }
        out += toDefinition(line) + tail;
        if (nl == std::string::npos) {
            break;
        }
        start = nl + 1;
    }
    return out;
}

}  // namespace xoj::calc
