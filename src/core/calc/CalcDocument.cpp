#include "CalcDocument.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>

#include <glib.h>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"

#include "ExcelLink.h"
#include "MathLayout.h"
#include "XlsxFile.h"

namespace fs = std::filesystem;

namespace xoj::calc {

namespace {

constexpr const char* RESULT_COLOR = "#1a5fb4";
constexpr const char* ERROR_COLOR = "#c01c28";
constexpr const char* OK_COLOR = "#26a269";
constexpr const char* EXCEL_COLOR = "#1d6f42";  ///< cells and workbooks of Excel
constexpr const char* NOTE_COLOR = "#77767b";

std::string escape(std::string_view s) {
    gchar* e = g_markup_escape_text(s.data(), static_cast<gssize>(s.size()));
    std::string r(e);
    g_free(e);
    return r;
}

// Multi-byte characters that end an identifier (operators and superscripts)
const char* const STOP_SEQUENCES[] = {"·", "×", "⋅", "∙", "÷", "−", "√", "≔", "⁰", "¹",
                                      "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹", "⁻"};

bool stopsIdentifier(std::string_view s, size_t i) {
    for (const char* stop: STOP_SEQUENCES) {
        if (s.substr(i, std::strlen(stop)) == stop) {
            return true;
        }
    }
    return false;
}

bool isIdentByte(std::string_view s, size_t i) {
    auto c = static_cast<unsigned char>(s[i]);
    return (std::isalnum(c) || c == '_' || c == '\'' || c >= 0x80) && !stopsIdentifier(s, i);
}


/// The file name of a path written in a document (with / or \\)
std::string fileNameOf(std::string_view path) {
    size_t slash = path.find_last_of("/\\");
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

/// A cell reference as it is shown: Hoja:"Hoja1" Celda:"C3" (instead of xl("Hoja1!C3"))
std::string cellLabelMarkup(std::string_view ref) {
    std::string text;
    try {
        CellRef r = parseCellRef(std::string(ref));
        if (!r.book.empty()) {
            text += "Libro:\"" + fileNameOf(r.book) + "\" ";
        }
        if (!r.sheet.empty()) {
            text += "Hoja:\"" + r.sheet + "\" ";
        }
        text += "Celda:\"" + r.cell + "\"";
    } catch (const CalcError&) {
        text = std::string(ref);
    }
    return std::string("<span foreground=\"") + EXCEL_COLOR + "\">" + escape(text) + "</span>";
}

/// Remove the quotes around a text ("..." or “...”)
std::string_view unquote(std::string_view s) {
    while (!s.empty() && s.front() == ' ') {
        s.remove_prefix(1);
    }
    while (!s.empty() && s.back() == ' ') {
        s.remove_suffix(1);
    }
    for (std::string_view q: {"\"", "“", "”"}) {
        if (s.substr(0, q.size()) == q) {
            s.remove_prefix(q.size());
            break;
        }
    }
    for (std::string_view q: {"\"", "”", "“"}) {
        if (s.size() >= q.size() && s.substr(s.size() - q.size()) == q) {
            s.remove_suffix(q.size());
            break;
        }
    }
    return s;
}

}  // namespace

std::optional<std::string> excelStatement(std::string_view line) {
    auto skipSpaces = [&](size_t i) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
            i++;
        }
        return i;
    };
    size_t i = skipSpaces(0);
    if (line.substr(i, 5) != "excel") {
        return std::nullopt;
    }
    i = skipSpaces(i + 5);
    if (i >= line.size() || line[i] != '(') {
        return std::nullopt;
    }
    i = skipSpaces(i + 1);
    if (i >= line.size() || line[i] != '"') {
        return std::nullopt;
    }
    size_t close = line.find('"', i + 1);
    if (close == std::string_view::npos || close == i + 1) {
        return std::nullopt;
    }
    size_t end = skipSpaces(close + 1);
    if (end >= line.size() || line[end] != ')' || skipSpaces(end + 1) != line.size()) {
        return std::nullopt;
    }
    return std::string(line.substr(i + 1, close - i - 1));
}

