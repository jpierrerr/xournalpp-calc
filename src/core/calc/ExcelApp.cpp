/*
 * Xournal++ Calc
 *
 * Cells of workbooks that are open in Excel (Windows, COM automation by late binding through IDispatch, so that no
 * Office headers or type libraries are needed). Excel is never started: only workbooks already open are used.
 *
 * @license GNU GPLv2 or later
 */

#include <string>
#include <vector>

#include "ExcelLink.h"

#ifdef _WIN32
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>

#include <objbase.h>
#include <oleauto.h>
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace xoj::calc::excelapp {

#ifdef _WIN32

namespace {

/// Owning pointer to an IDispatch
class Disp {
public:
    Disp() = default;
    explicit Disp(IDispatch* d): d(d) {}
    Disp(const Disp&) = delete;
    Disp& operator=(const Disp&) = delete;
    Disp(Disp&& o) noexcept: d(o.d) { o.d = nullptr; }
    Disp& operator=(Disp&& o) noexcept {
        if (this != &o) {
            reset();
            d = o.d;
            o.d = nullptr;
        }
        return *this;
    }
    ~Disp() { reset(); }
    void reset() {
        if (d) {
            d->Release();
            d = nullptr;
        }
    }
    IDispatch* get() const { return d; }
    explicit operator bool() const { return d != nullptr; }

private:
    IDispatch* d = nullptr;
};

/// VARIANT that is cleared when it goes out of scope
struct Var {
    VARIANT v;
    Var() { VariantInit(&v); }
    explicit Var(double d) {
        VariantInit(&v);
        v.vt = VT_R8;
        v.dblVal = d;
    }
    explicit Var(int i) {
        VariantInit(&v);
        v.vt = VT_I4;
        v.lVal = i;
    }
    explicit Var(const std::wstring& s) {
        VariantInit(&v);
        v.vt = VT_BSTR;
        v.bstrVal = SysAllocStringLen(s.data(), static_cast<UINT>(s.size()));
    }
    Var(const Var&) = delete;
    Var& operator=(const Var&) = delete;
    ~Var() { VariantClear(&v); }
};

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(n, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const wchar_t* w) {
    if (!w) {
        return {};
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(std::max(n, 1)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    s.pop_back();  // terminating zero
    return s;
}

bool comReady() {
    thread_local const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

/// Error message for a failed call to Excel
std::string excelError(HRESULT hr, const EXCEPINFO& ex) {
    if (hr == RPC_E_CALL_REJECTED || hr == RPC_E_SERVERCALL_RETRYLATER) {
        return "Excel está ocupado (¿hay una celda en edición o un cuadro de diálogo abierto?)";
    }
    if (ex.bstrDescription) {
        return "Excel: " + narrow(ex.bstrDescription);
    }
    char buf[48];
    std::snprintf(buf, sizeof(buf), "Excel no respondió (0x%08lX)", static_cast<unsigned long>(hr));
    return buf;
}

/// Call a method or property by name; args in their natural order. Throws CalcError
void invoke(IDispatch* obj, const wchar_t* name, WORD flags, std::vector<VARIANT*> args, VARIANT* result) {
    DISPID id;
    LPOLESTR n = const_cast<LPOLESTR>(name);
    HRESULT hr = obj->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) {
        throw CalcError("Excel: no se encontró " + narrow(name));
    }
    std::vector<VARIANT> rev;
    for (auto it = args.rbegin(); it != args.rend(); ++it) {
        rev.push_back(**it);  // shallow copies: the callers keep ownership
    }
    DISPPARAMS params{};
    params.cArgs = static_cast<UINT>(rev.size());
    params.rgvarg = rev.empty() ? nullptr : rev.data();
    DISPID putId = DISPID_PROPERTYPUT;
    if (flags & DISPATCH_PROPERTYPUT) {
        params.cNamedArgs = 1;
        params.rgdispidNamedArgs = &putId;
    }
    EXCEPINFO ex{};
    UINT argErr = 0;
    hr = obj->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, result, &ex, &argErr);
    std::string msg;
    if (FAILED(hr)) {
        msg = excelError(hr, ex);
    }
    SysFreeString(ex.bstrDescription);
    SysFreeString(ex.bstrSource);
    SysFreeString(ex.bstrHelpFile);
    if (hr == RPC_E_CALL_REJECTED || hr == RPC_E_SERVERCALL_RETRYLATER) {
        throw ExcelBusy(msg);
    }
    if (FAILED(hr)) {
        throw CalcError(msg);
    }
}

Disp getObject(IDispatch* obj, const wchar_t* name, std::vector<VARIANT*> args = {}) {
    Var r;
    invoke(obj, name, DISPATCH_PROPERTYGET | DISPATCH_METHOD, std::move(args), &r.v);
    if (r.v.vt != VT_DISPATCH || !r.v.pdispVal) {
        throw CalcError("Excel: respuesta inesperada de " + narrow(name));
    }
    IDispatch* d = r.v.pdispVal;
    d->AddRef();
    return Disp(d);
}

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    std::replace(s.begin(), s.end(), L'/', L'\\');
    return s;
}

/// Whether a running Excel does not answer window messages ("Not responding"). A call to it would wait without a
/// time limit, and Xournal would freeze with it. After a hang is seen, Excel is left alone for a few seconds
bool excelNotResponding() {
    static ULONGLONG quietUntil = 0;
    if (GetTickCount64() < quietUntil) {
        return true;
    }
    bool hung = false;
    EnumWindows(
            [](HWND h, LPARAM p) -> BOOL {
                wchar_t cls[16] = {};
                if (GetClassNameW(h, cls, 16) > 0 && std::wcscmp(cls, L"XLMAIN") == 0) {
                    DWORD_PTR answer = 0;
                    if (!SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 300, &answer)) {
                        *reinterpret_cast<bool*>(p) = true;
                        return FALSE;
                    }
                }
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&hung));
    if (hung) {
        quietUntil = GetTickCount64() + 5000;
    }
    return hung;
}

/// The workbook open in Excel with that file, or nothing. Throws ExcelBusy if Excel does not answer
Disp findWorkbook(const fs::path& file) {
    if (!comReady()) {
        return {};
    }
    if (excelNotResponding()) {
        throw ExcelBusy("Excel no responde");
    }
    const std::wstring path = file.wstring();

    // 1. Excel registers each open workbook in the running object table with a file moniker (works with several
    //    Excel processes)
    IRunningObjectTable* rot = nullptr;
    if (SUCCEEDED(GetRunningObjectTable(0, &rot)) && rot) {
        IMoniker* mk = nullptr;
        if (SUCCEEDED(CreateFileMoniker(path.c_str(), &mk)) && mk) {
            IUnknown* unk = nullptr;
            if (rot->GetObject(mk, &unk) == S_OK && unk) {
                IDispatch* d = nullptr;
                if (SUCCEEDED(unk->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&d))) && d) {
                    unk->Release();
                    mk->Release();
                    rot->Release();
                    return Disp(d);
                }
                unk->Release();
            }
            mk->Release();
        }
        rot->Release();
    }

    // 2. The workbooks of the running Excel, by full name or by name (workbooks in OneDrive have a URL as full name)
    CLSID clsid;
    if (FAILED(CLSIDFromProgID(L"Excel.Application", &clsid))) {
        return {};  // Excel is not installed
    }
    IUnknown* unk = nullptr;
    if (FAILED(GetActiveObject(clsid, nullptr, &unk)) || !unk) {
        return {};  // Excel is not running
    }
    IDispatch* app = nullptr;
    HRESULT hr = unk->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&app));
    unk->Release();
    if (FAILED(hr) || !app) {
        return {};
    }
    Disp excel(app);
    try {
        Disp books = getObject(excel.get(), L"Workbooks");
        Var count;
        invoke(books.get(), L"Count", DISPATCH_PROPERTYGET, {}, &count.v);
        if (FAILED(VariantChangeType(&count.v, &count.v, 0, VT_I4))) {
            return {};
        }
        const std::wstring wantedPath = lower(path);
        const std::wstring wantedName = lower(file.filename().wstring());
        Disp byName;
        for (int i = 1; i <= count.v.lVal; i++) {
            Var idx(i);
            Disp wb = getObject(books.get(), L"Item", {&idx.v});
            Var fullName, name;
            invoke(wb.get(), L"FullName", DISPATCH_PROPERTYGET, {}, &fullName.v);
            invoke(wb.get(), L"Name", DISPATCH_PROPERTYGET, {}, &name.v);
            if (fullName.v.vt == VT_BSTR && lower(fullName.v.bstrVal ? fullName.v.bstrVal : L"") == wantedPath) {
                return wb;
            }
            if (!byName && name.v.vt == VT_BSTR && lower(name.v.bstrVal ? name.v.bstrVal : L"") == wantedName &&
                fullName.v.vt == VT_BSTR && fullName.v.bstrVal &&
                std::wstring(fullName.v.bstrVal).rfind(L"http", 0) == 0) {
                byName = std::move(wb);
            }
        }
        return byName;
    } catch (const ExcelBusy&) {
        throw;  // the workbook may be open: its file must not be used (it may be older, or locked)
    } catch (const CalcError&) {
        return {};
    }
}

