#include "XlsxFile.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

#include <glib.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <zip.h>

#include "CalcEngine.h"  // for CalcError

namespace fs = std::filesystem;

namespace xoj::calc {

// ---------------------------------------------------------------------------------------------------------------------
// References
// ---------------------------------------------------------------------------------------------------------------------

std::string pathToUtf8(const fs::path& p) {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

fs::path pathFromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::string columnName(int col) {
    std::string s;
    while (col > 0) {
        int r = (col - 1) % 26;
        s.insert(s.begin(), static_cast<char>('A' + r));
        col = (col - 1) / 26;
    }
    return s;
}

namespace {

/// "C5" → (5, 3); false if it is not a cell name
bool splitCellName(std::string_view s, int& row, int& col) {
    size_t i = 0;
    if (i < s.size() && s[i] == '$') {
        i++;
    }
    col = 0;
    size_t letters = 0;
    while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) {
        col = col * 26 + (std::toupper(static_cast<unsigned char>(s[i])) - 'A' + 1);
        i++;
        letters++;
    }
    if (i < s.size() && s[i] == '$') {
        i++;
    }
    if (letters == 0 || letters > 3 || i == s.size()) {
        return false;
    }
    row = 0;
    for (; i < s.size(); i++) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
            return false;
        }
        row = row * 10 + (s[i] - '0');
        if (row > 1048576) {
            return false;
        }
    }
    return row > 0 && col <= 16384;
}

std::string trim(std::string s) {
    auto sp = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), sp));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), sp).base(), s.end());
    return s;
}

}  // namespace

std::string CellRef::toString() const { return sheet.empty() ? cell : sheet + "!" + cell; }

CellRef parseCellRef(const std::string& text) {
    CellRef r;
    std::string s = trim(text);
    std::string sheetPart;
    std::string cellPart = s;
    if (!s.empty() && s[0] == '\'') {
        // 'Mi hoja'!C5 (a quote inside the name is written twice)
        size_t i = 1;
        std::string name;
        for (; i < s.size(); i++) {
            if (s[i] == '\'') {
                if (i + 1 < s.size() && s[i + 1] == '\'') {
                    name += '\'';
                    i++;
                    continue;
                }
                break;
            }
            name += s[i];
        }
        if (i + 1 >= s.size() || s[i + 1] != '!') {
            throw CalcError("referencia inválida: " + text);
        }
        sheetPart = name;
        cellPart = s.substr(i + 2);
    } else if (size_t bang = s.rfind('!'); bang != std::string::npos) {
        sheetPart = s.substr(0, bang);
        cellPart = s.substr(bang + 1);
    } else if (!s.empty() && s[0] == '[') {
        // [libro.xlsx]C5
        size_t close = s.find(']');
        if (close == std::string::npos) {
            throw CalcError("referencia inválida: " + text);
        }
        sheetPart = s.substr(0, close + 1);
        cellPart = s.substr(close + 1);
    }
    if (!sheetPart.empty() && sheetPart[0] == '[') {
        size_t close = sheetPart.find(']');
        if (close == std::string::npos) {
            throw CalcError("referencia inválida: " + text);
        }
        r.book = trim(sheetPart.substr(1, close - 1));
        sheetPart = sheetPart.substr(close + 1);
    }
    r.sheet = trim(sheetPart);
    if (!splitCellName(trim(cellPart), r.row, r.col)) {
        throw CalcError("celda inválida: «" + trim(cellPart) + "» (se escribe como C5 u Hoja1!C5)");
    }
    r.cell = columnName(r.col) + std::to_string(r.row);
    return r;
}

