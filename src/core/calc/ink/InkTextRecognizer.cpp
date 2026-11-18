/*
 * Xournal++ Calc
 *
 * Handwritten text → text with the handwriting recognizer of Windows (Tablet PC ink automation objects, offline,
 * in the languages installed on the computer). The objects are used by late binding (IDispatch) because the MinGW
 * headers do not declare the recognizer interfaces.
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <string>
#include <vector>

#include <glib.h>

#include "InkRecognizer.h"

#ifdef _WIN32
#include <windows.h>
#include <oaidl.h>
#include <objbase.h>
#include <oleauto.h>
#endif

namespace xoj::calc::ink {

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

std::string hresultText(HRESULT hr) {
    char buf[32];
    g_snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::string narrow(const wchar_t* w, int len = -1) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(std::max(n, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    if (len == -1 && !s.empty() && s.back() == '\0') {
        s.pop_back();
    }
    return s;
}

/// Create a COM object from the first ProgID that works
Disp create(std::initializer_list<const wchar_t*> progIds, std::string& error) {
    HRESULT last = E_FAIL;
    for (const wchar_t* id: progIds) {
        CLSID clsid;
        HRESULT hr = CLSIDFromProgID(id, &clsid);
        if (FAILED(hr)) {
            last = hr;
            continue;
        }
        IDispatch* d = nullptr;
        hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IDispatch, reinterpret_cast<void**>(&d));
        if (SUCCEEDED(hr) && d) {
            return Disp(d);
        }
        last = hr;
    }
    error = "Windows no tiene el reconocedor de escritura (" + narrow(*progIds.begin()) + ": " + hresultText(last) +
            ")";
    return {};
}

/// Call a method or property of an IDispatch by name. args are in the natural order (they are reversed here)
HRESULT invoke(IDispatch* obj, const wchar_t* name, WORD flags, std::vector<VARIANT> args, VARIANT* result,
               std::string& error) {
    DISPID id;
    LPOLESTR n = const_cast<LPOLESTR>(name);
    HRESULT hr = obj->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, &id);
    if (FAILED(hr)) {
        error = "Reconocedor de Windows: no existe " + narrow(name) + " (" + hresultText(hr) + ")";
        return hr;
    }
    std::vector<VARIANT> rev(args.rbegin(), args.rend());
    DISPPARAMS params{};
    params.cArgs = static_cast<UINT>(rev.size());
    params.rgvarg = rev.empty() ? nullptr : rev.data();
    DISPID putId = DISPID_PROPERTYPUT;
    if (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) {
        params.cNamedArgs = 1;
        params.rgdispidNamedArgs = &putId;
    }
    EXCEPINFO ex{};
    UINT argErr = 0;
    hr = obj->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &params, result, &ex, &argErr);
    if (FAILED(hr)) {
        error = "Reconocedor de Windows: " + narrow(name) + " falló (" + hresultText(hr) + ")";
        if (ex.bstrDescription) {
            error += ": " + narrow(ex.bstrDescription);
        }
    }
    if (ex.bstrDescription) {
        SysFreeString(ex.bstrDescription);
    }
    if (ex.bstrSource) {
        SysFreeString(ex.bstrSource);
    }
    if (ex.bstrHelpFile) {
        SysFreeString(ex.bstrHelpFile);
    }
    return hr;
}

Disp dispatchOf(VARIANT& v) {
    if (v.vt == VT_DISPATCH && v.pdispVal) {
        IDispatch* d = v.pdispVal;
        d->AddRef();
        VariantClear(&v);
        return Disp(d);
    }
    VariantClear(&v);
    return {};
}

/// Points of the document are in pt (1/72 in); the ink uses HIMETRIC (0.01 mm)
constexpr double HIMETRIC_PER_PT = 2540.0 / 72.0;

/// COM is initialized once for the thread of the GUI
bool ensureCom() {
    static bool done = false;
    static bool ok = false;
    if (!done) {
        done = true;
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ok = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    return ok;
}

}  // namespace

std::optional<std::string> recognizeText(const std::vector<InkStroke>& strokes, std::string& error) {
    if (!ensureCom()) {
        error = "No se pudo iniciar COM";
        return std::nullopt;
    }
    Disp ink = create({L"msinkaut.InkObject.1", L"msinkaut.InkObject", L"msinkaut.InkDisp.1"}, error);
    if (!ink) {
        return std::nullopt;
    }
    InkBox box = boundingBox(strokes);
    for (const auto& s: smoothStrokes(strokes, std::max(box.height(), 1.0) / 50.0)) {
        if (s.points.empty()) {
            continue;
        }
        // Packet data: x0, y0, x1, y1, ... (the default packet description is X, Y)
        SAFEARRAY* sa = SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(2 * s.points.size()));
        if (!sa) {
            error = "Sin memoria";
            return std::nullopt;
        }
        LONG* data = nullptr;
        SafeArrayAccessData(sa, reinterpret_cast<void**>(&data));
        for (size_t i = 0; i < s.points.size(); i++) {
            data[2 * i] = static_cast<LONG>((s.points[i].x - box.x0) * HIMETRIC_PER_PT);
            data[2 * i + 1] = static_cast<LONG>((s.points[i].y - box.y0) * HIMETRIC_PER_PT);
        }
        SafeArrayUnaccessData(sa);
        VARIANT packets;
        VariantInit(&packets);
        packets.vt = VT_ARRAY | VT_I4;
        packets.parray = sa;
        VARIANT desc;
        VariantInit(&desc);  // VT_EMPTY: default description
        VARIANT stroke;
        VariantInit(&stroke);
        HRESULT hr = invoke(ink.get(), L"CreateStroke", DISPATCH_METHOD, {packets, desc}, &stroke, error);
        VariantClear(&packets);
        VariantClear(&stroke);
        if (FAILED(hr)) {
            return std::nullopt;
        }
    }
    VARIANT v;
    VariantInit(&v);
    if (FAILED(invoke(ink.get(), L"Strokes", DISPATCH_PROPERTYGET, {}, &v, error))) {
        return std::nullopt;
    }
    Disp inkStrokes = dispatchOf(v);
    if (!inkStrokes) {
        error = "Reconocedor de Windows: sin trazos";
        return std::nullopt;
    }

    Disp ctx = create({L"msinkaut.InkRecognizerContext.1", L"msinkaut.InkRecognizerContext"}, error);
    if (!ctx) {
        return std::nullopt;
    }
    VARIANT sv;
    VariantInit(&sv);
    sv.vt = VT_DISPATCH;
    sv.pdispVal = inkStrokes.get();
    if (FAILED(invoke(ctx.get(), L"Strokes", DISPATCH_PROPERTYPUTREF, {sv}, nullptr, error))) {
        // Some versions only accept a plain put
        std::string e2;
        if (FAILED(invoke(ctx.get(), L"Strokes", DISPATCH_PROPERTYPUT, {sv}, nullptr, e2))) {
            return std::nullopt;
        }
        error.clear();
    }
    LONG status = 0;
    VARIANT st;
    VariantInit(&st);
    st.vt = VT_BYREF | VT_I4;
    st.plVal = &status;
    VARIANT res;
    VariantInit(&res);
    if (FAILED(invoke(ctx.get(), L"Recognize", DISPATCH_METHOD, {st}, &res, error))) {
        return std::nullopt;
    }
    Disp result = dispatchOf(res);
    if (!result) {
        error = "No se reconoció texto";
        return std::nullopt;
    }
    VARIANT top;
    VariantInit(&top);
    if (FAILED(invoke(result.get(), L"TopString", DISPATCH_PROPERTYGET, {}, &top, error))) {
        return std::nullopt;
    }
    std::string text;
    if (top.vt == VT_BSTR && top.bstrVal) {
        text = narrow(top.bstrVal, static_cast<int>(SysStringLen(top.bstrVal)));
    }
    VariantClear(&top);
    // Line breaks come as "\r\n"
    std::string out;
    for (char c: text) {
        if (c != '\r') {
            out += c;
        }
    }
    g_message("Ink: texto reconocido: %s (estado %ld)", out.c_str(), static_cast<long>(status));
    if (out.empty()) {
        error = "No se reconoció texto";
        return std::nullopt;
    }
    return out;
}

#else

std::optional<std::string> recognizeText(const std::vector<InkStroke>&, std::string& error) {
    error = "El reconocimiento de texto a mano usa el reconocedor de Windows y no está disponible en este sistema";
    return std::nullopt;
}

#endif

}  // namespace xoj::calc::ink
