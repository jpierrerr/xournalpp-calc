#include "CalcDocument.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <set>

#include <glib.h>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"

#include "ExcelLink.h"

namespace fs = std::filesystem;

namespace xoj::calc {

namespace {

constexpr const char* RESULT_COLOR = "#1a5fb4";
constexpr const char* ERROR_COLOR = "#c01c28";
constexpr const char* OK_COLOR = "#26a269";

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
                out += prettifyMarkup(l.source, env) + note(l);
                break;
            case LineResult::Kind::Evaluation: {
                std::string_view prefix(l.display);
                prefix.remove_suffix(std::min(prefix.size(), l.result.size()));
                out += prettifyMarkup(prefix, env);
                // Comparisons: green when it holds, red when not
                const bool holds = l.result.find("✓") != std::string::npos;
                const bool fails = l.result.find("✗") != std::string::npos;
                const char* color = holds && !fails ? OK_COLOR : fails && !holds ? ERROR_COLOR : RESULT_COLOR;
                out += std::string("<span foreground=\"") + color + "\">" + prettifyMarkup(l.result, env) + "</span>" +
                       note(l);
                break;
            }
            case LineResult::Kind::Error:
                out += prettifyMarkup(l.source, env);
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

std::vector<size_t> recalculateDocument(Document* doc, bool excelWrites, bool keepIfExcelBusy) {
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

    // 3. Show the results (in the regions that still exist)
    std::vector<size_t> changedPages;
    doc->lock();
    std::set<const Text*> existing;
    for (size_t p = 0; p < doc->getPageCount(); p++) {
        if (PageRef page = doc->getPage(p)) {
            for (Layer* layer: page->getLayers()) {
                for (auto& e: layer->getElements()) {
                    existing.insert(dynamic_cast<const Text*>(e.get()));
                }
            }
        }
    }
    for (auto& r: regions) {
        if (!existing.count(r.text)) {
            continue;
        }
        bool changed = r.text->setCalcMarkup(std::move(r.markup));
        r.text->setCalcCells(std::move(r.newCells));
        if (changed && (changedPages.empty() || changedPages.back() != r.page)) {
            changedPages.push_back(r.page);
        }
    }
    doc->unlock();
    return changedPages;
}

}  // namespace xoj::calc