namespace xlsx {

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Zip and XML helpers
// ---------------------------------------------------------------------------------------------------------------------

struct XmlDocDeleter {
    void operator()(xmlDoc* d) const { xmlFreeDoc(d); }
};
using XmlDocPtr = std::unique_ptr<xmlDoc, XmlDocDeleter>;

std::string fileName(const fs::path& p) { return pathToUtf8(p.filename()); }

zip_t* openZip(const fs::path& file, int flags) {
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        throw CalcError("no existe el libro " + pathToUtf8(file));
    }
    int err = 0;
    zip_t* z = zip_open(pathToUtf8(file).c_str(), flags, &err);
    if (!z) {
        if (err == ZIP_ER_NOZIP || err == ZIP_ER_INCONS) {
            throw CalcError(fileName(file) +
                            " no es un libro .xlsx (los .xls antiguos no se pueden leer: guárdalo como .xlsx)");
        }
        if (err == ZIP_ER_OPEN || err == ZIP_ER_READ) {
            throw CalcError("no se pudo abrir " + fileName(file) + " (¿está abierto en otro programa?)");
        }
        zip_error_t ze;
        zip_error_init_with_code(&ze, err);
        std::string msg = std::string("no se pudo abrir ") + fileName(file) + ": " + zip_error_strerror(&ze);
        zip_error_fini(&ze);
        throw CalcError(msg);
    }
    return z;
}

struct ZipCloser {
    void operator()(zip_t* z) const { zip_discard(z); }
};
using ZipPtr = std::unique_ptr<zip_t, ZipCloser>;

std::optional<std::string> readEntry(zip_t* z, const std::string& name) {
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat(z, name.c_str(), 0, &st) != 0) {
        return std::nullopt;
    }
    zip_file_t* f = zip_fopen(z, name.c_str(), 0);
    if (!f) {
        return std::nullopt;
    }
    std::string data(static_cast<size_t>(st.size), '\0');
    zip_int64_t n = zip_fread(f, data.data(), st.size);
    zip_fclose(f);
    if (n < 0 || static_cast<zip_uint64_t>(n) != st.size) {
        return std::nullopt;
    }
    return data;
}

XmlDocPtr parseXml(const std::string& data, const std::string& what) {
    xmlDoc* d = xmlReadMemory(data.data(), static_cast<int>(data.size()), what.c_str(), nullptr,
                              XML_PARSE_NONET | XML_PARSE_HUGE | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    if (!d) {
        throw CalcError("el libro está dañado (" + what + ")");
    }
    return XmlDocPtr(d);
}

bool is(const xmlNode* n, const char* name) {
    return n && n->type == XML_ELEMENT_NODE && xmlStrEqual(n->name, BAD_CAST name);
}

xmlNode* child(xmlNode* parent, const char* name) {
    for (xmlNode* c = parent ? parent->children : nullptr; c; c = c->next) {
        if (is(c, name)) {
            return c;
        }
    }
    return nullptr;
}

std::optional<std::string> attr(const xmlNode* n, const char* name) {
    // The attribute may be in a namespace (r:id): match by local name
    for (xmlAttr* a = n->properties; a; a = a->next) {
        if (xmlStrEqual(a->name, BAD_CAST name)) {
            xmlChar* v = xmlNodeListGetString(n->doc, a->children, 1);
            std::string s = v ? reinterpret_cast<const char*>(v) : "";
            xmlFree(v);
            return s;
        }
    }
    return std::nullopt;
}

std::string content(xmlNode* n) {
    xmlChar* v = xmlNodeGetContent(n);
    std::string s = v ? reinterpret_cast<const char*>(v) : "";
    xmlFree(v);
    return s;
}

std::string fold(const std::string& s) {
    gchar* f = g_utf8_casefold(s.c_str(), static_cast<gssize>(s.size()));
    std::string r = f ? f : s;
    g_free(f);
    return r;
}

/// Number written as text in a cell ("12.5", "12,5", " 3 ")
std::optional<double> parseNumber(std::string s) {
    s = trim(std::move(s));
    if (s.empty()) {
        return std::nullopt;
    }
    if (s.find('.') == std::string::npos) {
        std::replace(s.begin(), s.end(), ',', '.');
    }
    char* end = nullptr;
    double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !std::isfinite(v)) {
        return std::nullopt;
    }
    return v;
}

std::string formatValue(double v) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.15g", v);
    if (std::strtod(buf, nullptr) != v) {
        std::snprintf(buf, sizeof(buf), "%.17g", v);
    }
    return buf;
}

