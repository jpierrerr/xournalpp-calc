#include "CalcDocument.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include <glib.h>

#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"

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
        if (c == '*') {
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
                out += prettifyMarkup(l.source, env);
                break;
            case LineResult::Kind::Evaluation: {
                std::string_view prefix(l.display);
                prefix.remove_suffix(std::min(prefix.size(), l.result.size()));
                out += prettifyMarkup(prefix, env);
                // Comparisons: green when it holds, red when not
                const bool holds = l.result.find("✓") != std::string::npos;
                const bool fails = l.result.find("✗") != std::string::npos;
                const char* color = holds && !fails ? OK_COLOR : fails && !holds ? ERROR_COLOR : RESULT_COLOR;
                out += std::string("<span foreground=\"") + color + "\">" + prettifyMarkup(l.result, env) + "</span>";
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

std::vector<size_t> recalculateDocument(Document* doc) {
    std::vector<size_t> changedPages;
    Environment env;

    doc->lock();
    const size_t pageCount = doc->getPageCount();
    for (size_t p = 0; p < pageCount; p++) {
        PageRef page = doc->getPage(p);
        if (!page) {
            continue;
        }
        std::vector<Text*> regions;
        for (Layer* layer: page->getLayers()) {
            for (auto& e: layer->getElements()) {
                if (e->getType() == ELEMENT_TEXT) {
                    auto* t = dynamic_cast<Text*>(e.get());
                    if (t && t->isCalc()) {
                        regions.push_back(t);
                    }
                }
            }
        }
        // Reading order: top to bottom, then left to right (regions on the same line, within 2pt)
        std::stable_sort(regions.begin(), regions.end(), [](const Text* a, const Text* b) {
            const auto& pa = a->getOrigin();
            const auto& pb = b->getOrigin();
            if (std::abs(pa.y - pb.y) > 2.0) {
                return pa.y < pb.y;
            }
            return pa.x < pb.x;
        });

        bool changed = false;
        for (Text* t: regions) {
            auto lines = evaluateText(t->getText(), env);
            changed |= t->setCalcMarkup(buildMarkup(lines, &env));
        }
        if (changed) {
            changedPages.push_back(p);
        }
    }
    doc->unlock();
    return changedPages;
}

}  // namespace xoj::calc