namespace {

/// Position of the ")" that closes the "(" at open (text between quotes is skipped), npos if it is missing
size_t matchingParenthesis(std::string_view s, size_t open) {
    int depth = 0;
    bool quoted = false;
    for (size_t i = open; i < s.size(); i++) {
        const char c = s[i];
        if (c == '"') {
            quoted = !quoted;
        } else if (quoted) {
            continue;
        } else if (c == '(' || c == '[') {
            depth++;
        } else if ((c == ')' || c == ']') && --depth == 0) {
            return i;
        }
    }
    return std::string_view::npos;
}

/// The arguments of a call, split at the separators (";" or ",") that are not inside parentheses or quotes
std::vector<std::string_view> splitArguments(std::string_view args) {
    std::vector<std::string_view> out;
    int depth = 0;
    bool quoted = false;
    size_t start = 0;
    for (size_t i = 0; i < args.size(); i++) {
        const char c = args[i];
        if (c == '"') {
            quoted = !quoted;
        } else if (quoted) {
            continue;
        } else if (c == '(' || c == '[') {
            depth++;
        } else if (c == ')' || c == ']') {
            depth--;
        } else if ((c == ';' || c == ',') && depth == 0) {
            out.push_back(args.substr(start, i - start));
            start = i + 1;
        }
    }
    out.push_back(args.substr(start));
    return out;
}

/// A function call in math notation: SENO(x) → sen(x), RAIZ(x) → √x with a bar over x, ABS(x) → |x|,
/// LOG(x; 2) → log₂(x). id: see functionId; name: as it is written; args: the text between the parentheses
std::string functionMarkup(const std::string& id, std::string_view name, std::string_view args,
                           const Environment* env) {
    if (id == "sqrt" || id == "cbrt") {
        return std::string(id == "sqrt" ? "√" : "∛") + "<span overline=\"single\">" + prettifyMarkup(args, env) +
               "</span>";
    }
    if (id == "abs") {
        return "|" + prettifyMarkup(args, env) + "|";
    }
    auto parts = splitArguments(args);
    if (id == "log" && parts.size() == 2) {
        return "log<sub>" + prettifyMarkup(parts[1], env) + "</sub>(" + prettifyMarkup(parts[0], env) + ")";
    }
    static const std::pair<const char*, const char*> NOTATION[] = {
            {"sin", "sen"},   {"cos", "cos"},       {"tan", "tan"},       {"asin", "arcsen"},
            {"acos", "arccos"}, {"atan", "arctan"}, {"ln", "ln"},         {"log10", "log"},
            {"log", "log"},   {"exp", "exp"},       {"min", "min"},       {"max", "max"},
            {"atan2", "ATAN2"}, {"round", "REDONDEAR"}, {"rounddown", "REDONDEAR.MENOS"},
            {"roundup", "REDONDEAR.MAS"},
    };
    std::string shown = escape(name);
    for (const auto& [i, n]: NOTATION) {
        if (id == i) {
            shown = n;
            break;
        }
    }
    // The arguments are separated by "; "
    std::string out = shown + "(";
    for (size_t k = 0; k < parts.size(); k++) {
        if (k > 0) {
            out += "; ";
        }
        out += prettifyMarkup(parts[k], env);
    }
    return out + ")";
}

}  // namespace

