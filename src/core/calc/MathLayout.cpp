#include "MathLayout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>

#include <glib.h>
#include <pango/pangocairo.h>

#include "util/raii/GObjectSPtr.h"

#include "CalcDocument.h"
#include "CalcEngine.h"

namespace xoj::calc::math {

namespace {

constexpr size_t NPOS = std::string::npos;

// Control characters of the structured markup
constexpr char FRAC_OPEN = '\x01';
constexpr char FRAC_SEP = '\x02';
constexpr char FRAC_CLOSE = '\x03';
constexpr char SUP_OPEN = '\x04';
constexpr char SUP_CLOSE = '\x05';
constexpr char ROOT_OPEN = '\x06';
constexpr char ROOT_CLOSE = '\x07';
constexpr char CBRT_OPEN = '\x0e';
constexpr char FENCE_OPEN = '\x0f';
constexpr char FENCE_CLOSE = '\x10';
constexpr char FENCE_SEP = '\x11';

bool isControl(char c) {
    return (c >= FRAC_OPEN && c <= ROOT_CLOSE) || c == CBRT_OPEN || c == FENCE_OPEN || c == FENCE_CLOSE ||
           c == FENCE_SEP;
}

enum class Tok { SPACE, STR, NUM, IDENT, OPEN, CLOSE, CARET, SLASH, LOWOP, MULOP, OTHER };

struct Token {
    Tok t;
    size_t a, b;  ///< byte range in the whole text
};

// Multi-byte characters that are operators
const char* const LOW_OPS[] = {"≔", "≤", "≥", "≠", "±", "−", "∓"};
const char* const MUL_OPS[] = {"·", "×", "⋅", "∙"};
// Multi-byte characters that end an identifier without being operators
const char* const OTHER_STOPS[] = {"÷", "√", "⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹", "⁻", "→", "✓", "✗"};

size_t startsWithAny(std::string_view s, size_t i, const char* const* list, size_t n) {
    for (size_t k = 0; k < n; k++) {
        size_t len = std::strlen(list[k]);
        if (s.substr(i, len) == list[k]) {
            return len;
        }
    }
    return 0;
}

#define LIST_LEN(l) (sizeof(l) / sizeof((l)[0]))

size_t utf8Len(std::string_view s, size_t i) {
    size_t len = 1;
    while (i + len < s.size() && (static_cast<unsigned char>(s[i + len]) & 0xC0) == 0x80) {
        len++;
    }
    return len;
}

bool identStart(std::string_view s, size_t i) {
    auto c = static_cast<unsigned char>(s[i]);
    if (std::isalpha(c) || c == '_') {
        return true;
    }
    if (c < 0x80) {
        return false;
    }
    return !startsWithAny(s, i, LOW_OPS, LIST_LEN(LOW_OPS)) && !startsWithAny(s, i, MUL_OPS, LIST_LEN(MUL_OPS)) &&
           !startsWithAny(s, i, OTHER_STOPS, LIST_LEN(OTHER_STOPS));
}

bool identByte(std::string_view s, size_t i) {
    auto c = static_cast<unsigned char>(s[i]);
    return std::isdigit(c) || c == '\'' || identStart(s, i);
}

/// Tokens of the line s, whose first byte is at base in the whole text
std::vector<Token> tokenize(std::string_view s, size_t base) {
    std::vector<Token> out;
    size_t i = 0;
    auto push = [&](Tok t, size_t j) {
        out.push_back({t, base + i, base + j});
        i = j;
    };
    while (i < s.size()) {
        const char c = s[i];
        if (c == ' ' || c == '\t') {
            size_t j = i;
            while (j < s.size() && (s[j] == ' ' || s[j] == '\t')) {
                j++;
            }
            push(Tok::SPACE, j);
        } else if (c == '"') {
            size_t j = s.find('"', i + 1);
            push(Tok::STR, j == std::string_view::npos ? s.size() : j + 1);
        } else if (std::isdigit(static_cast<unsigned char>(c)) ||
                   (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            size_t j = i;
            while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.')) {
                j++;
            }
            if (j + 1 < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                size_t k = j + 1;
                if (k < s.size() && (s[k] == '+' || s[k] == '-')) {
                    k++;
                }
                if (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) {
                    while (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) {
                        k++;
                    }
                    j = k;
                }
            }
            push(Tok::NUM, j);
        } else if (identStart(s, i)) {
            size_t j = i;
            while (j < s.size() && identByte(s, j)) {
                j += utf8Len(s, j);
            }
            // REDONDEAR.MENOS / REDONDEAR.MAS
            if (j + 1 < s.size() && s[j] == '.' && identStart(s, j + 1)) {
                size_t k = j + 1;
                while (k < s.size() && identByte(s, k)) {
                    k += utf8Len(s, k);
                }
                if (auto id = functionId(s.substr(i, k - i)); id && (*id == "rounddown" || *id == "roundup")) {
                    j = k;
                }
            }
            push(Tok::IDENT, j);
        } else if (c == '(' || c == '[') {
            push(Tok::OPEN, i + 1);
        } else if (c == ')' || c == ']') {
            push(Tok::CLOSE, i + 1);
        } else if (c == '^') {
            push(Tok::CARET, i + 1);
        } else if (c == '/') {
            push(Tok::SLASH, i + 1);
        } else if (c == ':' && i + 1 < s.size() && s[i + 1] == '=') {
            push(Tok::LOWOP, i + 2);
        } else if (c == '=' || c == '+' || c == '-' || c == '<' || c == '>' || c == ',' || c == ';') {
            push(Tok::LOWOP, i + 1);
        } else if (c == '*') {
            push(Tok::MULOP, i + 1);
        } else if (size_t l = startsWithAny(s, i, LOW_OPS, LIST_LEN(LOW_OPS))) {
            push(Tok::LOWOP, i + l);
        } else if (size_t m = startsWithAny(s, i, MUL_OPS, LIST_LEN(MUL_OPS))) {
            push(Tok::MULOP, i + m);
        } else {
            push(Tok::OTHER, i + utf8Len(s, i));
        }
    }
    return out;
}

bool isUnitName(std::string_view name) { return findUnit(name).has_value() || splitUnitPower(name).has_value(); }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The tree of a formula

struct Node {
    enum class Kind { ROW, TEXT, FRAC, SUP, ROOT, FENCE };
    Kind kind = Kind::ROW;

    // TEXT
    std::string text;  ///< display mode: markup
    std::string fname;  ///< edit mode: a function name (SENO) shown as it is displayed (sen)
    Tok tok = Tok::OTHER;

    /// TEXT: source range. ROW: where its content starts and ends. Structures: the whole structure (b: after its last
    /// character)
    size_t a = NPOS, b = NPOS;

    std::vector<std::unique_ptr<Node>> kids;  ///< ROW: items; structures: their boxes (ROW nodes)

