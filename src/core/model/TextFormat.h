/*
 * Xournal++ Calc
 *
 * Formatting of parts of a text (bold, italic, underline, colors, font...), as in a word processor
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <pango/pango.h>

namespace xoj::text {

enum class FormatKind : unsigned char {
    BOLD,        ///< "1"
    ITALIC,      ///< "1"
    UNDERLINE,   ///< "single", "double" or "wave"
    STRIKE,      ///< "1"
    SCRIPT,      ///< "sub" or "sup"
    FOREGROUND,  ///< "#rrggbb"
    BACKGROUND,  ///< "#rrggbb" (highlighter)
    FAMILY,      ///< font family name
    SIZE,        ///< size in points
};

/// One format on the characters [start, end) of the text (offsets in characters, not bytes)
struct FormatRun {
    int start;
    int end;
    FormatKind kind;
    std::string value;
};

using Formats = std::vector<FormatRun>;

/// Text form used in the .xopp file, and back
std::string serialize(const Formats& f);
Formats parse(const std::string& s);

/// Give [start, end) the format kind with this value (nullopt: remove it there)
void set(Formats& f, FormatKind kind, int start, int end, const std::optional<std::string>& value);
/// Remove every format from [start, end)
void clear(Formats& f, int start, int end);
/// The value of the format on all of [start, end), or nullopt if not all of it has the same value.
/// For an empty range (the cursor), the format of the character before it.
std::optional<std::string> valueAt(const Formats& f, FormatKind kind, int start, int end);

/// Keep the formats in place when characters are inserted at pos (text typed at the end of a run takes its format)
void onInsert(Formats& f, int pos, int count);
/// ... or when the characters [start, end) are deleted
void onDelete(Formats& f, int start, int end);

/// Add the formats to a Pango attribute list for the given text. baseSize: size of the font of the text (points)
void addToPango(const Formats& f, const std::string& text, PangoAttrList* list, double baseSize);

}  // namespace xoj::text