/// The range of one cell of an open workbook
Disp cellRange(IDispatch* wb, const CellRef& ref) {
    Disp sheets = getObject(wb, L"Worksheets");
    Disp ws;
    try {
        if (ref.sheet.empty()) {
            Var first(1);
            ws = getObject(sheets.get(), L"Item", {&first.v});
        } else {
            Var name(widen(ref.sheet));
            ws = getObject(sheets.get(), L"Item", {&name.v});
        }
    } catch (const CalcError&) {
        throw CalcError("no existe la hoja «" + ref.sheet + "» en el libro abierto en Excel");
    }
    Var cell(widen(ref.cell));
    return getObject(ws.get(), L"Range", {&cell.v});
}

/// Number in a VARIANT read from a cell
double numberOf(VARIANT& v, const CellRef& ref) {
    const std::string where = "la celda " + ref.toString();
    switch (v.vt) {
        case VT_EMPTY:
        case VT_NULL:
            throw CalcError(where + " está vacía");
        case VT_ERROR:
            throw CalcError(where + " tiene un error de Excel");
        case VT_BSTR: {
            std::string text = narrow(v.bstrVal);
            std::string t = text;
            if (t.find('.') == std::string::npos) {
                std::replace(t.begin(), t.end(), ',', '.');
            }
            char* end = nullptr;
            double d = std::strtod(t.c_str(), &end);
            while (end && *end == ' ') {
                end++;
            }
            if (t.empty() || !end || *end != '\0') {
                throw CalcError(where + " tiene texto: «" + text + "»");
            }
            return d;
        }
        default: {
            VARIANT d;
            VariantInit(&d);
            if (FAILED(VariantChangeType(&d, &v, 0, VT_R8))) {
                throw CalcError(where + " no tiene un número");
            }
            return d.dblVal;
        }
    }
}

}  // namespace

