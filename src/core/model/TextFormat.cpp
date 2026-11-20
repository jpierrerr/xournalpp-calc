#include "TextFormat.h"

#include <algorithm>
#include <array>
#include <sstream>

#include <glib.h>

namespace xoj::text {

namespace {
constexpr std::array<const char*, 9> CODES{"b", "i", "u", "s", "x", "fg", "bg", "f", "z"};

std::string escape(const std::string& v) {
    std::string out;
    for (char c: v) {
        if (c == ';' || c == ',' || c == '%') {
            char buf[4];
            g_snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

std::string unescape(const std::string& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '%' && i + 2 < v.size()) {
            out += static_cast<char>(std::stoi(v.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += v[i];
        }
    }
    return out;
}

/// Join runs of the same kind and value that touch, drop empty ones, keep them sorted
void normalize(Formats& f) {
    f.erase(std::remove_if(f.begin(), f.end(), [](const FormatRun& r) { return r.end <= r.start; }), f.end());
    std::sort(f.begin(), f.end(), [](const FormatRun& a, const FormatRun& b) {
        if (a.kind != b.kind) {
            return a.kind < b.kind;
        }
        return a.start < b.start;
    });
    Formats out;
    for (auto& r: f) {
        if (!out.empty() && out.back().kind == r.kind && out.back().value == r.value && out.back().end >= r.start) {
            out.back().end = std::max(out.back().end, r.end);
        } else {
            out.push_back(r);
        }
    }
    f = std::move(out);
}

bool parseColor(const std::string& v, guint16& r, guint16& g, guint16& b) {
    PangoColor c;
    if (!pango_color_parse(&c, v.c_str())) {
        return false;
    }
    r = c.red;
    g = c.green;
    b = c.blue;
    return true;
}
}  // namespace

std::string serialize(const Formats& f) {
    std::string s;
    for (const auto& r: f) {
        if (!s.empty()) {
            s += ';';
        }
        s += CODES[static_cast<size_t>(r.kind)];
        s += ',' + std::to_string(r.start) + ',' + std::to_string(r.end) + ',' + escape(r.value);
    }
    return s;
}

Formats parse(const std::string& s) {
    Formats f;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ';')) {
        std::vector<std::string> fields;
        std::stringstream ps(part);
        std::string field;
        while (std::getline(ps, field, ',')) {
            fields.push_back(field);
        }
        if (fields.size() < 3) {
            continue;
        }
        auto it = std::find_if(CODES.begin(), CODES.end(), [&](const char* c) { return fields[0] == c; });
        if (it == CODES.end()) {
            continue;
        }
        try {
            FormatRun r{std::stoi(fields[1]), std::stoi(fields[2]), static_cast<FormatKind>(it - CODES.begin()),
                        fields.size() > 3 ? unescape(fields[3]) : std::string()};
            f.push_back(std::move(r));
        } catch (...) {}
    }
    normalize(f);
    return f;
}

void set(Formats& f, FormatKind kind, int start, int end, const std::optional<std::string>& value) {
    if (end <= start) {
        return;
    }
    Formats out;
    for (const auto& r: f) {
        if (r.kind != kind || r.end <= start || r.start >= end) {
            out.push_back(r);
            continue;
        }
        if (r.start < start) {
            out.push_back({r.start, start, r.kind, r.value});
        }
        if (r.end > end) {
            out.push_back({end, r.end, r.kind, r.value});
        }
    }
    if (value) {
        out.push_back({start, end, kind, *value});
    }
    f = std::move(out);
    normalize(f);
}

void clear(Formats& f, int start, int end) {
    for (int k = 0; k <= static_cast<int>(FormatKind::SIZE); k++) {
        set(f, static_cast<FormatKind>(k), start, end, std::nullopt);
    }
}

std::optional<std::string> valueAt(const Formats& f, FormatKind kind, int start, int end) {
    if (end <= start) {
        if (start <= 0) {
            return std::nullopt;
        }
        start = start - 1;
        end = start + 1;
    }
    std::optional<std::string> value;
    int covered = start;
    // runs of one kind do not overlap and are sorted
    for (const auto& r: f) {
        if (r.kind != kind || r.end <= start || r.start >= end) {
            continue;
        }
        if (r.start > covered) {
            return std::nullopt;  // a gap without the format
        }
        if (value && *value != r.value) {
            return std::nullopt;
        }
        value = r.value;
        covered = r.end;
    }
    if (covered < end) {
        return std::nullopt;
    }
    return value;
}

void onInsert(Formats& f, int pos, int count) {
    for (auto& r: f) {
        if (pos < r.start || (pos == r.start && pos > 0)) {
            // after the insertion (text typed just before a run takes the format of the character before it)
            r.start += count;
            r.end += count;
        } else if (pos <= r.end) {
            // inside the run, at its end, or at the very start of the text
            r.end += count;
        }
    }
    normalize(f);
}

void onDelete(Formats& f, int start, int end) {
    const int n = end - start;
    auto map = [&](int p) { return p <= start ? p : (p < end ? start : p - n); };
    for (auto& r: f) {
        r.start = map(r.start);
        r.end = map(r.end);
    }
    normalize(f);
}

void addToPango(const Formats& f, const std::string& text, PangoAttrList* list, double baseSize) {
    const char* s = text.c_str();
    const auto nChars = static_cast<int>(g_utf8_strlen(s, static_cast<gssize>(text.size())));
    auto byteAt = [&](int ch) -> guint {
        ch = std::clamp(ch, 0, nChars);
        return static_cast<guint>(g_utf8_offset_to_pointer(s, ch) - s);
    };
    for (const auto& r: f) {
        if (r.start >= nChars) {
            continue;
        }
        std::vector<PangoAttribute*> attrs;
        switch (r.kind) {
            case FormatKind::BOLD:
                attrs.push_back(pango_attr_weight_new(PANGO_WEIGHT_BOLD));
                break;
            case FormatKind::ITALIC:
                attrs.push_back(pango_attr_style_new(PANGO_STYLE_ITALIC));
                break;
            case FormatKind::UNDERLINE:
                attrs.push_back(pango_attr_underline_new(r.value == "double" ? PANGO_UNDERLINE_DOUBLE :
                                                         r.value == "wave"   ? PANGO_UNDERLINE_ERROR :
                                                                               PANGO_UNDERLINE_SINGLE));
                break;
            case FormatKind::STRIKE:
                attrs.push_back(pango_attr_strikethrough_new(true));
                break;
            case FormatKind::SCRIPT: {
                const bool sup = r.value == "sup";
                attrs.push_back(pango_attr_scale_new(0.65));
                attrs.push_back(pango_attr_rise_new(static_cast<int>((sup ? 0.38 : -0.12) * baseSize * PANGO_SCALE)));
                break;
            }
            case FormatKind::FOREGROUND:
            case FormatKind::BACKGROUND: {
                guint16 red, green, blue;
                if (parseColor(r.value, red, green, blue)) {
                    attrs.push_back(r.kind == FormatKind::FOREGROUND ? pango_attr_foreground_new(red, green, blue) :
                                                                       pango_attr_background_new(red, green, blue));
                }
                break;
            }
            case FormatKind::FAMILY:
                attrs.push_back(pango_attr_family_new(r.value.c_str()));
                break;
            case FormatKind::SIZE:
                try {
                    attrs.push_back(pango_attr_size_new_absolute(static_cast<int>(std::stod(r.value) * PANGO_SCALE)));
                } catch (...) {}
                break;
        }
        for (auto* a: attrs) {
            a->start_index = byteAt(r.start);
            a->end_index = byteAt(r.end);
            pango_attr_list_insert(list, a);
        }
    }
}

}  // namespace xoj::text