std::string prettifyMarkup(std::string_view s, const Environment* env) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c == '"') {
            // Text between quotes (a cell or a file name) is shown as it is written
            size_t j = s.find('"', i + 1);
            j = j == std::string_view::npos ? s.size() : j + 1;
            out += escape(s.substr(i, j - i));
            i = j;
        } else if (c == '*') {
            out += "·";
            i++;
        } else if (c == ':' && i + 1 < s.size() && s[i + 1] == '=') {
            out += "≔";  // ":=" is shown as the definition symbol
            i += 2;
        } else if (c == '^') {
            i++;
            if (i < s.size() && s[i] == '(') {
                // find the matching parenthesis
                size_t depth = 0, j = i;
                for (; j < s.size(); j++) {
                    if (s[j] == '(') {
                        depth++;
                    } else if (s[j] == ')' && --depth == 0) {
                        break;
                    }
                }
                size_t end = std::min(j, s.size());
                out += "<sup>" + prettifyMarkup(s.substr(i + 1, end - i - 1), env) + "</sup>";
                i = end < s.size() ? end + 1 : end;
            } else {
                size_t j = i;
                if (j < s.size() && (s[j] == '-' || s[j] == '+')) {
                    j++;
                }
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '.')) {
                    j++;
                }
                out += "<sup>" + escape(s.substr(i, j - i)) + "</sup>";
                i = j;
            }
        } else if (c == ' ' || c == '\t') {
            // Any run of blanks is shown as one space, so a unit stays at a fixed distance from its number
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
                i++;
            }
            if (!out.empty()) {
                out += ' ';
            }
        } else if (std::isdigit(c) ||
                   (c == '.' && i + 1 < s.size() && std::isdigit(static_cast<unsigned char>(s[i + 1])))) {
            // A number: digits, decimal point and exponent ("1.5e-3")
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
            out += escape(s.substr(i, j - i));
            i = j;
            // "2m" is shown as "2 m": a unit right after its number gets the same single space
            if (i < s.size() &&
                (std::isalpha(static_cast<unsigned char>(s[i])) || static_cast<unsigned char>(s[i]) >= 0x80) &&
                !stopsIdentifier(s, i) && s.substr(i, std::strlen("°")) != "°") {
                out += ' ';
            }
        } else if ((std::isalpha(c) || c == '_' || c >= 0x80) && !stopsIdentifier(s, i)) {
            size_t j = i;
            while (j < s.size() && isIdentByte(s, j)) {
                j++;
            }
            std::string_view ident = s.substr(i, j - i);
            auto unitPower = splitUnitPower(ident);
            size_t open = j;
            while (open < s.size() && s[open] == ' ') {
                open++;
            }
            if (ident == "xl" && open < s.size() && s[open] == '(') {
                // A cell read from Excel is shown as Hoja:"Hoja1" Celda:"C3"; the command comes back when editing
                if (size_t close = s.find(')', open); close != std::string_view::npos) {
                    out += cellLabelMarkup(unquote(s.substr(open + 1, close - open - 1)));
                    i = close + 1;
                    continue;
                }
            }
            {
                // REDONDEAR.MENOS / REDONDEAR.MAS: the name goes on after the point
                if (j + 1 < s.size() && s[j] == '.') {
                    size_t k = j + 1;
                    while (k < s.size() && isIdentByte(s, k)) {
                        k++;
                    }
                    if (auto id = functionId(s.substr(i, k - i)); id && (*id == "rounddown" || *id == "roundup")) {
                        j = k;
                        ident = s.substr(i, j - i);
                        open = j;
                        while (open < s.size() && s[open] == ' ') {
                            open++;
                        }
                    }
                }
                if (s.substr(open, 1) == "(" && callsFunction(std::string(ident), env)) {
                    if (size_t close = matchingParenthesis(s, open); close != std::string_view::npos) {
                        out += functionMarkup(*functionId(ident), ident, s.substr(open + 1, close - open - 1), env);
                        i = close + 1;
                        continue;
                    }
                }
            }
            if (ident == "sqrt") {
                out += "√";
            } else if (unitPower &&
                       !(env && (env->vars.count(std::string(ident)) || env->vars.count(unitPower->first)))) {
                out += escape(unitPower->first) + "<sup>" + std::to_string(unitPower->second) + "</sup>";
            } else if (auto unit = canonicalUnitName(ident);
                       unit && *unit != ident && !(env && env->vars.count(std::string(ident)))) {
                out += escape(*unit);  // "kpa" is shown as kPa
            } else if (size_t u = ident.find('_'); u != std::string_view::npos && u > 0 && u + 1 < ident.size()) {
                out += escape(ident.substr(0, u)) + "<sub>" + escape(ident.substr(u + 1)) + "</sub>";
            } else {
                out += escape(ident);
            }
            i = j;
        } else {
            // copy one UTF-8 character
            size_t len = 1;
            while (i + len < s.size() && (static_cast<unsigned char>(s[i + len]) & 0xC0) == 0x80) {
                len++;
            }
            out += escape(s.substr(i, len));
            i += len;
        }
    }
    return out;
}

