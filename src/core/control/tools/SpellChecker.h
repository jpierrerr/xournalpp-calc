/*
 * Xournal JackMath (xournalpp-calc)
 *
 * Spell checking of texts with Hunspell and the Spanish dictionary in ui/dictionaries
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>   // for string
#include <utility>  // for pair
#include <vector>   // for vector

#include "filesystem.h"  // for path

namespace xoj::spell {

/// Folder with es.aff and es.dic (the dictionary is loaded the first time a word is checked)
void setDictionaryDir(const fs::path& dir);

/// Whether a dictionary could be loaded (false when the program was built without Hunspell)
bool available();

/// The misspelled words of a text, as [start, end) byte ranges
std::vector<std::pair<int, int>> misspelled(const std::string& text);

/// Corrections for a word, best first
std::vector<std::string> suggest(const std::string& word, size_t max = 6);

/// Accept the word from now on, also in later sessions (personal dictionary in the configuration folder)
void addToDictionary(const std::string& word);

/// Accept the word until the program is closed
void ignore(const std::string& word);

}  // namespace xoj::spell