    // Structures
    size_t slash = NPOS;  ///< FRAC: position of "/"
    size_t denOuterStart = NPOS;
    size_t nameStart = NPOS;    ///< FENCE of a function call: where the name of the function starts
    std::string open, close;    ///< FENCE: delimiters (markup in display mode)
    bool hiddenDelims = false;  ///< FENCE: the parentheses in the text are hidden (|x| for ABS(x))
    bool visibleOpen = false, visibleClose = false;  ///< FENCE: the delimiters are characters of the text
    size_t openPos = NPOS, closePos = NPOS;
    bool cube = false;    ///< ROOT: ∛
    bool isSlot = false;  ///< ROW: a box of a structure

    // Layout (relative to the parent: dx, dy of the origin = left end of the baseline)
    double scale = 1;
    double w = 0, asc = 0, desc = 0;
    double dx = 0, dy = 0;
    double x = 0, base = 0;  ///< absolute
    double axis = 0, thick = 0, pad = 0, rw = 0;
    xoj::util::GObjectSPtr<PangoLayout> layout, openLayout, closeLayout;
    std::vector<int> map;  ///< TEXT (edit): layout byte index of each source byte of [a, b]
    bool tall = false;
};

namespace {

using NodePtr = std::unique_ptr<Node>;

NodePtr makeNode(Node::Kind k) {
    auto n = std::make_unique<Node>();
    n->kind = k;
    return n;
}

bool hasStructureNode(const Node& n) {
    switch (n.kind) {
        case Node::Kind::FRAC:
        case Node::Kind::SUP:
        case Node::Kind::ROOT:
            return true;
        case Node::Kind::TEXT:
            return false;
        default:
            for (const auto& k: n.kids) {
                if (hasStructureNode(*k)) {
                    return true;
                }
            }
            return false;
    }
}

/// A FENCE is drawn specially (and not with the markup of its text) if it contains a structure, or for ABS
bool isPlain(const Node& n) {
    if (n.kind == Node::Kind::TEXT) {
        return true;
    }
    if (n.kind == Node::Kind::FENCE) {
        return !hasStructureNode(n);
    }
    return false;
}

std::string functionNameMarkup(std::string_view name);

class Parser {
public:
    Parser(std::string_view text, size_t base, size_t lineEnd):
            src(text), toks(tokenize(text.substr(base, lineEnd - base), base)), lineEnd(lineEnd) {}

    NodePtr parseLine() {
        auto row = parseRow(false, lineStartPos());
        finalize(*row);
        return row;
    }

private:
    size_t lineStartPos() const { return toks.empty() ? lineEnd : toks.front().a; }
    size_t pos() const { return i < toks.size() ? toks[i].a : lineEnd; }

    NodePtr text(const Token& t) {
        auto n = makeNode(Node::Kind::TEXT);
        n->a = t.a;
        n->b = t.b;
        n->tok = t.t;
        return n;
    }

    bool unitDivision(const std::vector<NodePtr>& items, size_t termStart) {
        bool sawUnit = false;
        for (size_t j = items.size(); j > termStart; j--) {
            const Node& n = *items[j - 1];
            if (n.kind == Node::Kind::SUP) {
                continue;
            }
            if (n.kind != Node::Kind::TEXT) {
                return false;
            }
            if (n.tok == Tok::SPACE || n.tok == Tok::MULOP) {
                continue;
            }
            if (n.tok == Tok::IDENT && isUnitName(src.substr(n.a, n.b - n.a))) {
                sawUnit = true;
                continue;
            }
            return n.tok == Tok::NUM && sawUnit;
        }
        return false;
    }

    /// The single "( )" item of a box, whose parentheses are then hidden: nullptr if there is none
    static Node* singleParenthesis(std::vector<NodePtr>& items) {
        Node* found = nullptr;
        for (auto& n: items) {
            if (n->kind == Node::Kind::TEXT && n->tok == Tok::SPACE) {
                continue;
            }
            if (found || n->kind != Node::Kind::FENCE || n->nameStart != NPOS || n->hiddenDelims || n->open != "(") {
                return nullptr;
            }
            found = n.get();
        }
        return found;
    }

    /// The box made of items: if they are one parenthesis, its content (the parentheses are hidden)
    NodePtr box(std::vector<NodePtr> items, size_t start, size_t end, size_t& outerStart, size_t& outerEnd) {
        if (Node* f = singleParenthesis(items)) {
            outerStart = f->a;
            outerEnd = f->b;
            NodePtr content = std::move(f->kids[0]);
            content->a = f->openPos + 1;
            content->b = f->visibleClose ? f->closePos : lineEnd;
            content->isSlot = true;
            return content;
        }
        auto row = makeNode(Node::Kind::ROW);
        row->a = items.empty() ? start : items.front()->a;
        row->b = items.empty() ? start : (items.back()->b == NPOS ? end : items.back()->b);
        row->kids = std::move(items);
        row->isSlot = true;
        outerStart = row->a;
        outerEnd = row->b;
        return row;
    }

    NodePtr parseRow(bool inGroup, size_t start) {
        std::vector<NodePtr> items;
        size_t termStart = 0;
        while (i < toks.size()) {
            const Token t = toks[i];
            if (t.t == Tok::CLOSE) {
                if (inGroup) {
                    break;
                }
                items.push_back(text(t));
                i++;
                continue;
            }
            if (t.t == Tok::LOWOP) {
                items.push_back(text(t));
                i++;
                while (i < toks.size() && toks[i].t == Tok::SPACE) {
                    items.push_back(text(toks[i++]));
                }
                termStart = items.size();
                continue;
            }
            if (t.t == Tok::SPACE) {
                items.push_back(text(t));
                i++;
                if (items.size() == termStart + 1) {
                    termStart++;
                }
                continue;
            }
            if (t.t == Tok::SLASH) {
                if (unitDivision(items, termStart)) {
                    items.push_back(text(t));
                    i++;
                    continue;
                }
                i++;
                std::vector<NodePtr> num;
                for (size_t k = termStart; k < items.size(); k++) {
                    num.push_back(std::move(items[k]));
                }
                items.resize(termStart);
                auto frac = makeNode(Node::Kind::FRAC);
                frac->slash = t.a;
                size_t numOuterStart = 0, numOuterEnd = 0, denOuterStart = 0, denOuterEnd = 0;
                frac->kids.push_back(box(std::move(num), t.a, t.a, numOuterStart, numOuterEnd));
                std::vector<NodePtr> den = parseFactor();
                frac->kids.push_back(box(std::move(den), t.b, t.b, denOuterStart, denOuterEnd));
                frac->a = numOuterStart;
                frac->b = std::max(denOuterEnd, t.b);
                frac->denOuterStart = denOuterStart;
                items.push_back(std::move(frac));
                continue;
            }
            if (t.t == Tok::CARET) {
                items.push_back(parseSup());
                continue;
            }
            parsePrimary(items);
        }
        auto row = makeNode(Node::Kind::ROW);
        row->a = items.empty() ? start : items.front()->a;
        row->b = items.empty() ? start : items.back()->b;
        row->kids = std::move(items);
        return row;
    }