namespace {

/// excel("...") and enviar(...) are shown in words: Libro ligado → "Zapatas.xlsx", Enviar M = 750 kN·m → Hoja:...
std::optional<std::string> statementMarkup(const LineResult& l, const Environment* env) {
    std::string_view src(l.source);
    while (!src.empty() && (src.front() == ' ' || src.front() == '\t')) {
        src.remove_prefix(1);
    }
    if (auto book = excelStatement(src)) {
        const bool live = l.result.find("abierto en Excel") != std::string::npos;
        return std::string("<b>Libro ligado</b> → <span foreground=\"") + EXCEL_COLOR + "\">\"" +
               escape(fileNameOf(*book)) + "\"</span>   <span foreground=\"" + NOTE_COLOR + "\" size=\"smaller\">" +
               (live ? "abierto en Excel" : "archivo") + "</span>";
    }
    if (src.substr(0, 6) != "enviar") {
        return std::nullopt;
    }
    // enviar(expr, "ref"[, unit]): the expression is what comes before the reference in quotes
    size_t open = src.find('(');
    size_t q1 = src.find('"');
    size_t q2 = q1 == std::string_view::npos ? q1 : src.find('"', q1 + 1);
    size_t comma = q1 == std::string_view::npos ? q1 : src.find_last_of(",;", q1);
    size_t colon = l.result.rfind(": ");
    if (open == std::string_view::npos || q2 == std::string_view::npos || comma == std::string_view::npos ||
        comma < open || colon == std::string::npos) {
        return std::nullopt;
    }
    std::string value = l.result.substr(colon + 2);
    std::string where = l.result.substr(0, colon);
    const bool notSent = where.find("(no enviado)") != std::string::npos;
    return std::string("<b>Enviar</b> ") + prettifyMarkup(src.substr(open + 1, comma - open - 1), env) +
           " = <span foreground=\"" + RESULT_COLOR + "\">" + prettifyMarkup(value, env) + "</span> → " +
           cellLabelMarkup(src.substr(q1 + 1, q2 - q1 - 1)) +
           (notSent ? std::string("   <span foreground=\"") + NOTE_COLOR + "\" size=\"smaller\">no enviado</span>" :
                      std::string());
}

}  // namespace

std::string buildMarkup(const std::vector<LineResult>& lines, const Environment* env) {
    std::string out;
    bool first = true;
    auto note = [](const LineResult& l) {
        return l.note.empty() ?
                       std::string() :
                       std::string("   <span foreground=\"#77767b\" size=\"smaller\">") + escape(l.note) + "</span>";
    };
    for (const auto& l: lines) {
        if (!first) {
            out += "\n";
        }
        first = false;
        switch (l.kind) {
            case LineResult::Kind::Text:
                out += escape(l.source);
                break;
            case LineResult::Kind::Definition:
                out += math::structuredMarkup(l.source, env) + note(l);
                break;
            case LineResult::Kind::Evaluation: {
                if (auto statement = statementMarkup(l, env)) {
                    out += *statement + note(l);
                    break;
                }
                std::string_view prefix(l.display);
                prefix.remove_suffix(std::min(prefix.size(), l.result.size()));
                out += math::structuredMarkup(prefix, env);
                // Comparisons: green when it holds, red when not
                const bool holds = l.result.find("✓") != std::string::npos;
                const bool fails = l.result.find("✗") != std::string::npos;
                const char* color = holds && !fails ? OK_COLOR : fails && !holds ? ERROR_COLOR : RESULT_COLOR;
                out += std::string("<span foreground=\"") + color + "\">" + prettifyMarkup(l.result, env) + "</span>" +
                       note(l);
                break;
            }
            case LineResult::Kind::Error:
                out += math::structuredMarkup(l.source, env);
                out += std::string("   <span foreground=\"") + ERROR_COLOR + "\" size=\"smaller\">⚠ " +
                       escape(l.error) + "</span>";
                break;
        }
    }
    return out;
}

namespace {

struct Region {
    size_t page;
    Text* text;
    std::string source;
    std::string cells;  ///< values read from Excel, as saved
    std::string markup;
    std::string newCells;
};

/// The calculation regions in reading order, and the folder of the document. Locks the document while reading it
std::vector<Region> collectRegions(Document* doc, fs::path& baseDir) {
    std::vector<Region> regions;
    doc->lock_shared();
    baseDir = doc->getFilepath();
    baseDir = baseDir.empty() ? fs::path() : baseDir.parent_path();
    const size_t pageCount = doc->getPageCount();
    for (size_t p = 0; p < pageCount; p++) {
        PageRef page = doc->getPage(p);
        if (!page) {
            continue;
        }
        std::vector<Text*> onPage;
        for (Layer* layer: page->getLayers()) {
            for (auto& e: layer->getElements()) {
                if (e->getType() == ELEMENT_TEXT) {
                    auto* t = dynamic_cast<Text*>(e.get());
                    if (t && t->isCalc()) {
                        onPage.push_back(t);
                    }
                }
            }
        }
        // Reading order: top to bottom, then left to right (regions on the same line, within 2pt)
        std::stable_sort(onPage.begin(), onPage.end(), [](const Text* a, const Text* b) {
            const auto& pa = a->getOrigin();
            const auto& pb = b->getOrigin();
            if (std::abs(pa.y - pb.y) > 2.0) {
                return pa.y < pb.y;
            }
            return pa.x < pb.x;
        });
        for (Text* t: onPage) {
            regions.push_back({p, t, t->getText(), t->getCalcCells(), {}, {}});
        }
    }
    doc->unlock_shared();
    return regions;
}

}  // namespace