// ---------------------------------------------------------------------------------------------------------------------
// Workbook structure
// ---------------------------------------------------------------------------------------------------------------------

/// Path inside the zip of the worksheet with that name ("" = the first one)
std::string sheetEntry(zip_t* z, const fs::path& file, const std::string& sheet) {
    auto wbData = readEntry(z, "xl/workbook.xml");
    auto relData = readEntry(z, "xl/_rels/workbook.xml.rels");
    if (!wbData || !relData) {
        throw CalcError(fileName(file) + " no es un libro de Excel válido");
    }
    auto wb = parseXml(*wbData, "workbook.xml");
    auto rels = parseXml(*relData, "workbook.xml.rels");

    std::string id;
    std::vector<std::string> names;
    xmlNode* sheets = child(xmlDocGetRootElement(wb.get()), "sheets");
    for (xmlNode* s = sheets ? sheets->children : nullptr; s; s = s->next) {
        if (!is(s, "sheet")) {
            continue;
        }
        std::string name = attr(s, "name").value_or("");
        names.push_back(name);
        if ((sheet.empty() && id.empty()) || (!sheet.empty() && fold(name) == fold(sheet))) {
            id = attr(s, "id").value_or("");
            if (!sheet.empty()) {
                break;
            }
        }
    }
    if (id.empty()) {
        std::string list;
        for (const auto& n: names) {
            list += (list.empty() ? "" : ", ") + n;
        }
        throw CalcError("no existe la hoja «" + sheet + "» en " + fileName(file) + " (hojas: " + list + ")");
    }
    for (xmlNode* r = xmlDocGetRootElement(rels.get())->children; r; r = r->next) {
        if (is(r, "Relationship") && attr(r, "Id") == id) {
            std::string target = attr(r, "Target").value_or("");
            if (!target.empty() && target[0] == '/') {
                return target.substr(1);
            }
            return "xl/" + target;
        }
    }
    throw CalcError(fileName(file) + " está dañado (no se encuentra la hoja)");
}

/// The row and cell elements of a cell, if they exist. Rows and cells without "r" follow the previous one
struct CellNodes {
    xmlNode* sheetData = nullptr;
    xmlNode* row = nullptr;
    xmlNode* cell = nullptr;
    xmlNode* rowAfter = nullptr;   ///< first row after the wanted one (to insert before it)
    xmlNode* cellAfter = nullptr;  ///< first cell after the wanted one in its row
};

CellNodes findCell(xmlDoc* doc, const CellRef& ref) {
    CellNodes res;
    res.sheetData = child(xmlDocGetRootElement(doc), "sheetData");
    if (!res.sheetData) {
        return res;
    }
    int rowNum = 0;
    for (xmlNode* r = res.sheetData->children; r; r = r->next) {
        if (!is(r, "row")) {
            continue;
        }
        auto rAttr = attr(r, "r");
        rowNum = rAttr ? std::atoi(rAttr->c_str()) : rowNum + 1;
        if (rowNum < ref.row) {
            continue;
        }
        if (rowNum > ref.row) {
            res.rowAfter = r;
            break;
        }
        res.row = r;
        int colNum = 0;
        for (xmlNode* c = r->children; c; c = c->next) {
            if (!is(c, "c")) {
                continue;
            }
            auto cAttr = attr(c, "r");
            int rr = 0, cc = 0;
            colNum = cAttr && splitCellName(*cAttr, rr, cc) ? cc : colNum + 1;
            if (colNum == ref.col) {
                res.cell = c;
                break;
            }
            if (colNum > ref.col) {
                res.cellAfter = c;
                break;
            }
        }
        break;
    }
    return res;
}

// ---------------------------------------------------------------------------------------------------------------------
// Cache of the workbooks that are read (each one is parsed again only when the file changes)
// ---------------------------------------------------------------------------------------------------------------------

struct Book {
    fs::file_time_type mtime{};
    uintmax_t size = 0;
    std::map<std::string, std::string> sheetEntries;  ///< sheet name (folded) → entry
    std::map<std::string, XmlDocPtr> sheets;          ///< entry → document
    std::optional<std::vector<std::string>> sharedStrings;
};