bool isOpen(const fs::path& file) { return static_cast<bool>(findWorkbook(file)); }

std::optional<double> read(const fs::path& file, const CellRef& ref) {
    Disp wb = findWorkbook(file);
    if (!wb) {
        return std::nullopt;
    }
    Disp range = cellRange(wb.get(), ref);
    Var value;
    invoke(range.get(), L"Value2", DISPATCH_PROPERTYGET, {}, &value.v);
    return numberOf(value.v, ref);
}

bool write(const fs::path& file, const CellRef& ref, double value) {
    Disp wb = findWorkbook(file);
    if (!wb) {
        return false;
    }
    Disp range = cellRange(wb.get(), ref);
    Var hasFormula;
    invoke(range.get(), L"HasFormula", DISPATCH_PROPERTYGET, {}, &hasFormula.v);
    if (hasFormula.v.vt == VT_BOOL && hasFormula.v.boolVal != VARIANT_FALSE) {
        throw CalcError("la celda " + ref.toString() + " tiene una fórmula: envía el valor a otra celda");
    }
    Var old;
    invoke(range.get(), L"Value2", DISPATCH_PROPERTYGET, {}, &old.v);
    if (old.v.vt == VT_R8 && old.v.dblVal == value) {
        return true;  // unchanged: the workbook is not marked as modified
    }
    Var v(value);
    invoke(range.get(), L"Value2", DISPATCH_PROPERTYPUT, {&v.v}, nullptr);
    return true;
}

namespace {

int intProperty(IDispatch* obj, const wchar_t* name) {
    Var v;
    invoke(obj, name, DISPATCH_PROPERTYGET, {}, &v.v);
    if (FAILED(VariantChangeType(&v.v, &v.v, 0, VT_I4))) {
        throw CalcError("Excel: respuesta inesperada de " + narrow(name));
    }
    return v.v.lVal;
}

/// Text of a value as it is shown in the preview
std::string displayText(VARIANT& v) {
    switch (v.vt) {
        case VT_EMPTY:
        case VT_NULL:
            return {};
        case VT_BSTR:
            return narrow(v.bstrVal);
        case VT_BOOL:
            return v.boolVal != VARIANT_FALSE ? "VERDADERO" : "FALSO";
        case VT_ERROR:
            return "#ERROR";
        default: {
            VARIANT d;
            VariantInit(&d);
            if (FAILED(VariantChangeType(&d, &v, 0, VT_R8))) {
                return {};
            }
            char buf[40];
            std::snprintf(buf, sizeof(buf), "%.10g", d.dblVal);
            return buf;
        }
    }
}

}  // namespace

