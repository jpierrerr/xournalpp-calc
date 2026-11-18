#include "ExcelLink.h"

#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace xoj::calc {

// ---------------------------------------------------------------------------------------------------------------------
// Cache of the values read
// ---------------------------------------------------------------------------------------------------------------------

namespace {

std::string escapeKey(const std::string& k) {
    std::string out;
    for (char c: k) {
        if (c == '%' || c == '|' || c == ';' || static_cast<unsigned char>(c) < 0x20) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

std::string unescapeKey(std::string_view k) {
    std::string out;
    for (size_t i = 0; i < k.size(); i++) {
        if (k[i] == '%' && i + 2 < k.size()) {
            out += static_cast<char>(std::strtol(std::string(k.substr(i + 1, 2)).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += k[i];
        }
    }
    return out;
}

}  // namespace

std::string serializeCellCache(const CellCache& cache) {
    std::string out;
    for (const auto& [k, v]: cache) {
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        out += escapeKey(k) + "|" + buf + ";";
    }
    return out;
}

CellCache parseCellCache(std::string_view s) {
    CellCache out;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(';', start);
        if (end == std::string_view::npos) {
            end = s.size();
        }
        std::string_view item = s.substr(start, end - start);
        if (size_t bar = item.rfind('|'); bar != std::string_view::npos) {
            std::string num(item.substr(bar + 1));
            char* e = nullptr;
            double v = std::strtod(num.c_str(), &e);
            if (e && *e == '\0' && !num.empty()) {
                out[unescapeKey(item.substr(0, bar))] = v;
            }
        }
        start = end + 1;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------------------------------
// Link
// ---------------------------------------------------------------------------------------------------------------------

ExcelLink::ExcelLink(fs::path baseDir, bool allowWrites): baseDir(std::move(baseDir)), allowWrites(allowWrites) {}

void ExcelLink::setRegionCache(const CellCache* s, CellCache* f) {
    saved = s;
    fresh = f;
}

std::vector<std::string> ExcelLink::takeNotes() { return std::exchange(notes, {}); }

fs::path ExcelLink::resolve(const std::string& path) const {
    fs::path p = pathFromUtf8(path);
    if (!p.has_extension()) {
        p += ".xlsx";
    }
    if (p.is_relative()) {
        if (baseDir.empty()) {
            throw CalcError("guarda primero la hoja (.xopp) o escribe la ruta completa del libro");
        }
        p = baseDir / p;
    }
    return p.lexically_normal();
}

fs::path ExcelLink::workbookFor(const CellRef& r) const {
    if (!r.book.empty()) {
        return resolve(r.book);
    }
    if (!workbook) {
        throw CalcError("indica primero el libro con excel(\"libro.xlsx\")");
    }
    return *workbook;
}

std::string ExcelLink::useWorkbook(const std::string& path) {
    workbook.reset();
    fs::path p = resolve(path);
    workbook = p;  // also if it does not exist: the values saved with the sheet are used
    const std::string name = "\"" + pathToUtf8(p.filename()) + "\"";
    if (excelapp::isOpen(p)) {
        return name + " abierto en Excel";
    }
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        throw CalcError("no existe el libro " + pathToUtf8(p));
    }
    return name + " (archivo)";
}

double ExcelLink::read(const std::string& text) {
    CellRef r = parseCellRef(text);
    fs::path file;
    std::string key;
    try {
        file = workbookFor(r);
        key = pathToUtf8(file.filename()) + "|" + r.toString();
        double v;
        if (auto live = excelapp::read(file, r)) {
            v = *live;
        } else {
            v = xlsx::read(file, r);
        }
        if (fresh) {
            (*fresh)[key] = v;
        }
        return v;
    } catch (const CalcError& e) {
        if (key.empty() && !r.book.empty()) {
            key = pathToUtf8(pathFromUtf8(r.book).filename()) + "|" + r.toString();
        }
        if (saved && !key.empty()) {
            if (auto it = saved->find(key); it != saved->end()) {
                notes.push_back(r.toString() + ": valor guardado (" + e.what() + ")");
                if (fresh) {
                    (*fresh)[key] = it->second;
                }
                return it->second;
            }
        }
        throw;
    }
}

std::string ExcelLink::write(const std::string& text, double value) {
    CellRef r = parseCellRef(text);
    fs::path file = workbookFor(r);
    std::string where = (r.book.empty() ? "" : "[" + pathToUtf8(file.filename()) + "]") + r.toString();
    if (!allowWrites) {
        return where + " (no enviado)";
    }
    if (excelapp::write(file, r, value)) {
        return where + " en Excel";
    }
    xlsx::write(file, r, value);
    return where;
}

}  // namespace xoj::calc
