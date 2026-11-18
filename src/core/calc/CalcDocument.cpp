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

std::string prettifyMarkup(std::string_view s) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c == '*') {
            out += "·";
            i++;
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
                out += "<sup>" + prettifyMarkup(s.substr(i + 1, end - i - 1)) + "</sup>";
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
        } else if ((std::isalpha(c) || c == '_' || c >= 0x80) && !stopsIdentifier(s, i)) {
            size_t j = i;
            while (j < s.size() && isIdentByte(s, j)) {
                j++;
            }
            std::string_view ident = s.substr(i, j - i);
            if (ident == "sqrt") {
                out += "√";
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

std::string buildMarkup(const std::vector<LineResult>& lines) {
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
                out += prettifyMarkup(l.source);
                break;
            case LineResult::Kind::Evaluation: {
                std::string_view prefix(l.display);
                prefix.remove_suffix(std::min(prefix.size(), l.result.size()));
                out += prettifyMarkup(prefix);
                out += std::string("<span foreground=\"") + RESULT_COLOR + "\">" + prettifyMarkup(l.result) + "</span>";
                break;
            }
            case LineResult::Kind::Error:
                out += prettifyMarkup(l.source);
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
            changed |= t->setCalcMarkup(buildMarkup(lines));
        }
        if (changed) {
            changedPages.push_back(p);
        }
    }
    doc->unlock();
    return changedPages;
}

}  // namespace xoj::calc