    std::vector<NodePtr> parseFactor() {
        std::vector<NodePtr> items;
        while (i < toks.size() && toks[i].t == Tok::SPACE) {
            items.push_back(text(toks[i++]));
        }
        if (i < toks.size() && toks[i].t == Tok::LOWOP) {
            auto op = src.substr(toks[i].a, toks[i].b - toks[i].a);
            if (op == "-" || op == "+" || op == "−") {
                items.push_back(text(toks[i++]));
            }
        }
        if (i < toks.size()) {
            Tok t = toks[i].t;
            if (t == Tok::NUM || t == Tok::IDENT || t == Tok::OPEN || t == Tok::STR || t == Tok::OTHER) {
                parsePrimary(items);
            }
        }
        while (i < toks.size() && toks[i].t == Tok::CARET) {
            items.push_back(parseSup());
        }
        return items;
    }

    void parsePrimary(std::vector<NodePtr>& items) {
        const Token t = toks[i];
        if (t.t == Tok::OPEN) {
            items.push_back(parseFence());
            return;
        }
        if (t.t == Tok::IDENT && i + 1 < toks.size() && toks[i + 1].t == Tok::OPEN && toks[i + 1].a == t.b &&
            src[toks[i + 1].a] == '(') {
            auto id = functionId(src.substr(t.a, t.b - t.a));
            if (id && (*id == "sqrt" || *id == "cbrt")) {
                i++;
                auto f = parseFence();
                auto root = makeNode(Node::Kind::ROOT);
                root->cube = *id == "cbrt";
                root->a = t.a;
                root->b = f->b;
                root->openPos = f->openPos;
                root->closePos = f->closePos;
                NodePtr content = std::move(f->kids[0]);
                content->a = f->openPos + 1;
                content->b = f->visibleClose ? f->closePos : lineEnd;
                content->isSlot = true;
                root->kids.push_back(std::move(content));
                items.push_back(std::move(root));
                return;
            }
            if (id && *id == "abs") {
                i++;
                auto f = parseFence();
                f->a = t.a;
                f->hiddenDelims = true;
                f->visibleOpen = f->visibleClose = false;
                f->open = f->close = "|";
                f->kids[0]->isSlot = true;
                items.push_back(std::move(f));
                return;
            }
            items.push_back(text(t));
            if (id) {
                std::string shown = functionNameMarkup(src.substr(t.a, t.b - t.a));
                if (shown.find_first_of("<&") == std::string::npos) {
                    items.back()->fname = shown;
                }
            }
            i++;
            auto f = parseFence();
            f->nameStart = t.a;
            items.push_back(std::move(f));
            return;
        }
        items.push_back(text(t));
        i++;
    }

    NodePtr parseFence() {
        const Token o = toks[i++];
        auto f = makeNode(Node::Kind::FENCE);
        f->a = o.a;
        f->openPos = o.a;
        f->open = std::string(1, src[o.a]);
        f->visibleOpen = true;
        auto content = parseRow(true, o.b);
        if (i < toks.size() && toks[i].t == Tok::CLOSE) {
            f->closePos = toks[i].a;
            f->close = std::string(1, src[toks[i].a]);
            f->visibleClose = true;
            f->b = toks[i].b;
            i++;
        } else {
            f->closePos = lineEnd;
            f->b = lineEnd;
        }
        content->a = o.b;
        content->b = f->closePos;
        f->kids.push_back(std::move(content));
        return f;
    }

    NodePtr parseSup() {
        const Token c = toks[i++];
        auto sup = makeNode(Node::Kind::SUP);
        sup->a = c.a;
        if (i < toks.size() && toks[i].t == Tok::OPEN && toks[i].a == c.b && src[toks[i].a] == '(') {
            auto f = parseFence();
            sup->b = f->b;
            sup->openPos = f->openPos;
            sup->closePos = f->closePos;
            NodePtr content = std::move(f->kids[0]);
            content->a = f->openPos + 1;
            content->b = f->visibleClose ? f->closePos : lineEnd;
            content->isSlot = true;
            sup->kids.push_back(std::move(content));
            return sup;
        }
        std::vector<NodePtr> items;
        if (i < toks.size() && toks[i].t == Tok::LOWOP && toks[i].a == c.b) {
            auto op = src.substr(toks[i].a, toks[i].b - toks[i].a);
            if (op == "-" || op == "+" || op == "−") {
                items.push_back(text(toks[i++]));
            }
        }
        if (i < toks.size() && (toks[i].t == Tok::NUM || toks[i].t == Tok::IDENT) &&
            toks[i].a == (items.empty() ? c.b : items.back()->b)) {
            items.push_back(text(toks[i++]));
        }
        auto row = makeNode(Node::Kind::ROW);
        row->a = c.b;
        row->b = items.empty() ? c.b : items.back()->b;
        row->kids = std::move(items);
        row->isSlot = true;
        sup->b = row->b;
        sup->kids.push_back(std::move(row));
        return sup;
    }

    /// Merge the consecutive pieces of text
    static void finalize(Node& n) {
        for (auto& k: n.kids) {
            finalize(*k);
        }
        if (n.kind != Node::Kind::ROW) {
            return;
        }
        std::vector<NodePtr> merged;
        for (auto& k: n.kids) {
            if (k->kind == Node::Kind::TEXT && !merged.empty() && merged.back()->kind == Node::Kind::TEXT &&
                merged.back()->b == k->a && k->fname.empty() && merged.back()->fname.empty()) {
                merged.back()->b = k->b;
                continue;
            }
            merged.push_back(std::move(k));
        }
        n.kids = std::move(merged);
    }

