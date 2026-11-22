/*
 * Xournal++ Calc
 *
 * Two-dimensional typesetting of the calculation regions, as in the equation editor of a word processor or the
 * "natural display" of a scientific calculator: a/b is drawn as a fraction with a horizontal bar, x^(2) with the 2
 * raised, RAIZ(x) with a radical sign.
 *
 * What is written stays plain text (the engine reads it as before): the parentheses that only group a numerator, a
 * denominator, an exponent or the argument of a root are not drawn. While a region is edited, the formula is drawn
 * from the text, with the cursor moving between the boxes (numerator, denominator, exponent...).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <cairo.h>

#include "util/Color.h"

namespace xoj::calc {
struct Environment;
}

namespace xoj::calc::math {

struct Node;

/// Where the cursor is drawn for a position in the text (coordinates of the Text element)
struct Caret {
    double x = 0;
    double top = 0;
    double bottom = 0;
    int depth = -1;  ///< how deep in the formula the position is (the deepest box wins)
};

/// A box of the formula that can be filled (numerator, denominator, exponent, argument of a root...)
struct Slot {
    size_t start = 0;             ///< position of the start of its content in the text (bytes)
    size_t end = 0;               ///< position of the end of its content
    size_t outerStart = 0;        ///< where the structure (fraction, power...) starts
    size_t outerEnd = 0;          ///< where it ends (after its last hidden character)
    bool structureEmpty = false;  ///< every box of the structure is empty
    bool exponent = false;
    /// While the box is empty, Backspace at its start deletes this (e.g. "^()" or "/()"); npos: nothing
    size_t delStart = std::string::npos, delEnd = std::string::npos;
};

struct Style {
    std::string fontName;  ///< Pango font description (family and style)
    double fontSize = 12;
    double firstBaseline = 0;  ///< baseline of the first line, from the top of the text
    double lineHeight = 0;     ///< pitch of the lines (alignment grid), 0: natural
    double naturalLineHeight = 0;
};

/**
 * A laid out calculation region (all its lines)
 */
class Formula {
public:
    /// The formula of a text being edited (positions refer to text)
    static std::unique_ptr<Formula> forEditing(const std::string& text, const Style& style);
    /// The formula of a region as displayed: structuredMarkup lines (see below), separated by '\n'
    static std::unique_ptr<Formula> forDisplay(const std::string& smu, const Style& style);

    ~Formula();

    double width() const { return w; }
    double top() const { return yTop; }
    double bottom() const { return yBottom; }
    double lastBaseline() const { return baselines.empty() ? 0 : baselines.back(); }

    /// Draw with the current source color of cr. Edit mode: selection [selStart, selEnd), empty boxes, the box of
    /// the cursor highlighted
    void draw(cairo_t* cr, size_t cursor = std::string::npos, size_t selStart = 0, size_t selEnd = 0,
              Color selectionColor = Color(0x80a0c0ff)) const;

    // Edit mode only
    bool isStop(size_t pos) const;
    Caret caretAt(size_t pos) const;
    /// The closest position where the cursor can stand
    size_t nearestStop(size_t pos) const;
    /// Next / previous cursor position (dir = +1 / -1), skipping the hidden characters
    size_t step(size_t pos, int dir) const;
    /// The position under a point
    size_t hit(double x, double y) const;
    /// Position above (dir = -1) or below (dir = +1), if there is one
    std::optional<size_t> vertical(size_t pos, int dir) const;
    /// The innermost box that contains pos (start <= pos <= end)
    std::optional<Slot> slotAround(size_t pos) const;
    /// The boxes, in the order of the text
    const std::vector<Slot>& slots() const { return slotList; }
    /// Whether the character starting at pos is drawn (not a hidden parenthesis or operator)
    bool visibleByte(size_t pos) const;

private:
    Formula() = default;
    void layoutLines(const Style& style);
    void registerCarets(const Node& n, int depth);
    void addCaret(size_t pos, const Caret& c);

    std::string source;
    std::vector<std::unique_ptr<Node>> lines;
    std::vector<size_t> lineStart;  ///< edit mode: offset of each line
    std::vector<double> baselines;
    double w = 0, yTop = 0, yBottom = 0;
    bool editing = false;
    std::vector<Caret> carets;  ///< edit mode: for each byte position (depth -1: not a stop)
    std::vector<bool> visible;  ///< edit mode: for each byte, whether it is drawn
    std::vector<Slot> slotList;
    double em = 12;
};

/**
 * Markup of a source line for display: Pango markup (see prettifyMarkup) with the structures marked by control
 * characters (fractions, powers, roots, tall parentheses). A line without any of them is plain Pango markup.
 */
std::string structuredMarkup(std::string_view source, const Environment* env);

/// Plain Pango markup of a structured markup (control characters removed), e.g. for a tooltip
std::string flattenMarkup(std::string_view smu);

/// Whether "/" typed at pos divides units ("5 kN/m": the unit stays on the line instead of making a fraction)
bool unitDivisionBefore(std::string_view text, size_t pos);

/// Whether the text has a structure (fraction, power, root) on some line
bool hasStructure(std::string_view text);

}  // namespace xoj::calc::math