std::map<std::string, Book>& books() {
    static std::map<std::string, Book> b;
    return b;
}

Book& book(const fs::path& file) {
    std::error_code ec;
    auto mtime = fs::last_write_time(file, ec);
    auto size = fs::file_size(file, ec);
    Book& b = books()[pathToUtf8(file)];
    if (b.mtime != mtime || b.size != size) {
        b = Book{};
        b.mtime = mtime;
        b.size = size;
    }
    return b;
}

std::vector<std::string> readSharedStrings(zip_t* z) {
    std::vector<std::string> out;
    auto data = readEntry(z, "xl/sharedStrings.xml");
    if (!data) {
        return out;
    }
    auto doc = parseXml(*data, "sharedStrings.xml");
    for (xmlNode* si = xmlDocGetRootElement(doc.get())->children; si; si = si->next) {
        if (!is(si, "si")) {
            continue;
        }
        // Plain text (<t>) or rich text runs (<r><t>); phonetic runs (<rPh>) are not part of the text
        std::string s;
        for (xmlNode* c = si->children; c; c = c->next) {
            if (is(c, "t")) {
                s += content(c);
            } else if (is(c, "r")) {
                if (xmlNode* t = child(c, "t")) {
                    s += content(t);
                }
            }
        }
        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace

double read(const fs::path& file, const CellRef& ref) {
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        throw CalcError("no existe el libro " + pathToUtf8(file));
    }
    Book& b = book(file);
    const std::string where = ref.toString() + " de " + fileName(file);

    std::unique_ptr<zip_t, ZipCloser> z;
    auto zip = [&]() {
        if (!z) {
            z.reset(openZip(file, ZIP_RDONLY));
        }
        return z.get();
    };

    std::string key = fold(ref.sheet);
    auto e = b.sheetEntries.find(key);
    if (e == b.sheetEntries.end()) {
        e = b.sheetEntries.emplace(key, sheetEntry(zip(), file, ref.sheet)).first;
    }
    auto s = b.sheets.find(e->second);
    if (s == b.sheets.end()) {
        auto data = readEntry(zip(), e->second);
        if (!data) {
            throw CalcError(fileName(file) + " está dañado (falta " + e->second + ")");
        }
        s = b.sheets.emplace(e->second, parseXml(*data, e->second)).first;
    }

    CellNodes n = findCell(s->second.get(), ref);
    if (!n.cell) {
        throw CalcError("la celda " + where + " está vacía");
    }
    std::string type = attr(n.cell, "t").value_or("n");
    xmlNode* v = child(n.cell, "v");
    if (type == "inlineStr") {
        xmlNode* inl = child(n.cell, "is");
        std::string text = inl ? content(inl) : "";
        if (auto num = parseNumber(text)) {
            return *num;
        }
        throw CalcError("la celda " + where + " tiene texto: «" + text + "»");
    }
    if (!v) {
        throw CalcError("la celda " + where + " está vacía");
    }
    std::string raw = content(v);
    if (type == "e") {
        throw CalcError("la celda " + where + " tiene un error de Excel: " + raw);
    }
    if (type == "s") {
        if (!b.sharedStrings) {
            b.sharedStrings = readSharedStrings(zip());
        }
        size_t idx = static_cast<size_t>(std::atol(raw.c_str()));
        std::string text = idx < b.sharedStrings->size() ? (*b.sharedStrings)[idx] : "";
        if (auto num = parseNumber(text)) {
            return *num;
        }
        throw CalcError("la celda " + where + " tiene texto: «" + text + "»");
    }
    if (auto num = parseNumber(raw)) {
        return *num;  // numbers, booleans (0/1) and formulas that give a number
    }
    throw CalcError("la celda " + where + " tiene texto: «" + raw + "»");
}

void write(const fs::path& file, const CellRef& ref, double value) {
    if (!std::isfinite(value)) {
        throw CalcError("no se puede enviar un valor infinito o indefinido");
    }
    const std::string where = ref.toString() + " de " + fileName(file);
    ZipPtr z(openZip(file, 0));

    std::string entry = sheetEntry(z.get(), file, ref.sheet);
    auto data = readEntry(z.get(), entry);
    if (!data) {
        throw CalcError(fileName(file) + " está dañado (falta " + entry + ")");
    }
    auto doc = parseXml(*data, entry);
    CellNodes n = findCell(doc.get(), ref);
    if (!n.sheetData) {
        throw CalcError(fileName(file) + " está dañado (hoja sin datos)");
    }
    const std::string text = formatValue(value);

    if (n.cell) {
        if (child(n.cell, "f")) {
            throw CalcError("la celda " + where + " tiene una fórmula: envía el valor a otra celda");
        }
        auto type = attr(n.cell, "t").value_or("n");
        xmlNode* v = child(n.cell, "v");
        if ((type == "n") && v) {
            if (auto old = parseNumber(content(v)); old && *old == value) {
                return;  // already there: the file is not touched
            }
        }
        // Keep the style ("s"), replace the content
        xmlUnsetProp(n.cell, BAD_CAST "t");
        while (n.cell->children) {
            xmlNode* c = n.cell->children;
            xmlUnlinkNode(c);
            xmlFreeNode(c);
        }
        xmlNewTextChild(n.cell, n.cell->ns, BAD_CAST "v", BAD_CAST text.c_str());
    } else {
        xmlNode* row = n.row;
        if (!row) {
            row = xmlNewNode(n.sheetData->ns, BAD_CAST "row");
            xmlNewProp(row, BAD_CAST "r", BAD_CAST std::to_string(ref.row).c_str());
            if (n.rowAfter) {
                xmlAddPrevSibling(n.rowAfter, row);
            } else {
                xmlAddChild(n.sheetData, row);
            }
        }
        xmlUnsetProp(row, BAD_CAST "spans");  // optional hint about the columns of the row: it may have changed
        xmlNode* cell = xmlNewNode(row->ns, BAD_CAST "c");
        xmlNewProp(cell, BAD_CAST "r", BAD_CAST ref.cell.c_str());
        xmlNewTextChild(cell, row->ns, BAD_CAST "v", BAD_CAST text.c_str());
        if (n.cellAfter) {
            xmlAddPrevSibling(n.cellAfter, cell);
        } else {
            xmlAddChild(row, cell);
        }
    }

    auto replace = [&](const std::string& name, xmlDoc* d) {
        xmlChar* mem = nullptr;
        int len = 0;
        xmlDocDumpMemoryEnc(d, &mem, &len, "UTF-8");
        void* copy = std::malloc(static_cast<size_t>(len));
        std::memcpy(copy, mem, static_cast<size_t>(len));
        xmlFree(mem);
        zip_source_t* src = zip_source_buffer(z.get(), copy, static_cast<zip_uint64_t>(len), 1);
        zip_int64_t idx = zip_name_locate(z.get(), name.c_str(), 0);
        if (!src || idx < 0 || zip_file_replace(z.get(), static_cast<zip_uint64_t>(idx), src, 0) != 0) {
            if (src) {
                zip_source_free(src);
            }
            throw CalcError("no se pudo modificar " + fileName(file));
        }
    };
    replace(entry, doc.get());

    // Excel recalculates the formulas that use the cell when it opens the workbook
    if (auto wbData = readEntry(z.get(), "xl/workbook.xml")) {
        auto wb = parseXml(*wbData, "workbook.xml");
        xmlNode* root = xmlDocGetRootElement(wb.get());
        xmlNode* calcPr = child(root, "calcPr");
        if (!calcPr || attr(calcPr, "fullCalcOnLoad") != "1") {
            if (!calcPr) {
                calcPr = xmlNewNode(root->ns, BAD_CAST "calcPr");
                // Order of the schema: calcPr comes after sheets, functionGroups, externalReferences, definedNames
                xmlNode* after = nullptr;
                for (xmlNode* c = root->children; c; c = c->next) {
                    if (is(c, "sheets") || is(c, "functionGroups") || is(c, "externalReferences") ||
                        is(c, "definedNames")) {
                        after = c;
                    }
                }
                if (after) {
                    xmlAddNextSibling(after, calcPr);
                } else {
                    xmlAddChild(root, calcPr);
                }
            }
            xmlSetProp(calcPr, BAD_CAST "fullCalcOnLoad", BAD_CAST "1");
            replace("xl/workbook.xml", wb.get());
        }
    }

    zip_t* raw = z.release();
    if (zip_close(raw) != 0) {
        std::string msg = zip_strerror(raw);
        zip_discard(raw);
        throw CalcError("no se pudo guardar " + fileName(file) + " (¿está abierto en otro programa?): " + msg);
    }
    books().erase(pathToUtf8(file));
}

std::vector<std::string> sheetNames(const fs::path& file) {
    ZipPtr z(openZip(file, ZIP_RDONLY));
    auto wbData = readEntry(z.get(), "xl/workbook.xml");
    if (!wbData) {
        throw CalcError(fileName(file) + " no es un libro de Excel válido");
    }
    auto wb = parseXml(*wbData, "workbook.xml");
    std::vector<std::string> names;
    xmlNode* sheets = child(xmlDocGetRootElement(wb.get()), "sheets");
    for (xmlNode* s = sheets ? sheets->children : nullptr; s; s = s->next) {
        if (is(s, "sheet")) {
            names.push_back(attr(s, "name").value_or(""));
        }
    }
    return names;
}

SheetPreview preview(const fs::path& file, const std::string& sheet, int maxRows, int maxCols) {
    ZipPtr z(openZip(file, ZIP_RDONLY));
    std::string entry = sheetEntry(z.get(), file, sheet);
    auto data = readEntry(z.get(), entry);
    if (!data) {
        throw CalcError(fileName(file) + " está dañado (falta " + entry + ")");
    }
    auto doc = parseXml(*data, entry);
    std::optional<std::vector<std::string>> shared;

    SheetPreview out;
    xmlNode* sheetData = child(xmlDocGetRootElement(doc.get()), "sheetData");
    int rowNum = 0;
    for (xmlNode* r = sheetData ? sheetData->children : nullptr; r; r = r->next) {
        if (!is(r, "row")) {
            continue;
        }
        auto rAttr = attr(r, "r");
        rowNum = rAttr ? std::atoi(rAttr->c_str()) : rowNum + 1;
        if (rowNum > maxRows) {
            break;
        }
        int colNum = 0;
        for (xmlNode* c = r->children; c; c = c->next) {
            if (!is(c, "c")) {
                continue;
            }
            auto cAttr = attr(c, "r");
            int rr = 0, cc = 0;
            colNum = cAttr && splitCellName(*cAttr, rr, cc) ? cc : colNum + 1;
            if (colNum > maxCols) {
                break;
            }
            std::string type = attr(c, "t").value_or("n");
            std::string text;
            if (type == "inlineStr") {
                xmlNode* inl = child(c, "is");
                text = inl ? content(inl) : "";
            } else if (xmlNode* v = child(c, "v")) {
                std::string raw = content(v);
                if (type == "s") {
                    if (!shared) {
                        shared = readSharedStrings(z.get());
                    }
                    size_t idx = static_cast<size_t>(std::atol(raw.c_str()));
                    text = idx < shared->size() ? (*shared)[idx] : "";
                } else if (type == "b") {
                    text = raw == "1" ? "VERDADERO" : "FALSO";
                } else if (type == "n" || type.empty()) {
                    if (auto num = parseNumber(raw)) {
                        char buf[40];
                        std::snprintf(buf, sizeof(buf), "%.10g", *num);
                        text = buf;
                    } else {
                        text = raw;
                    }
                } else {
                    text = raw;  // "str" (text formula), "e" (error)
                }
            }
            if (text.empty()) {
                continue;
            }
            out.cells[{rowNum, colNum}] = text;
            out.rows = std::max(out.rows, rowNum);
            out.cols = std::max(out.cols, colNum);
        }
    }
    return out;
}

}  // namespace xlsx

}  // namespace xoj::calc