std::vector<std::string> documentSources(Document* doc, fs::path* baseDir) {
    fs::path dir;
    std::vector<std::string> out;
    for (auto& r: collectRegions(doc, dir)) {
        out.push_back(std::move(r.source));
    }
    if (baseDir) {
        *baseDir = dir;
    }
    return out;
}

Environment documentVariables(Document* doc) {
    fs::path baseDir;
    auto regions = collectRegions(doc, baseDir);
    ExcelLink link(baseDir, false);
    Environment env;
    env.cells = &link;
    for (auto& r: regions) {
        CellCache saved = parseCellCache(r.cells);
        CellCache fresh;
        link.setRegionCache(&saved, &fresh);
        evaluateText(r.source, env);
    }
    env.cells = nullptr;
    return env;
}

bool documentUsesExcel(Document* doc) {
    for (const auto& src: documentSources(doc)) {
        if (src.find("xl(") != std::string::npos || src.find("enviar(") != std::string::npos) {
            return true;
        }
    }
    return false;
}

namespace {

std::vector<size_t> recalculate(Document* doc, bool excelWrites, bool keepIfExcelBusy) {
    // 1. Collect the regions in reading order. The evaluation itself happens without holding the document lock,
    //    because talking to Excel may dispatch window messages (e.g. a repaint that needs the lock)
    fs::path baseDir;
    std::vector<Region> regions = collectRegions(doc, baseDir);

    // 2. Evaluate
    ExcelLink link(baseDir, excelWrites);
    Environment env;
    env.cells = &link;
    for (auto& r: regions) {
        CellCache saved = parseCellCache(r.cells);
        CellCache fresh;
        link.setRegionCache(&saved, &fresh);
        auto lines = evaluateText(r.source, env);
        r.markup = buildMarkup(lines, &env);
        r.newCells = serializeCellCache(fresh);
    }
    link.setRegionCache(nullptr, nullptr);
    if (keepIfExcelBusy && link.wasBusy()) {
        return {};  // try again later, instead of showing "Excel is busy" for a moment
    }

    // 3. Show the results in the regions that still exist (a region may have been deleted, and its memory reused by
    //    another text, while Excel was answering: it must still be a calculation on the same page)
    std::vector<size_t> changedPages;
    std::lock_guard<Document> lock(*doc);
    std::vector<std::set<const Text*>> onPage(doc->getPageCount());
    for (size_t p = 0; p < doc->getPageCount(); p++) {
        if (PageRef page = doc->getPage(p)) {
            for (Layer* layer: page->getLayers()) {
                for (auto& e: layer->getElements()) {
                    if (auto* t = dynamic_cast<const Text*>(e.get()); t && t->isCalc()) {
                        onPage[p].insert(t);
                    }
                }
            }
        }
    }
    for (auto& r: regions) {
        if (r.page >= onPage.size() || !onPage[r.page].count(r.text) || r.text->getText() != r.source) {
            continue;
        }
        bool changed = r.text->setCalcMarkup(std::move(r.markup));
        r.text->setCalcCells(std::move(r.newCells));
        if (changed && (changedPages.empty() || changedPages.back() != r.page)) {
            changedPages.push_back(r.page);
        }
    }
    return changedPages;
}

}  // namespace

std::vector<size_t> recalculateDocument(Document* doc, bool excelWrites, bool keepIfExcelBusy) {
    // Talking to Excel may dispatch window messages, and with them another recalculation: one at a time
    static bool running = false;
    if (running || !doc) {
        return {};
    }
    running = true;
    std::vector<size_t> changed;
    try {
        changed = recalculate(doc, excelWrites, keepIfExcelBusy);
    } catch (const std::exception& e) {
        g_warning("xournalpp-calc: the calculations could not be updated: %s", e.what());
    }
    running = false;
    return changed;
}

}  // namespace xoj::calc
