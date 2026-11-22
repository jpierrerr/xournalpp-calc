#include "SpellChecker.h"

#include <fstream>        // for ifstream, ofstream
#include <unordered_map>  // for unordered_map
#include <unordered_set>  // for unordered_set

#include <glib.h>  // for g_utf8_next_char, g_unichar_isalpha

#include "util/PathUtil.h"  // for getConfigFile

#include "config-features.h"  // for ENABLE_SPELLCHECK

#ifdef ENABLE_SPELLCHECK
#include <hunspell.h>  // for Hunhandle, Hunspell_create, Hunspell_spell, Hunspell_suggest
#endif

namespace xoj::spell {
namespace {

constexpr auto PERSONAL_FILE = "diccionario-personal.txt";

fs::path dictionaryDir;
bool loadTried = false;
std::unordered_map<std::string, bool> cache;
std::unordered_set<std::string> accepted;  // personal dictionary and ignored words

#ifdef ENABLE_SPELLCHECK
Hunhandle* handle = nullptr;

auto pathString(fs::path p) -> std::string {
    std::error_code ec;
    auto full = fs::absolute(p, ec);
    auto s = (ec ? p : full).make_preferred().u8string();
    std::string res(s.begin(), s.end());
#ifdef _WIN32
    // Hunspell opens UTF-8 paths on Windows only with this prefix
    res = "\\\\?\\" + res;
#endif
    return res;
}
#endif

void load() {
    if (loadTried) {
        return;
    }
    loadTried = true;
#ifdef ENABLE_SPELLCHECK
    std::error_code ec;
    auto aff = dictionaryDir / "es.aff";
    auto dic = dictionaryDir / "es.dic";
    if (dictionaryDir.empty() || !fs::exists(aff, ec) || !fs::exists(dic, ec)) {
        g_warning("xournalpp-calc: no spelling dictionary in %s", dictionaryDir.u8string().c_str());
        return;
    }
    handle = Hunspell_create(pathString(aff).c_str(), pathString(dic).c_str());
#endif
    std::ifstream in(Util::getConfigFile(PERSONAL_FILE));
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            accepted.insert(line);
        }
    }
}

auto isCorrect(const std::string& word) -> bool {
#ifdef ENABLE_SPELLCHECK
    if (accepted.count(word)) {
        return true;
    }
    if (auto it = cache.find(word); it != cache.end()) {
        return it->second;
    }
    bool ok = Hunspell_spell(handle, word.c_str()) != 0;
    cache.emplace(word, ok);
    return ok;
#else
    return true;
#endif
}

/// Words with digits, "_" or a capital after the first letter (units, symbols, acronyms: kN, MPa, A_s, x2) are not
/// checked, nor single letters
auto worthChecking(const std::string& text, int start, int end) -> bool {
    if (start > 0) {
        gunichar before = g_utf8_get_char(g_utf8_find_prev_char(text.c_str(), text.c_str() + start));
        if (g_unichar_isdigit(before) || before == '_' || before == '^' || before == '\\') {
            return false;
        }
    }
    if (end < static_cast<int>(text.size())) {
        gunichar after = g_utf8_get_char(text.c_str() + end);
        if (g_unichar_isdigit(after) || after == '_' || after == '^') {
            return false;
        }
    }
    int letters = 0;
    for (const char* p = text.c_str() + start; p < text.c_str() + end; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (letters > 0 && g_unichar_isupper(c)) {
            return false;
        }
        letters++;
    }
    return letters > 1;
}

}  // namespace

void setDictionaryDir(const fs::path& dir) { dictionaryDir = dir; }

bool available() {
    load();
#ifdef ENABLE_SPELLCHECK
    return handle != nullptr;
#else
    return false;
#endif
}

std::vector<std::pair<int, int>> misspelled(const std::string& text) {
    std::vector<std::pair<int, int>> res;
    if (!available() || !g_utf8_validate(text.c_str(), static_cast<gssize>(text.size()), nullptr)) {
        return res;
    }
    const char* base = text.c_str();
    const char* p = base;
    while (*p) {
        if (!g_unichar_isalpha(g_utf8_get_char(p))) {
            p = g_utf8_next_char(p);
            continue;
        }
        const char* s = p;
        while (*p && g_unichar_isalpha(g_utf8_get_char(p))) {
            p = g_utf8_next_char(p);
        }
        int start = static_cast<int>(s - base);
        int end = static_cast<int>(p - base);
        if (worthChecking(text, start, end) && !isCorrect(text.substr(start, end - start))) {
            res.emplace_back(start, end);
        }
    }
    return res;
}

std::vector<std::string> suggest(const std::string& word, size_t max) {
    std::vector<std::string> res;
#ifdef ENABLE_SPELLCHECK
    if (!available()) {
        return res;
    }
    char** list = nullptr;
    int n = Hunspell_suggest(handle, &list, word.c_str());
    for (int i = 0; i < n && res.size() < max; i++) {
        res.emplace_back(list[i]);
    }
    Hunspell_free_list(handle, &list, n);
#endif
    return res;
}

void addToDictionary(const std::string& word) {
    load();
    if (accepted.insert(word).second) {
        std::ofstream out(Util::getConfigFile(PERSONAL_FILE), std::ios::app);
        out << word << "\n";
    }
}

void ignore(const std::string& word) {
    load();
    accepted.insert(word);
}

}  // namespace xoj::spell