    std::string_view src;
    std::vector<Token> toks;
    size_t lineEnd;
    size_t i = 0;
};

// ---------------------------------------------------------------------------------------------------------------------
// Structured markup

std::string functionNameMarkup(std::string_view name) {
    std::string m = prettifyMarkup(std::string(name) + "(x)", nullptr);
    if (m.size() >= 3 && m.compare(m.size() - 3, 3, "(x)") == 0) {
        return m.substr(0, m.size() - 3);
    }
    gchar* e = g_markup_escape_text(name.data(), static_cast<gssize>(name.size()));
    std::string r(e);
    g_free(e);
    return r;
}

std::string escapeMarkup(std::string_view s) {
    gchar* e = g_markup_escape_text(s.data(), static_cast<gssize>(s.size()));
    std::string r(e);
    g_free(e);
    return r;
}

void emitRow(const Node& row, std::string_view src, const Environment* env, std::string& out) {
    size_t plainA = NPOS, plainB = NPOS;
    bool emitted = false;
    auto flush = [&]() {
        if (plainA == NPOS || plainB <= plainA) {
            plainA = NPOS;
            return;
        }
        std::string_view piece = src.substr(plainA, plainB - plainA);
        std::string m = prettifyMarkup(piece, env);
        if (emitted && !piece.empty() && (piece.front() == ' ' || piece.front() == '\t')) {
            m = " " + m;
        }
        out += m;
        emitted = true;
        plainA = NPOS;
    };
    for (const auto& k: row.kids) {
        if (isPlain(*k)) {
            if (plainA == NPOS) {
                plainA = k->a;
            }
            plainB = k->b;
            continue;
        }
        std::string name;
        if (k->kind == Node::Kind::FENCE && k->nameStart != NPOS && plainA != NPOS) {
            name = functionNameMarkup(src.substr(k->nameStart, k->openPos - k->nameStart));
            plainB = k->nameStart;
        }
        flush();
        out += name;
        emitted = true;
        switch (k->kind) {
            case Node::Kind::FRAC:
                out += FRAC_OPEN;
                emitRow(*k->kids[0], src, env, out);
                out += FRAC_SEP;
                emitRow(*k->kids[1], src, env, out);
                out += FRAC_CLOSE;
                break;
            case Node::Kind::SUP:
                out += SUP_OPEN;
                emitRow(*k->kids[0], src, env, out);
                out += SUP_CLOSE;
                break;
            case Node::Kind::ROOT:
                out += k->cube ? CBRT_OPEN : ROOT_OPEN;
                emitRow(*k->kids[0], src, env, out);
                out += ROOT_CLOSE;
                break;
            case Node::Kind::FENCE:
                out += FENCE_OPEN;
                out += escapeMarkup(k->open);
                out += FENCE_SEP;
                emitRow(*k->kids[0], src, env, out);
                out += FENCE_SEP;
                out += escapeMarkup(k->hiddenDelims || k->visibleClose ? k->close : "");
                out += FENCE_CLOSE;
                break;
            default:
                break;
        }
    }
    flush();
}

/// Parse a structured markup row, until one of the stop characters (or the end)
NodePtr parseSmu(std::string_view s, size_t& i, const char* stops) {
    auto row = makeNode(Node::Kind::ROW);
    auto isStop = [&](char c) { return std::strchr(stops, c) != nullptr && c != '\0'; };
    std::string pending;
    auto flush = [&]() {
        if (!pending.empty()) {
            auto t = makeNode(Node::Kind::TEXT);
            t->text = std::move(pending);
            pending.clear();
            row->kids.push_back(std::move(t));
        }
    };
    auto expect = [&](char c) {
        if (i < s.size() && s[i] == c) {
            i++;
        }
    };
    while (i < s.size() && !isStop(s[i])) {
        const char c = s[i];
        if (!isControl(c)) {
            pending += c;
            i++;
            continue;
        }
        flush();
        i++;
        if (c == FRAC_OPEN) {
            auto f = makeNode(Node::Kind::FRAC);
            f->kids.push_back(parseSmu(s, i, "\x02"));
            expect(FRAC_SEP);
            f->kids.push_back(parseSmu(s, i, "\x03"));
            expect(FRAC_CLOSE);
            f->kids[0]->isSlot = f->kids[1]->isSlot = true;
            row->kids.push_back(std::move(f));
        } else if (c == SUP_OPEN) {
            auto f = makeNode(Node::Kind::SUP);
            f->kids.push_back(parseSmu(s, i, "\x05"));
            expect(SUP_CLOSE);
            row->kids.push_back(std::move(f));
        } else if (c == ROOT_OPEN || c == CBRT_OPEN) {
            auto f = makeNode(Node::Kind::ROOT);
            f->cube = c == CBRT_OPEN;
            f->kids.push_back(parseSmu(s, i, "\x07"));
            expect(ROOT_CLOSE);
            row->kids.push_back(std::move(f));
        } else if (c == FENCE_OPEN) {
            auto f = makeNode(Node::Kind::FENCE);
            size_t j = s.find(FENCE_SEP, i);
            j = j == std::string_view::npos ? s.size() : j;
            f->open = std::string(s.substr(i, j - i));
            i = j;
            expect(FENCE_SEP);
            f->kids.push_back(parseSmu(s, i, "\x11"));
            expect(FENCE_SEP);
            j = s.find(FENCE_CLOSE, i);
            j = j == std::string_view::npos ? s.size() : j;
            f->close = std::string(s.substr(i, j - i));
            i = j;
            expect(FENCE_CLOSE);
            row->kids.push_back(std::move(f));
        }
        // a stray separator is skipped
    }
    flush();
    return row;
}

// ---------------------------------------------------------------------------------------------------------------------
// Layout

struct LayoutContext {
    xoj::util::GObjectSPtr<PangoContext> pango;
    std::string fontName;
    double fontSize = 12;
    double asc0 = 9, desc0 = 3;  ///< metrics of the font at scale 1
    bool editing = false;
    std::string_view src;

