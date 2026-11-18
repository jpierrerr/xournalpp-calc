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
    static const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
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

/// The workbook open in Excel with that file, or nothing
Disp findWorkbook(const fs::path& file) {
    if (!comReady()) {
        return {};
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
        VariantChangeType(&count.v, &count.v, 0, VT_I4);
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
    } catch (const CalcError&) {
        return {};  // Excel is busy: the file is used instead
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

#else  // not Windows: there is no Excel to talk to, the files are used

bool isOpen(const fs::path&) { return false; }
std::optional<double> read(const fs::path&, const CellRef&) { return std::nullopt; }
bool write(const fs::path&, const CellRef&, double) { return false; }

#endif

}  // namespace xoj::calc::excelapp