std::optional<std::vector<std::string>> sheetNames(const fs::path& file) {
    try {
        Disp wb = findWorkbook(file);
        if (!wb) {
            return std::nullopt;
        }
        Disp sheets = getObject(wb.get(), L"Worksheets");
        int n = intProperty(sheets.get(), L"Count");
        std::vector<std::string> names;
        for (int i = 1; i <= n; i++) {
            Var idx(i);
            Disp ws = getObject(sheets.get(), L"Item", {&idx.v});
            Var name;
            invoke(ws.get(), L"Name", DISPATCH_PROPERTYGET, {}, &name.v);
            names.push_back(name.v.vt == VT_BSTR ? narrow(name.v.bstrVal) : std::string());
        }
        return names;
    } catch (const CalcError&) {
        return std::nullopt;  // Excel is busy: the file is used instead
    }
}

std::optional<SheetPreview> preview(const fs::path& file, const std::string& sheet, int maxRows, int maxCols) {
    Disp wb;
    try {
        wb = findWorkbook(file);
    } catch (const ExcelBusy&) {
        return std::nullopt;  // the preview shows the file instead
    }
    if (!wb) {
        return std::nullopt;
    }
    Disp sheets = getObject(wb.get(), L"Worksheets");
    Disp ws;
    try {
        if (sheet.empty()) {
            Var first(1);
            ws = getObject(sheets.get(), L"Item", {&first.v});
        } else {
            Var name(widen(sheet));
            ws = getObject(sheets.get(), L"Item", {&name.v});
        }
    } catch (const CalcError&) {
        throw CalcError("no existe la hoja «" + sheet + "» en el libro abierto en Excel");
    }
    Disp used = getObject(ws.get(), L"UsedRange");
    const int row0 = intProperty(used.get(), L"Row");
    const int col0 = intProperty(used.get(), L"Column");
    Disp rowsObj = getObject(used.get(), L"Rows");
    Disp colsObj = getObject(used.get(), L"Columns");
    int nRows = std::min(intProperty(rowsObj.get(), L"Count"), std::max(0, maxRows - row0 + 1));
    int nCols = std::min(intProperty(colsObj.get(), L"Count"), std::max(0, maxCols - col0 + 1));
    SheetPreview out;
    if (nRows <= 0 || nCols <= 0) {
        return out;
    }
    // Only the part that is shown: Range(Cells(row0, col0), Cells(row0 + nRows - 1, col0 + nCols - 1))
    std::wstring addr = widen(columnName(col0) + std::to_string(row0) + ":" + columnName(col0 + nCols - 1) +
                              std::to_string(row0 + nRows - 1));
    Var a(addr);
    Disp range = getObject(ws.get(), L"Range", {&a.v});
    Var values;
    invoke(range.get(), L"Value2", DISPATCH_PROPERTYGET, {}, &values.v);
    auto put = [&](int r, int c, VARIANT& v) {
        std::string text = displayText(v);
        if (!text.empty()) {
            out.cells[{r, c}] = text;
            out.rows = std::max(out.rows, r);
            out.cols = std::max(out.cols, c);
        }
    };
    if ((values.v.vt & VT_ARRAY) && values.v.parray && SafeArrayGetDim(values.v.parray) == 2) {
        SAFEARRAY* arr = values.v.parray;
        LONG r1, r2, c1, c2;
        SafeArrayGetLBound(arr, 1, &r1);
        SafeArrayGetUBound(arr, 1, &r2);
        SafeArrayGetLBound(arr, 2, &c1);
        SafeArrayGetUBound(arr, 2, &c2);
        for (LONG r = r1; r <= r2; r++) {
            for (LONG c = c1; c <= c2; c++) {
                LONG idx[2] = {r, c};
                VARIANT v;
                VariantInit(&v);
                if (SUCCEEDED(SafeArrayGetElement(arr, idx, &v))) {
                    put(row0 + static_cast<int>(r - r1), col0 + static_cast<int>(c - c1), v);
                }
                VariantClear(&v);
            }
        }
    } else {
        put(row0, col0, values.v);  // a single cell
    }
    return out;
}

#else  // not Windows: there is no Excel to talk to, the files are used

bool isOpen(const fs::path&) { return false; }
std::optional<double> read(const fs::path&, const CellRef&) { return std::nullopt; }
bool write(const fs::path&, const CellRef&, double) { return false; }
std::optional<std::vector<std::string>> sheetNames(const fs::path&) { return std::nullopt; }
std::optional<SheetPreview> preview(const fs::path&, const std::string&, int, int) { return std::nullopt; }

#endif

}  // namespace xoj::calc::excelapp