    xoj::util::GObjectSPtr<PangoLayout> newLayout(double scale) const {
        xoj::util::GObjectSPtr<PangoLayout> l(pango_layout_new(pango.get()), xoj::util::adopt);
        PangoFontDescription* desc = pango_font_description_from_string(fontName.c_str());
        pango_font_description_set_absolute_size(desc, std::max(1.0, fontSize * scale) * PANGO_SCALE);
        pango_layout_set_font_description(l.get(), desc);
        pango_font_description_free(desc);
        return l;
    }
};

void measure(PangoLayout* l, double& w, double& asc, double& desc) {
    PangoRectangle logical;
    pango_layout_get_extents(l, nullptr, &logical);
    w = static_cast<double>(logical.x + logical.width) / PANGO_SCALE;
    asc = static_cast<double>(pango_layout_get_baseline(l)) / PANGO_SCALE;
    desc = static_cast<double>(logical.y + logical.height) / PANGO_SCALE - asc;
}

void layoutNode(Node& n, const LayoutContext& ctx, double scale, int fracDepth);

void layoutText(Node& n, const LayoutContext& ctx, double scale) {
    n.layout = ctx.newLayout(scale);
    if (!ctx.editing) {
        pango_layout_set_markup(n.layout.get(), n.text.c_str(), static_cast<int>(n.text.size()));
    } else {
        std::string_view s = ctx.src.substr(n.a, n.b - n.a);
        std::string out;
        n.map.assign(s.size() + 1, 0);
        if (!n.fname.empty()) {
            // A function: its name as it will be displayed, the cursor goes over it at once
            n.map[s.size()] = static_cast<int>(n.fname.size());
            pango_layout_set_text(n.layout.get(), n.fname.c_str(), static_cast<int>(n.fname.size()));
            measure(n.layout.get(), n.w, n.asc, n.desc);
            n.asc = std::max(n.asc, ctx.asc0 * scale);
            n.desc = std::max(n.desc, ctx.desc0 * scale);
            return;
        }
        for (size_t k = 0; k < s.size(); k++) {
            n.map[k] = static_cast<int>(out.size());
            if (s[k] == '*') {
                out += "·";
            } else {
                out += s[k];
            }
        }
        n.map[s.size()] = static_cast<int>(out.size());
        pango_layout_set_text(n.layout.get(), out.c_str(), static_cast<int>(out.size()));
        // A_s: the "_" is (almost) hidden and what follows it is a subscript
        PangoAttrList* attrs = pango_attr_list_new();
        const double em = ctx.fontSize * scale;
        for (size_t k = 1; k + 1 < s.size(); k++) {
            if (s[k] != '_' || !identByte(s, k - 1) || !identByte(s, k + 1)) {
                continue;
            }
            size_t j = k + 1;
            while (j < s.size() && identByte(s, j) && s[j] != '_') {
                j += utf8Len(s, j);
            }
            PangoAttribute* hide = pango_attr_scale_new(0.05);
            hide->start_index = static_cast<guint>(n.map[k]);
            hide->end_index = static_cast<guint>(n.map[k + 1]);
            pango_attr_list_insert(attrs, hide);
            PangoAttribute* small = pango_attr_scale_new(0.7);
            small->start_index = static_cast<guint>(n.map[k + 1]);
            small->end_index = static_cast<guint>(n.map[j]);
            pango_attr_list_insert(attrs, small);
            PangoAttribute* rise = pango_attr_rise_new(static_cast<int>(-0.22 * em * PANGO_SCALE));
            rise->start_index = static_cast<guint>(n.map[k + 1]);
            rise->end_index = static_cast<guint>(n.map[j]);
            pango_attr_list_insert(attrs, rise);
            k = j - 1;
        }
        pango_layout_set_attributes(n.layout.get(), attrs);
        pango_attr_list_unref(attrs);
    }
    measure(n.layout.get(), n.w, n.asc, n.desc);
    // The height of a line does not depend on its letters (the metrics of the font)
    n.asc = std::max(n.asc, ctx.asc0 * scale);
    n.desc = std::max(n.desc, ctx.desc0 * scale);
}

void layoutRow(Node& row, const LayoutContext& ctx, double scale, int fracDepth) {
    row.scale = scale;
    const double em = ctx.fontSize * scale;
    double x = 0;
    row.asc = 0;
    row.desc = 0;
    double prevAsc = ctx.asc0 * scale;
    for (auto& k: row.kids) {
        layoutNode(*k, ctx, scale, fracDepth);
        if (k->kind == Node::Kind::SUP) {
            const Node& c = *k->kids[0];
            const double shift = std::max(0.42 * em, prevAsc - 0.55 * em);
            k->kids[0]->dy = -shift;
            k->asc = shift + c.asc;
            k->desc = std::max(0.0, c.desc - shift);
        }
        k->dx = x;
        k->dy = 0;
        x += k->w;
        row.asc = std::max(row.asc, k->asc);
        row.desc = std::max(row.desc, k->desc);
        prevAsc = k->asc;
    }
    row.w = x;
    if (row.kids.empty()) {
        if (row.isSlot && ctx.editing) {
            row.w = 0.6 * em;
            row.asc = 0.75 * em;
            row.desc = 0.08 * em;
        } else {
            row.w = row.isSlot ? 0.3 * em : 0;
            row.asc = ctx.asc0 * scale;
            row.desc = ctx.desc0 * scale;
        }
    } else {
        row.asc = std::max(row.asc, 0.5 * em);
    }
}

void layoutNode(Node& n, const LayoutContext& ctx, double scale, int fracDepth) {
    const double em = ctx.fontSize * scale;
    n.scale = scale;
    switch (n.kind) {
        case Node::Kind::ROW:
            layoutRow(n, ctx, scale, fracDepth);
            break;
        case Node::Kind::TEXT:
            layoutText(n, ctx, scale);
            break;
        case Node::Kind::FRAC: {
            const double cs = fracDepth > 0 ? std::max(0.6, scale * 0.85) : scale;
            Node& num = *n.kids[0];
            Node& den = *n.kids[1];
            layoutRow(num, ctx, cs, fracDepth + 1);
            layoutRow(den, ctx, cs, fracDepth + 1);
            n.axis = 0.28 * em;
            n.thick = std::max(0.05 * em, 0.4);
            const double gap = 0.12 * em;
            n.pad = 0.15 * em;
            n.w = std::max(num.w, den.w) + 2 * n.pad;
            num.dx = (n.w - num.w) / 2;
            den.dx = (n.w - den.w) / 2;
            num.dy = -(n.axis + n.thick / 2 + gap + num.desc);
            den.dy = -n.axis + n.thick / 2 + gap + den.asc;
            n.asc = -num.dy + num.asc;
            n.desc = std::max(0.0, den.dy + den.desc);
            break;
        }
        case Node::Kind::SUP: {
            Node& c = *n.kids[0];
            layoutRow(c, ctx, std::max(0.5, scale * 0.7), fracDepth);
            c.dx = 0.03 * em;
            n.w = c.w + 0.08 * em;
            n.asc = c.asc;  // completed by the row (depends on what is before)
            n.desc = c.desc;
            break;
        }
        case Node::Kind::ROOT: {
            Node& c = *n.kids[0];
            layoutRow(c, ctx, scale, fracDepth);
            n.thick = std::max(0.05 * em, 0.4);
            const double gap = 0.12 * em;
            n.rw = (n.cube ? 0.72 : 0.6) * em;
            c.dx = n.rw + 0.04 * em;
            c.dy = 0;
            n.w = c.dx + c.w + 0.1 * em;
            n.asc = c.asc + gap + n.thick + 0.04 * em;
            n.desc = c.desc + 0.08 * em;
            if (n.cube) {
                n.openLayout = ctx.newLayout(scale * 0.5);
                pango_layout_set_text(n.openLayout.get(), "3", 1);
            }
            break;
        }
        case Node::Kind::FENCE: {
            Node& c = *n.kids[0];
            layoutRow(c, ctx, scale, fracDepth);
            double ow = 0, oa = 0, od = 0, cw = 0, ca = 0, cd = 0;
            n.openLayout = ctx.newLayout(scale);
            n.closeLayout = ctx.newLayout(scale);
            const std::string& op = n.open;
            const std::string cl = ctx.editing && !n.hiddenDelims && !n.visibleClose ? "" : n.close;
            if (ctx.editing) {
                pango_layout_set_text(n.openLayout.get(), op.c_str(), -1);
                pango_layout_set_text(n.closeLayout.get(), cl.c_str(), -1);
            } else {
                pango_layout_set_markup(n.openLayout.get(), op.c_str(), -1);
                pango_layout_set_markup(n.closeLayout.get(), cl.c_str(), -1);
            }
            measure(n.openLayout.get(), ow, oa, od);
            measure(n.closeLayout.get(), cw, ca, cd);
            if (cl.empty()) {
                cw = 0;
            }
            n.tall = c.asc > 1.05 * ctx.asc0 * scale || c.desc > 1.3 * ctx.desc0 * scale;
            c.dx = ow;
            c.dy = 0;
            n.w = ow + c.w + cw;
            n.asc = std::max(c.asc + (n.tall ? 0.06 * em : 0), oa);
            n.desc = std::max(c.desc + (n.tall ? 0.06 * em : 0), od);
            break;
        }
    }
}

void place(Node& n, double x, double base) {
    n.x = x;
    n.base = base;
    if (n.kind == Node::Kind::ROW) {
        for (auto& k: n.kids) {
            place(*k, x + k->dx, base + k->dy);
        }
    } else {
        for (auto& k: n.kids) {
            place(*k, x + k->dx, base + k->dy);
        }
    }
}

double layoutX(PangoLayout* l, int index) {
    PangoRectangle r;
    pango_layout_index_to_pos(l, index, &r);
    return static_cast<double>(r.x) / PANGO_SCALE;
}

void showLayout(cairo_t* cr, PangoLayout* l, double x, double base) {
    cairo_move_to(cr, x, base - static_cast<double>(pango_layout_get_baseline(l)) / PANGO_SCALE);
    pango_cairo_show_layout(cr, l);
}

struct DrawState {
    cairo_t* cr;
    bool editing;
    size_t cursor, selStart, selEnd;
    Color selColor;
    double em;
};

void fillRect(const DrawState& st, double x, double y, double w, double h, Color c) {
    cairo_save(st.cr);
    cairo_set_operator(st.cr, CAIRO_OPERATOR_OVER);
    Util::cairo_set_source_argb(st.cr, c);
    cairo_rectangle(st.cr, x, y, w, h);
    cairo_fill(st.cr);
    cairo_restore(st.cr);
}

void drawNode(const Node& n, const DrawState& st) {
    cairo_t* cr = st.cr;
    const bool selected = st.editing && st.selStart < st.selEnd && n.kind != Node::Kind::TEXT &&
                          n.kind != Node::Kind::ROW && n.a != NPOS && n.a >= st.selStart && n.b <= st.selEnd;
    if (selected) {
        fillRect(st, n.x, n.base - n.asc, n.w, n.asc + n.desc, st.selColor);
    }
    switch (n.kind) {
        case Node::Kind::TEXT: {
            if (st.editing && st.selStart < st.selEnd && st.selStart < n.b && st.selEnd > n.a) {
                const size_t s0 = std::max(st.selStart, n.a) - n.a;
                const size_t s1 = std::min(st.selEnd, n.b) - n.a;
                const double x0 = layoutX(n.layout.get(), n.map[s0]);
                const double x1 = layoutX(n.layout.get(), n.map[s1]);
                fillRect(st, n.x + std::min(x0, x1), n.base - n.asc, std::abs(x1 - x0), n.asc + n.desc, st.selColor);
            }
            showLayout(cr, n.layout.get(), n.x, n.base);
            break;
        }
        case Node::Kind::ROW:
            if (n.kids.empty() && n.isSlot && st.editing) {
                const double em = st.em * n.scale;
                const bool here = st.cursor != NPOS && st.cursor >= n.a && st.cursor <= n.b;
                const double x = n.x + 0.06 * em, y = n.base - n.asc, w = n.w - 0.12 * em, h = n.asc + n.desc;
                if (here) {
                    fillRect(st, x, y, w, h, Color(0x55a8c8ffU));
                }
                cairo_save(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
                Util::cairo_set_source_argb(cr, Color(here ? 0xff3d7bd0U : 0xff8c8c8cU));
                cairo_set_line_width(cr, std::max(0.04 * em, 0.3));
                const double dash = 0.1 * em;
                cairo_set_dash(cr, &dash, 1, 0);
                cairo_rectangle(cr, x, y, w, h);
                cairo_stroke(cr);
                cairo_restore(cr);
            }
            for (const auto& k: n.kids) {
                drawNode(*k, st);
            }
            break;
        case Node::Kind::FRAC:
            drawNode(*n.kids[0], st);
            drawNode(*n.kids[1], st);
            cairo_rectangle(cr, n.x + n.pad * 0.4, n.base - n.axis - n.thick / 2, n.w - n.pad * 0.8, n.thick);
            cairo_fill(cr);
            break;
        case Node::Kind::SUP:
            drawNode(*n.kids[0], st);
            break;
        case Node::Kind::ROOT: {
            const Node& c = *n.kids[0];
            const double em = st.em * n.scale;
            const double yt = n.base - (c.asc + 0.12 * em + n.thick / 2);
            const double yb = n.base + c.desc + 0.06 * em;
            const double ym = yt + 0.62 * (yb - yt);
            const double x0 = n.x + (n.cube ? 0.12 * em : 0);
            cairo_save(cr);
            cairo_set_line_width(cr, n.thick);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_move_to(cr, x0 + 0.05 * em, ym + 0.06 * em);
            cairo_line_to(cr, x0 + 0.17 * em, ym);
            cairo_line_to(cr, x0 + 0.33 * em, yb);
            cairo_line_to(cr, n.x + n.rw - 0.02 * em, yt);
            cairo_line_to(cr, n.x + n.w - 0.03 * em, yt);
            cairo_stroke(cr);
            cairo_restore(cr);
            if (n.cube && n.openLayout) {
                showLayout(cr, n.openLayout.get(), n.x, ym - 0.08 * em);
            }
            drawNode(c, st);
            break;
        }
        case Node::Kind::FENCE: {
            const Node& c = *n.kids[0];
            const double em = st.em * n.scale;
            auto delim = [&](PangoLayout* l, double x) {
                if (!n.tall) {
                    showLayout(cr, l, x, n.base);
                    return;
                }
                double w = 0, a = 0, d = 0;
                measure(l, w, a, d);
                const double top = n.base - c.asc - 0.06 * em;
                const double bottom = n.base + c.desc + 0.06 * em;
                cairo_save(cr);
                cairo_translate(cr, x, top);
                cairo_scale(cr, 1, (bottom - top) / std::max(a + d, 0.1));
                cairo_move_to(cr, 0, 0);
                pango_cairo_show_layout(cr, l);
                cairo_restore(cr);
            };
            delim(n.openLayout.get(), n.x);
            drawNode(c, st);
            delim(n.closeLayout.get(), c.x + c.w);
            break;
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

Formula::~Formula() = default;

static LayoutContext makeContext(const Style& style, bool editing, std::string_view src) {
    LayoutContext ctx;
    ctx.pango.reset(pango_font_map_create_context(pango_cairo_font_map_get_default()), xoj::util::adopt);
    pango_context_set_round_glyph_positions(ctx.pango.get(), false);
    ctx.fontName = style.fontName;
    ctx.fontSize = style.fontSize;
    ctx.editing = editing;
    ctx.src = src;
    auto l = ctx.newLayout(1);
    pango_layout_set_text(l.get(), "Ag", -1);
    double w = 0;
    measure(l.get(), w, ctx.asc0, ctx.desc0);
    return ctx;
}

void Formula::layoutLines(const Style& style) {
    LayoutContext ctx = makeContext(style, editing, source);
    em = style.fontSize;
    const double pitch = style.lineHeight > 0 ?
                                 style.lineHeight :
                                 (style.naturalLineHeight > 0 ? style.naturalLineHeight : ctx.asc0 + ctx.desc0);
    double base = style.firstBaseline > 0 ? style.firstBaseline : ctx.asc0;
    w = 0;
    yTop = 0;
    yBottom = 0;
    baselines.clear();
    for (size_t li = 0; li < lines.size(); li++) {
        Node& row = *lines[li];
        layoutRow(row, ctx, 1, 0);
        if (li > 0) {
            const Node& prev = *lines[li - 1];
            const double need = prev.desc + row.asc + 0.12 * em;
            double d = pitch;
            if (need > pitch) {
                d = style.lineHeight > 0 ? std::ceil(need / pitch - 1e-6) * pitch : need;
            }
            base += d;
        }
        baselines.push_back(base);
        place(row, 0, base);
        w = std::max(w, row.w);
        yTop = std::min(yTop, base - row.asc);
        yBottom = std::max(yBottom, base + row.desc);
    }
    if (!baselines.empty()) {
        yBottom = std::max(yBottom, baselines.back() + pitch - baselines.front());
    }
}

std::unique_ptr<Formula> Formula::forEditing(const std::string& text, const Style& style) {
    std::unique_ptr<Formula> f(new Formula());
    f->editing = true;
    f->source = text;
    size_t start = 0;
    while (true) {
        size_t end = f->source.find('\n', start);
        if (end == std::string::npos) {
            end = f->source.size();
        }
        f->lineStart.push_back(start);
        Parser p(f->source, start, end);
        auto row = p.parseLine();
        if (row->kids.empty()) {
            row->a = row->b = start;
        }
        f->lines.push_back(std::move(row));
        if (end == f->source.size()) {
            break;
        }
        start = end + 1;
    }
    f->layoutLines(style);

    f->carets.assign(f->source.size() + 1, Caret{});
    f->visible.assign(f->source.size(), false);
    for (size_t k = 0; k < f->source.size(); k++) {
        if (f->source[k] == '\n') {
            f->visible[k] = true;
        }
    }
    for (size_t li = 0; li < f->lines.size(); li++) {
        const Node& row = *f->lines[li];
        f->registerCarets(row, 0);
        const size_t ls = f->lineStart[li];
        const size_t le = li + 1 < f->lineStart.size() ? f->lineStart[li + 1] - 1 : f->source.size();
        const double b = f->baselines[li];
        const double a0 = row.kids.empty() ? row.asc : std::min(row.asc, f->em);
        Caret c{0, b - a0, b + std::min(row.desc, 0.4 * f->em), 0};
        if (f->carets[ls].depth < 0) {
            f->addCaret(ls, c);
        }
        if (f->carets[le].depth < 0) {
            c.x = row.w;
            f->addCaret(le, c);
        }
    }
    std::sort(f->slotList.begin(), f->slotList.end(),
              [](const Slot& x, const Slot& y) { return x.start < y.start || (x.start == y.start && x.end > y.end); });
    return f;
}

std::unique_ptr<Formula> Formula::forDisplay(const std::string& smu, const Style& style) {
    std::unique_ptr<Formula> f(new Formula());
    f->editing = false;
    size_t start = 0;
    while (true) {
        size_t end = smu.find('\n', start);
        if (end == std::string::npos) {
            end = smu.size();
        }
        std::string_view line(smu.data() + start, end - start);
        size_t i = 0;
        f->lines.push_back(parseSmu(line, i, ""));
        if (end == smu.size()) {
            break;
        }
        start = end + 1;
    }
    f->layoutLines(style);
    return f;
}

void Formula::addCaret(size_t pos, const Caret& c) {
    if (pos >= carets.size()) {
        return;
    }
    if (carets[pos].depth < c.depth) {
        carets[pos] = c;
    }
}

void Formula::registerCarets(const Node& n, int depth) {
    const double em = this->em * n.scale;
    auto outer = [&](double x) {
        return Caret{x, n.base - std::min(n.asc, 0.95 * em), n.base + std::min(n.desc, 0.3 * em), depth};
    };
    auto slotCaret = [&](const Node& r, double x) {
        return Caret{x, r.base - std::min(r.asc, 0.95 * this->em * r.scale),
                     r.base + std::min(r.desc, 0.3 * this->em * r.scale), depth + 1};
    };
    switch (n.kind) {
        case Node::Kind::TEXT: {
            for (size_t k = n.a; k < n.b; k++) {
                visible[k] = true;
            }
            if (!n.fname.empty()) {
                functionNames.emplace_back(n.a, n.b);
            }
            for (size_t k = n.a; k <= n.b; k++) {
                if (k < n.b && (static_cast<unsigned char>(source[k]) & 0xC0) == 0x80) {
                    continue;
                }
                if (!n.fname.empty() && k != n.a && k != n.b) {
                    continue;  // inside the name of a function: not a cursor position
                }
                addCaret(k,
                         Caret{n.x + layoutX(n.layout.get(), n.map[k - n.a]), n.base - n.asc, n.base + n.desc, depth});
            }
            return;
        }
        case Node::Kind::ROW:
            for (const auto& k: n.kids) {
                registerCarets(*k, depth);
            }
            if (n.kids.empty() && n.a != NPOS) {
                addCaret(n.a, Caret{n.x + (n.isSlot ? 0.14 * em : 0), n.base - n.asc, n.base + n.desc, depth});
            }
            return;
        default:
            break;
    }
    // A structure: before and after it, then its boxes
    addCaret(n.a, outer(n.x));
    if (n.b != NPOS) {
        addCaret(n.b, outer(n.x + n.w));  // (an unclosed structure: the end of its last box wins, it is deeper)
    }
    if (n.kind == Node::Kind::FENCE && !n.hiddenDelims) {
        if (n.openPos != NPOS && n.openPos < visible.size()) {
            visible[n.openPos] = true;
        }
        if (n.visibleClose && n.closePos < visible.size()) {
            visible[n.closePos] = true;
        }
    }
    bool allEmpty = true;
    for (const auto& r: n.kids) {
        allEmpty = allEmpty && r->kids.empty();
    }
    for (size_t idx = 0; idx < n.kids.size(); idx++) {
        const Node& r = *n.kids[idx];
        addCaret(r.a, slotCaret(r, r.x + (r.kids.empty() ? 0.14 * this->em * r.scale : 0)));
        if (!r.kids.empty()) {
            addCaret(r.b, slotCaret(r, r.x + r.w));
        }
        registerCarets(r, depth + 1);
        if (n.kind == Node::Kind::FENCE && !n.hiddenDelims) {
            continue;  // ordinary parentheses: their characters are deleted as usual
        }
        Slot s;
        s.start = r.a;
        s.end = r.b;
        s.outerStart = n.a;
        s.outerEnd = n.b;
        s.structureEmpty = allEmpty;
        s.exponent = n.kind == Node::Kind::SUP;
        if (r.kids.empty()) {
            if (n.kind == Node::Kind::FRAC && idx == 1) {
                s.delStart = n.slash;
                s.delEnd = n.b;
            } else if (n.kind != Node::Kind::FRAC) {
                s.delStart = n.a;
                s.delEnd = n.b;
            }
        }
        slotList.push_back(s);
    }
}

bool Formula::isStop(size_t pos) const { return pos < carets.size() && carets[pos].depth >= 0; }

bool Formula::visibleByte(size_t pos) const { return pos < visible.size() && visible[pos]; }

Caret Formula::caretAt(size_t pos) const {
    if (carets.empty()) {
        return Caret{0, 0, em, 0};
    }
    return carets[nearestStop(std::min(pos, carets.size() - 1))];
}

size_t Formula::nearestStop(size_t pos) const {
    if (carets.empty()) {
        return 0;
    }
    pos = std::min(pos, carets.size() - 1);
    for (size_t d = 0; d < carets.size(); d++) {
        if (pos + d < carets.size() && isStop(pos + d)) {
            return pos + d;
        }
        if (d <= pos && isStop(pos - d)) {
            return pos - d;
        }
    }
    return pos;
}

size_t Formula::step(size_t pos, int dir) const {
    size_t p = pos;
    while (true) {
        if (dir > 0) {
            if (p >= source.size()) {
                return pos;
            }
            p += utf8Len(source, p);
        } else {
            if (p == 0) {
                return pos;
            }
            p--;
            while (p > 0 && (static_cast<unsigned char>(source[p]) & 0xC0) == 0x80) {
                p--;
            }
        }
        if (isStop(p)) {
            return p;
        }
    }
}

size_t Formula::hit(double x, double y) const {
    size_t best = 0;
    double bestScore = 1e300;
    for (size_t p = 0; p < carets.size(); p++) {
        const Caret& c = carets[p];
        if (c.depth < 0) {
            continue;
        }
        const double dy = y < c.top ? c.top - y : (y > c.bottom ? y - c.bottom : 0);
        const double score = 3 * dy + std::abs(x - c.x) - 0.01 * c.depth;
        if (score < bestScore) {
            bestScore = score;
            best = p;
        }
    }
    return best;
}

std::optional<size_t> Formula::vertical(size_t pos, int dir) const {
    const Caret cur = caretAt(pos);
    const double cm = (cur.top + cur.bottom) / 2;
    const double h = cur.bottom - cur.top;
    std::optional<size_t> best;
    double bestScore = 1e300;
    for (size_t p = 0; p < carets.size(); p++) {
        const Caret& c = carets[p];
        if (c.depth < 0) {
            continue;
        }
        const double m = (c.top + c.bottom) / 2;
        if (dir < 0 ? !(m < cm - 0.3 * h) : !(m > cm + 0.3 * h)) {
            continue;
        }
        // Staying in the same structure (numerator ↔ denominator) is preferred to leaving it
        const double score = std::abs(c.x - cur.x) + 0.5 * std::abs(m - cm) + (c.depth < cur.depth ? em : 0);
        if (score < bestScore) {
            bestScore = score;
            best = p;
        }
    }
    return best;
}

std::optional<std::pair<size_t, size_t>> Formula::functionNameAt(size_t pos, int dir) const {
    for (const auto& [a, b]: functionNames) {
        if ((dir < 0 && pos == b) || (dir > 0 && pos == a)) {
            return std::make_pair(a, b);
        }
    }
    return std::nullopt;
}

std::optional<Slot> Formula::slotAround(size_t pos) const {
    std::optional<Slot> best;
    for (const Slot& s: slotList) {
        if (s.start <= pos && pos <= s.end && (!best || s.end - s.start <= best->end - best->start)) {
            best = s;
        }
    }
    return best;
}

void Formula::draw(cairo_t* cr, size_t cursor, size_t selStart, size_t selEnd, Color selectionColor) const {
    DrawState st{cr, editing, cursor, selStart, selEnd, selectionColor, em};
    for (const auto& l: lines) {
        drawNode(*l, st);
    }
    cairo_new_path(cr);
}

// ---------------------------------------------------------------------------------------------------------------------

std::string structuredMarkup(std::string_view source, const Environment* env) {
    std::string src(source);
    if (src.find('/') == std::string::npos && src.find('^') == std::string::npos &&
        src.find('(') == std::string::npos) {
        return prettifyMarkup(source, env);
    }
    Parser p(src, 0, src.size());
    auto row = p.parseLine();
    if (!hasStructureNode(*row)) {
        return prettifyMarkup(source, env);
    }
    std::string out;
    emitRow(*row, src, env, out);
    return out;
}

std::string flattenMarkup(std::string_view smu) {
    std::string out;
    for (char c: smu) {
        if (c == FRAC_SEP) {
            out += '/';
        } else if (!isControl(c)) {
            out += c;
        }
    }
    return out;
}

bool unitDivisionBefore(std::string_view text, size_t pos) {
    const size_t nl = pos == 0 ? std::string_view::npos : text.rfind('\n', pos - 1);
    const size_t ls = nl == std::string_view::npos ? 0 : nl + 1;
    // The line up to pos, followed by "/x": is that "/" a division of units (no fraction made of it)?
    std::string line(text.substr(ls, pos - ls));
    const size_t slash = line.size();
    line += "/x";
    Parser p(line, 0, line.size());
    auto row = p.parseLine();
    std::function<bool(const Node&)> fracAt = [&](const Node& n) {
        if (n.kind == Node::Kind::FRAC && n.slash == slash) {
            return true;
        }
        for (const auto& k: n.kids) {
            if (fracAt(*k)) {
                return true;
            }
        }
        return false;
    };
    return !fracAt(*row);
}

bool hasStructure(std::string_view text) {
    std::string src(text);
    size_t start = 0;
    while (true) {
        size_t end = src.find('\n', start);
        if (end == std::string::npos) {
            end = src.size();
        }
        Parser p(src, start, end);
        if (hasStructureNode(*p.parseLine())) {
            return true;
        }
        if (end == src.size()) {
            return false;
        }
        start = end + 1;
    }
}

}  // namespace xoj::calc::math
