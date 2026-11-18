/*
 * Xournal++ Calc
 *
 * Handwritten formula → LaTeX with a local ONNX model (pix2text-mfr: a TrOCR encoder-decoder trained on printed and
 * handwritten formulas). The model files are looked for in
 *     <config>/modelos/pix2text-mfr/   (the user's own copy, if any)
 *     <data>/models/pix2text-mfr/      (installed with the program)
 * and must contain encoder_model.onnx, decoder_model.onnx and tokenizer.json.
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <glib.h>

#include "config-features.h"
#include "util/PathUtil.h"
#include "util/StringUtils.h"

#include "InkRecognizer.h"
#include "filesystem.h"  // for path

#ifdef ENABLE_INK_MATH
// ONNX Runtime is loaded at run time (not linked): the program starts even if its DLL is missing or broken, and
// on Windows the DLL next to the executable is used instead of the older one of Windows ML in System32
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace xoj::calc::ink {

namespace {

// ---------------------------------------------------------------------------------------------------------------
// Minimal JSON reader (enough for tokenizer.json and config.json)

struct Json;
using JsonObject = std::vector<std::pair<std::string, Json>>;
using JsonArray = std::vector<Json>;

struct Json {
    std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<JsonArray>, std::shared_ptr<JsonObject>> v;

    const Json* get(const std::string& key) const {
        if (auto* o = std::get_if<std::shared_ptr<JsonObject>>(&v)) {
            for (const auto& [k, val]: **o) {
                if (k == key) {
                    return &val;
                }
            }
        }
        return nullptr;
    }
    const std::string* str() const { return std::get_if<std::string>(&v); }
    const double* num() const { return std::get_if<double>(&v); }
    const JsonArray* arr() const {
        auto* a = std::get_if<std::shared_ptr<JsonArray>>(&v);
        return a ? a->get() : nullptr;
    }
    const JsonObject* obj() const {
        auto* o = std::get_if<std::shared_ptr<JsonObject>>(&v);
        return o ? o->get() : nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& s): s(s) {}
    Json parse() {
        Json j = value();
        return j;
    }

private:
    const std::string& s;
    size_t p = 0;

    void ws() {
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) {
            p++;
        }
    }
    [[noreturn]] void fail() { throw std::runtime_error("JSON inválido en la posición " + std::to_string(p)); }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    uint32_t hex4() {
        if (p + 4 > s.size()) {
            fail();
        }
        uint32_t v = static_cast<uint32_t>(std::stoul(s.substr(p, 4), nullptr, 16));
        p += 4;
        return v;
    }

    std::string string() {
        if (s[p] != '"') {
            fail();
        }
        p++;
        std::string out;
        while (p < s.size() && s[p] != '"') {
            char c = s[p++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (p >= s.size()) {
                fail();
            }
            char e = s[p++];
            switch (e) {
                case 'n':
                    out += '\n';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'u': {
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 6 <= s.size() && s[p] == '\\' && s[p + 1] == 'u') {
                        p += 2;
                        uint32_t lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    out += e;
            }
        }
        p++;
        return out;
    }

    Json value() {
        ws();
        if (p >= s.size()) {
            fail();
        }
        char c = s[p];
        Json j;
        if (c == '{') {
            p++;
            auto o = std::make_shared<JsonObject>();
            ws();
            if (s[p] == '}') {
                p++;
            } else {
                while (true) {
                    ws();
                    std::string k = string();
                    ws();
                    if (s[p] != ':') {
                        fail();
                    }
                    p++;
                    o->emplace_back(std::move(k), value());
                    ws();
                    if (s[p] == ',') {
                        p++;
                        continue;
                    }
                    if (s[p] == '}') {
                        p++;
                        break;
                    }
                    fail();
                }
            }
            j.v = o;
        } else if (c == '[') {
            p++;
            auto a = std::make_shared<JsonArray>();
            ws();
            if (s[p] == ']') {
                p++;
            } else {
                while (true) {
                    a->push_back(value());
                    ws();
                    if (s[p] == ',') {
                        p++;
                        continue;
                    }
                    if (s[p] == ']') {
                        p++;
                        break;
                    }
                    fail();
                }
            }
            j.v = a;
        } else if (c == '"') {
            j.v = string();
        } else if (s.compare(p, 4, "true") == 0) {
            p += 4;
            j.v = true;
        } else if (s.compare(p, 5, "false") == 0) {
            p += 5;
            j.v = false;
        } else if (s.compare(p, 4, "null") == 0) {
            p += 4;
            j.v = nullptr;
        } else {
            size_t start = p;
            while (p < s.size() && (std::isdigit(static_cast<unsigned char>(s[p])) || s[p] == '-' || s[p] == '+' ||
                                    s[p] == '.' || s[p] == 'e' || s[p] == 'E')) {
                p++;
            }
            if (start == p) {
                fail();
            }
            j.v = std::strtod(s.substr(start, p - start).c_str(), nullptr);
        }
        return j;
    }
};

std::string readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// ---------------------------------------------------------------------------------------------------------------
// Tokenizer (decoding only)

class Detokenizer {
public:
    bool load(const fs::path& tokenizerJson, std::string& error) {
        try {
            std::string text = readFile(tokenizerJson);
            if (text.empty()) {
                error = "no se pudo leer " + std::string(char_cast(tokenizerJson.u8string()));
                return false;
            }
            Json root = JsonParser(text).parse();
            const Json* model = root.get("model");
            const Json* vocab = model ? model->get("vocab") : nullptr;
            if (!vocab) {
                error = "tokenizer.json sin vocabulario";
                return false;
            }
            if (const auto* o = vocab->obj()) {
                for (const auto& [tok, id]: *o) {
                    if (const double* n = id.num()) {
                        set(static_cast<int>(*n), tok);
                    }
                }
            } else if (const auto* a = vocab->arr()) {  // Unigram: [[token, score], ...]
                for (size_t i = 0; i < a->size(); i++) {
                    if (const auto* pair = (*a)[i].arr(); pair && !pair->empty() && (*pair)[0].str()) {
                        set(static_cast<int>(i), *(*pair)[0].str());
                    }
                }
            }
            if (const Json* added = root.get("added_tokens"); added && added->arr()) {
                for (const auto& t: *added->arr()) {
                    const double* id = t.get("id") ? t.get("id")->num() : nullptr;
                    const std::string* content = t.get("content") ? t.get("content")->str() : nullptr;
                    if (id && content) {
                        set(static_cast<int>(*id), *content);
                        const Json* sp = t.get("special");
                        if (sp && std::holds_alternative<bool>(sp->v) && std::get<bool>(sp->v)) {
                            special.push_back(static_cast<int>(*id));
                        }
                    }
                }
            }
            if (const Json* dec = root.get("decoder")) {
                std::string kind = dec->get("type") && dec->get("type")->str() ? *dec->get("type")->str() : "";
                std::string all = kind;
                if (const Json* decs = dec->get("decoders"); decs && decs->arr()) {
                    for (const auto& d: *decs->arr()) {
                        if (d.get("type") && d.get("type")->str()) {
                            all += "," + *d.get("type")->str();
                        }
                    }
                }
                byteLevel = all.find("ByteLevel") != std::string::npos;
                wordPiece = all.find("WordPiece") != std::string::npos;
                metaspace = all.find("Metaspace") != std::string::npos;
            }
            return !tokens.empty();
        } catch (const std::exception& e) {
            error = std::string("tokenizer.json: ") + e.what();
            return false;
        }
    }

    std::string decode(const std::vector<int64_t>& ids) const {
        std::string joined;
        for (int64_t id: ids) {
            if (id < 0 || static_cast<size_t>(id) >= tokens.size()) {
                continue;
            }
            if (std::find(special.begin(), special.end(), id) != special.end()) {
                continue;
            }
            const std::string& t = tokens[static_cast<size_t>(id)];
            if (t == "<s>" || t == "</s>" || t == "<pad>" || t == "<unk>" || t == "[CLS]" || t == "[SEP]" ||
                t == "[PAD]") {
                continue;
            }
            if (wordPiece) {
                if (t.rfind("##", 0) == 0) {
                    joined += t.substr(2);
                } else {
                    if (!joined.empty()) {
                        joined += ' ';
                    }
                    joined += t;
                }
            } else {
                joined += t;
            }
        }
        if (byteLevel) {
            joined = byteLevelDecode(joined);
        }
        if (metaspace) {
            std::string out;
            const std::string meta = "▁";
            for (size_t i = 0; i < joined.size();) {
                if (joined.compare(i, meta.size(), meta) == 0) {
                    out += ' ';
                    i += meta.size();
                } else {
                    out += joined[i++];
                }
            }
            joined = out;
        }
        // trim
        size_t b = joined.find_first_not_of(' ');
        size_t e = joined.find_last_not_of(' ');
        return b == std::string::npos ? "" : joined.substr(b, e - b + 1);
    }

private:
    std::vector<std::string> tokens;
    std::vector<int> special;
    bool byteLevel = false;
    bool wordPiece = false;
    bool metaspace = false;

    void set(int id, const std::string& t) {
        if (id < 0) {
            return;
        }
        if (static_cast<size_t>(id) >= tokens.size()) {
            tokens.resize(static_cast<size_t>(id) + 1);
        }
        tokens[static_cast<size_t>(id)] = t;
    }

    /// GPT-2 byte-level BPE: each byte is shown as a printable code point; map them back
    static std::string byteLevelDecode(const std::string& s) {
        static std::map<uint32_t, unsigned char> table = [] {
            std::map<uint32_t, unsigned char> m;
            std::vector<int> bs;
            for (int b = '!'; b <= '~'; b++) bs.push_back(b);
            for (int b = 0xA1; b <= 0xAC; b++) bs.push_back(b);
            for (int b = 0xAE; b <= 0xFF; b++) bs.push_back(b);
            std::vector<uint32_t> cs(bs.begin(), bs.end());
            uint32_t n = 0;
            for (int b = 0; b < 256; b++) {
                if (std::find(bs.begin(), bs.end(), b) == bs.end()) {
                    bs.push_back(b);
                    cs.push_back(256 + n++);
                }
            }
            for (size_t i = 0; i < bs.size(); i++) {
                m[cs[i]] = static_cast<unsigned char>(bs[i]);
            }
            return m;
        }();
        std::string out;
        for (size_t i = 0; i < s.size();) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            uint32_t cp;
            size_t len;
            if (c < 0x80) {
                cp = c;
                len = 1;
            } else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
                cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
                len = 2;
            } else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
                cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                     (static_cast<unsigned char>(s[i + 2]) & 0x3F);
                len = 3;
            } else {
                out += s[i++];
                continue;
            }
            auto it = table.find(cp);
            if (it != table.end()) {
                out += static_cast<char>(it->second);
            } else {
                out += s.substr(i, len);
            }
            i += len;
        }
        return out;
    }
};

fs::path findModelDir() {
    std::vector<fs::path> candidates;
    if (const char* env = g_getenv("JACKMATH_MODELOS")) {
        fs::path base(reinterpret_cast<const char8_t*>(env));
        candidates.emplace_back(base / "pix2text-mfr");
        candidates.emplace_back(base);
    }
    candidates.push_back(Util::getConfigSubfolder("modelos") / "pix2text-mfr");
    candidates.push_back(Util::getDataPath() / "models" / "pix2text-mfr");
    for (const auto& c: candidates) {
        std::error_code ec;
        if (fs::exists(c / "encoder_model.onnx", ec) && fs::exists(c / "decoder_model.onnx", ec)) {
            return c;
        }
    }
    return {};
}

double envDouble(const char* name, double def) {
    const char* v = g_getenv(name);
    if (!v || !*v) {
        return def;
    }
    return g_ascii_strtod(v, nullptr);
}

}  // namespace

#ifdef ENABLE_INK_MATH

namespace {

/// Load the ONNX Runtime library and initialize the C++ API. Empty string on success, else the reason
std::string loadOnnxRuntime() {
    using GetApiBaseFn = const OrtApiBase*(ORT_API_CALL*)();
    GetApiBaseFn getApiBase = nullptr;
    std::string tried;
#ifdef _WIN32
    std::vector<fs::path> paths{Util::getExePath() / "onnxruntime.dll"};
    for (const auto& p: paths) {
        HMODULE h = LoadLibraryExW(p.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (h) {
            getApiBase = reinterpret_cast<GetApiBaseFn>(reinterpret_cast<void*>(GetProcAddress(h, "OrtGetApiBase")));
            if (getApiBase) {
                break;
            }
        }
        tried += std::string(char_cast(p.u8string().c_str())) + " (error " + std::to_string(GetLastError()) + ") ";
    }
#else
    for (const char* name: {"libonnxruntime.so.1", "libonnxruntime.so"}) {
        if (void* h = dlopen(name, RTLD_NOW | RTLD_LOCAL)) {
            getApiBase = reinterpret_cast<GetApiBaseFn>(dlsym(h, "OrtGetApiBase"));
            if (getApiBase) {
                break;
            }
        }
        tried += std::string(name) + " ";
    }
#endif
    if (!getApiBase) {
        return "No se pudo cargar ONNX Runtime: " + tried;
    }
    const OrtApiBase* base = getApiBase();
    const OrtApi* api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
    if (!api) {
        return std::string("La versión de ONNX Runtime instalada (") + (base ? base->GetVersionString() : "?") +
               ") es más vieja que la que necesita el programa";
    }
    Ort::InitApi(api);
    g_message("Ink: ONNX Runtime %s", base->GetVersionString());
    return "";
}

/// The model, loaded on first use and kept
class MathModel {
public:
    static MathModel* get(std::string& error) {
        static std::mutex m;
        static std::unique_ptr<MathModel> instance;
        static std::string loadError;
        std::lock_guard lock(m);
        if (!instance && loadError.empty()) {
            auto mm = std::make_unique<MathModel>();
            if (mm->load(loadError)) {
                instance = std::move(mm);
            }
        }
        if (!instance) {
            error = loadError;
        }
        return instance.get();
    }

    bool load(std::string& error) {
        static const std::string ortError = loadOnnxRuntime();
        if (!ortError.empty()) {
            error = ortError;
            return false;
        }
        fs::path dir = findModelDir();
        if (dir.empty()) {
            error = "No se encontró el modelo de fórmulas (carpeta models/pix2text-mfr junto al programa)";
            return false;
        }
        if (!detok.load(dir / "tokenizer.json", error)) {
            return false;
        }
        readConfig(dir);
        try {
            env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "jackmath-ink");
            Ort::SessionOptions so;
            so.SetIntraOpNumThreads(static_cast<int>(std::max(1u, std::min(4u, std::thread::hardware_concurrency()))));
            so.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            encoder = std::make_unique<Ort::Session>(*env, (dir / "encoder_model.onnx").c_str(), so);
            decoder = std::make_unique<Ort::Session>(*env, (dir / "decoder_model.onnx").c_str(), so);
            Ort::AllocatorWithDefaultOptions alloc;
            for (size_t i = 0; i < encoder->GetInputCount(); i++) {
                encIn.push_back(encoder->GetInputNameAllocated(i, alloc).get());
            }
            for (size_t i = 0; i < encoder->GetOutputCount(); i++) {
                encOut.push_back(encoder->GetOutputNameAllocated(i, alloc).get());
            }
            for (size_t i = 0; i < decoder->GetInputCount(); i++) {
                decIn.push_back(decoder->GetInputNameAllocated(i, alloc).get());
            }
            for (size_t i = 0; i < decoder->GetOutputCount(); i++) {
                decOut.push_back(decoder->GetOutputNameAllocated(i, alloc).get());
            }
            g_message("Ink: modelo de fórmulas cargado de %s (encoder %zu→%zu, decoder %zu→%zu)",
                      char_cast(dir.u8string().c_str()), encIn.size(), encOut.size(), decIn.size(), decOut.size());
            for (auto& n: decIn) {
                g_debug("Ink: entrada del decoder: %s", n.c_str());
            }
        } catch (const std::exception& e) {  // Ort::Exception, and also out of memory or a damaged model
            error = std::string("Error al cargar el modelo: ") + e.what();
            return false;
        }
        return true;
    }

    std::optional<std::string> recognizeLine(const std::vector<InkStroke>& strokes, std::string& error) {
        const int size = imageSize;
        std::string debugPng;
        if (g_getenv("JACKMATH_INK_DEBUG")) {
            debugPng = std::string(char_cast(
                    (fs::temp_directory_path() / ("jackmath-ink-" + std::to_string(debugCounter++) + ".png")).u8string()));
        }
        auto gray = renderStrokes(strokes, size, size, envDouble("JACKMATH_INK_HEIGHT", 64),
                                  envDouble("JACKMATH_INK_PEN", 1.6), debugPng);
        std::vector<float> pixels(3 * static_cast<size_t>(size) * size);
        const size_t plane = static_cast<size_t>(size) * size;
        for (size_t i = 0; i < plane; i++) {
            float v = (static_cast<float>(gray[i]) / 255.0f - 0.5f) / 0.5f;
            pixels[i] = pixels[plane + i] = pixels[2 * plane + i] = v;
        }
        try {
            Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            std::array<int64_t, 4> shape{1, 3, size, size};
            Ort::Value img = Ort::Value::CreateTensor<float>(mem, pixels.data(), pixels.size(), shape.data(), 4);
            std::vector<const char*> inNames{encIn[0].c_str()};
            std::vector<const char*> outNames{encOut[0].c_str()};
            auto encResult = encoder->Run(Ort::RunOptions{nullptr}, inNames.data(), &img, 1, outNames.data(), 1);
            Ort::Value& hidden = encResult[0];
            auto hiddenShape = hidden.GetTensorTypeAndShapeInfo().GetShape();
            int64_t encLen = hiddenShape.size() >= 2 ? hiddenShape[1] : 1;
            std::vector<int64_t> encMask(static_cast<size_t>(encLen), 1);
            std::array<int64_t, 2> maskShape{1, encLen};

            std::vector<int64_t> ids{decoderStart};
            std::vector<int64_t> generated;
            const char* logitsName = decOut[0].c_str();
            for (const auto& n: decOut) {
                if (n == "logits") {
                    logitsName = n.c_str();
                }
            }
            for (int step = 0; step < maxTokens; step++) {
                std::array<int64_t, 2> idShape{1, static_cast<int64_t>(ids.size())};
                std::vector<int64_t> attn(ids.size(), 1);
                std::vector<Ort::Value> inputs;
                std::vector<const char*> names;
                for (const auto& n: decIn) {
                    if (n.find("encoder_hidden_states") != std::string::npos) {
                        inputs.push_back(Ort::Value::CreateTensor<float>(
                                mem, hidden.GetTensorMutableData<float>(),
                                hidden.GetTensorTypeAndShapeInfo().GetElementCount(), hiddenShape.data(),
                                hiddenShape.size()));
                    } else if (n.find("encoder_attention_mask") != std::string::npos) {
                        inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, encMask.data(), encMask.size(),
                                                                           maskShape.data(), 2));
                    } else if (n.find("input_ids") != std::string::npos) {
                        inputs.push_back(
                                Ort::Value::CreateTensor<int64_t>(mem, ids.data(), ids.size(), idShape.data(), 2));
                    } else if (n.find("attention_mask") != std::string::npos) {
                        inputs.push_back(
                                Ort::Value::CreateTensor<int64_t>(mem, attn.data(), attn.size(), idShape.data(), 2));
                    } else {
                        error = "El modelo pide una entrada desconocida: " + n;
                        return std::nullopt;
                    }
                    names.push_back(n.c_str());
                }
                auto out = decoder->Run(Ort::RunOptions{nullptr}, names.data(), inputs.data(), inputs.size(),
                                        &logitsName, 1);
                auto lshape = out[0].GetTensorTypeAndShapeInfo().GetShape();
                int64_t vocab = lshape.back();
                int64_t seq = lshape.size() >= 3 ? lshape[1] : 1;
                const float* logits = out[0].GetTensorData<float>() + (seq - 1) * vocab;
                int64_t best = std::max_element(logits, logits + vocab) - logits;
                if (best == eos) {
                    break;
                }
                ids.push_back(best);
                generated.push_back(best);
            }
            std::string latex = detok.decode(generated);
            g_message("Ink: fórmula reconocida: %s", latex.c_str());
            return latex;
        } catch (const std::exception& e) {  // Ort::Exception, and also out of memory or a damaged model
            error = std::string("Error del modelo: ") + e.what();
            return std::nullopt;
        }
    }

private:
    std::unique_ptr<Ort::Env> env;
    std::unique_ptr<Ort::Session> encoder;
    std::unique_ptr<Ort::Session> decoder;
    std::vector<std::string> encIn, encOut, decIn, decOut;
    Detokenizer detok;
    int imageSize = 384;
    int64_t decoderStart = 2;
    int64_t eos = 2;
    int maxTokens = 256;
    int debugCounter = 0;

    void readConfig(const fs::path& dir) {
        try {
            std::string text = readFile(dir / "config.json");
            if (!text.empty()) {
                Json root = JsonParser(text).parse();
                if (const Json* v = root.get("decoder_start_token_id"); v && v->num()) {
                    decoderStart = static_cast<int64_t>(*v->num());
                }
                if (const Json* v = root.get("eos_token_id"); v && v->num()) {
                    eos = static_cast<int64_t>(*v->num());
                }
                if (const Json* enc = root.get("encoder")) {
                    if (const Json* v = enc->get("image_size"); v && v->num()) {
                        if (double d = *v->num(); d >= 32 && d <= 2048) {
                            imageSize = static_cast<int>(d);
                        }
                    }
                }
            }
            text = readFile(dir / "generation_config.json");
            if (!text.empty()) {
                Json root = JsonParser(text).parse();
                if (const Json* v = root.get("decoder_start_token_id"); v && v->num()) {
                    decoderStart = static_cast<int64_t>(*v->num());
                }
                if (const Json* v = root.get("eos_token_id"); v && v->num()) {
                    eos = static_cast<int64_t>(*v->num());
                }
            }
        } catch (const std::exception& e) {
            g_warning("Ink: config.json: %s", e.what());
        }
    }
};

}  // namespace

std::optional<std::string> recognizeMath(const std::vector<InkStroke>& strokes, std::string& error) {
    MathModel* model = MathModel::get(error);
    if (!model) {
        return std::nullopt;
    }
    // Writing with a touchpad gives steps: smooth at about 1/25 of the height of a character
    std::vector<InkStroke> smooth = smoothStrokes(strokes, std::max(boundingBox(strokes).height(), 1.0) / 50.0);
    std::string result;
    for (const auto& line: splitLines(smooth)) {
        auto latex = model->recognizeLine(line, error);
        if (!latex) {
            return std::nullopt;
        }
        if (latex->empty()) {
            continue;
        }
        if (!result.empty()) {
            result += " \\\\ ";
        }
        result += *latex;
    }
    if (result.empty()) {
        error = "No se reconoció ninguna fórmula";
        return std::nullopt;
    }
    return result;
}

#else

std::optional<std::string> recognizeMath(const std::vector<InkStroke>&, std::string& error) {
    error = "Este programa se compiló sin ONNX Runtime: no puede reconocer fórmulas";
    return std::nullopt;
}

#endif

}  // namespace xoj::calc::ink
