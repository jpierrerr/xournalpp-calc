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
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>

#include "InkRecognizer.h"

#ifdef _WIN32
#include <io.h>  // close
#include <oaidl.h>
#include <objbase.h>
#include <oleauto.h>
#include <windows.h>
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


// ---------------------------------------------------------------------------------------------------------------------
// Printed text in a picture: the OCR of Windows (Windows.Media.Ocr, offline, in the languages of the user). It is a
// WinRT API, which MinGW cannot call directly: a small script of the PowerShell that comes with Windows calls it
// ---------------------------------------------------------------------------------------------------------------------

namespace {

const char* const OCR_SCRIPT = R"ps(
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
function Await($op, [Type]$type) {
    $task = $asTask.MakeGenericMethod($type).Invoke($null, @($op))
    $task.Wait(-1) | Out-Null
    $task.Result
}
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
$null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics, ContentType = WindowsRuntime]
$null = [Windows.Graphics.Imaging.SoftwareBitmap, Windows.Foundation, ContentType = WindowsRuntime]
$null = [Windows.Storage.Streams.IRandomAccessStream, Windows.Storage.Streams, ContentType = WindowsRuntime]
$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($env:JACKMATH_OCR_IMAGE)) ([Windows.Storage.StorageFile])
$stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
$decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
$bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
if (-not $engine) { [Console]::Error.WriteLine('NO_ENGINE'); exit 2 }
$result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
$stream.Dispose()
foreach ($line in $result.Lines) { [Console]::Out.WriteLine($line.Text) }
)ps";

std::wstring widenUtf8(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(std::max(n, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

/// Run PowerShell with a script, without a window. Its output (UTF-8), or nullopt with a message in error
std::optional<std::string> runPowerShell(const std::string& script, const std::wstring& imagePath, std::string& error) {
    std::wstring wscript = widenUtf8(script);
    gchar* b64 = g_base64_encode(reinterpret_cast<const guchar*>(wscript.data()), wscript.size() * sizeof(wchar_t));
    std::wstring cmd =
            L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " + widenUtf8(b64);
    g_free(b64);

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readOut = nullptr, writeOut = nullptr;
    if (!CreatePipe(&readOut, &writeOut, &sa, 0)) {
        error = "No se pudo iniciar el reconocimiento de texto";
        return std::nullopt;
    }
    SetHandleInformation(readOut, HANDLE_FLAG_INHERIT, 0);

    // The picture is passed in an environment variable of the child only
    SetEnvironmentVariableW(L"JACKMATH_OCR_IMAGE", imagePath.c_str());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeOut;
    si.hStdError = writeOut;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    SetEnvironmentVariableW(L"JACKMATH_OCR_IMAGE", nullptr);
    CloseHandle(writeOut);
    if (!ok) {
        CloseHandle(readOut);
        error = "No se pudo iniciar PowerShell para reconocer el texto";
        return std::nullopt;
    }

    std::string out;
    const ULONGLONG deadline = GetTickCount64() + 60000;
    bool finished = false;
    while (true) {
        DWORD avail = 0;
        while (PeekNamedPipe(readOut, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            char buf[4096];
            DWORD got = 0;
            if (!ReadFile(readOut, buf, std::min<DWORD>(avail, sizeof(buf)), &got, nullptr) || got == 0) {
                break;
            }
            out.append(buf, got);
        }
        if (finished) {
            break;
        }
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            finished = true;  // read what is left once more
            continue;
        }
        if (GetTickCount64() > deadline) {
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(readOut);
            error = "El reconocimiento de texto tardó demasiado";
            return std::nullopt;
        }
    }
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(readOut);
    if (code != 0) {
        g_warning("OCR: PowerShell exited with %lu: %s", static_cast<unsigned long>(code), out.c_str());
        error = out.find("NO_ENGINE") != std::string::npos ?
                        "Windows no tiene reconocimiento de texto (OCR) para tu idioma. Instálalo en Configuración › "
                        "Hora e idioma › Idioma." :
                        "Windows no pudo reconocer el texto de la imagen";
        return std::nullopt;
    }
    return out;
}

}  // namespace

std::optional<std::string> recognizeTextImage(const GrayImage& img, std::string& error) {
    if (img.w <= 0 || img.h <= 0) {
        error = "La imagen está vacía";
        return std::nullopt;
    }
    // Small text (a screenshot of a few lines) is recognized better when it is enlarged
    const int scale = img.h < 60 ? 3 : img.h < 200 ? 2 : 1;
    const int w = img.w * scale, h = img.h * scale;
    if (w > 9000 || h > 9000) {
        error = "La imagen es demasiado grande para reconocer el texto";
        return std::nullopt;
    }
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_surface_flush(surf);
    unsigned char* data = cairo_image_surface_get_data(surf);
    const int stride = cairo_image_surface_get_stride(surf);
    for (int y = 0; y < h; y++) {
        auto* row = reinterpret_cast<uint32_t*>(data + static_cast<size_t>(y) * stride);
        for (int x = 0; x < w; x++) {
            const unsigned v = img.px[static_cast<size_t>(y / scale) * img.w + x / scale];
            row[x] = 0xFF000000u | (v << 16) | (v << 8) | v;
        }
    }
    cairo_surface_mark_dirty(surf);
    gchar* tmpName = nullptr;
    gint fd = g_file_open_tmp("jackmath-ocr-XXXXXX.png", &tmpName, nullptr);
    if (fd < 0 || !tmpName) {
        cairo_surface_destroy(surf);
        error = "No se pudo crear un archivo temporal para la imagen";
        return std::nullopt;
    }
    close(fd);
    const bool written = cairo_surface_write_to_png(surf, tmpName) == CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(surf);
    std::optional<std::string> out;
    if (!written) {
        error = "No se pudo guardar la imagen para reconocerla";
    } else {
        gunichar2* wpath = g_utf8_to_utf16(tmpName, -1, nullptr, nullptr, nullptr);
        out = runPowerShell(OCR_SCRIPT, wpath ? std::wstring(reinterpret_cast<wchar_t*>(wpath)) : std::wstring(),
                            error);
        g_free(wpath);
    }
    g_remove(tmpName);
    g_free(tmpName);
    if (!out) {
        return std::nullopt;
    }
    // Lines without the carriage returns of Windows, and without empty lines at the end
    std::string text;
    for (char c: *out) {
        if (c != '\r') {
            text += c;
        }
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }
    if (text.empty()) {
        error = "No se encontró texto en la imagen";
        return std::nullopt;
    }
    return text;
}

#else

std::optional<std::string> recognizeTextImage(const GrayImage&, std::string& error) {
    error = "El reconocimiento de texto en imágenes solo está disponible en Windows";
    return std::nullopt;
}

std::optional<std::string> recognizeText(const std::vector<InkStroke>&, std::string& error) {
    error = "El reconocimiento de texto a mano usa el reconocedor de Windows y no está disponible en este sistema";
    return std::nullopt;
}

#endif

}  // namespace xoj::calc::ink
