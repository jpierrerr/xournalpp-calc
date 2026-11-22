/*
 * Xournal++
 *
 * A text element
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>
#include <string>  // for string
#include <vector>

#include <pango/pango.h>

#include "util/Point.h"
#include "util/raii/GObjectSPtr.h"

#include "AudioContent.h"
#include "Font.h"  // for XojFont
#include "RectangularElement.h"
#include "TextAlignment.h"
#include "TextFormat.h"

class ObjectInputStream;
class ObjectOutputStream;
class XojPdfRectangle;
namespace xoj::calc::math {
class Formula;
struct Style;
}  // namespace xoj::calc::math

class Text: public RectangularElement, public AudioContent {
public:
    Text();
    ~Text() override;

    static constexpr double NO_WRAP = -1;

public:
    void setFont(const XojFont& font);
    XojFont& getFont();
    const XojFont& getFont() const;
    double getFontSize() const;       // same result as getFont()->getSize(), but const
    std::string getFontName() const;  // same result as getFont()->getName(), but const

    const std::string& getText() const;
    void setText(std::string text);

    void setInEditing(bool inEditing);
    bool isInEditing() const;

    xoj::util::GObjectSPtr<PangoLayout> createPangoLayout() const;
    void updatePangoFont(PangoLayout* layout) const;

    /// Set the text's wrapping width. Use Text::NO_WRAP to disable wrapping
    void setWrap(double wrap);

    /// Return Text::NO_WRAP if wrapping is disabled
    inline double getWrap() const { return wrapWidth; }

    void setAlignment(TextAlignment a);
    inline TextAlignment getAlign() const { return align; }

    inline void setJustify(bool j) { this->justify = j; }
    inline bool getJustify() const { return justify; }

    /// Calculation region (Mathcad style): the text is evaluated by xoj::calc and shown with its results
    inline void setCalc(bool c) {
        this->calc = c;
        sizeCalculated = false;
    }
    inline bool isCalc() const { return calc; }

    /// Pango markup shown for a calculation region when it is not being edited (computed, not saved).
    /// Returns true if it changed.
    bool setCalcMarkup(std::string markup);
    inline const std::string& getCalcMarkup() const { return calcMarkup; }

    /// Values read from Excel cells by a calculation region (see calc/ExcelLink.h), saved so that the results are
    /// still shown when the workbook is not available
    inline void setCalcCells(std::string c) { this->calcCells = std::move(c); }
    inline const std::string& getCalcCells() const { return calcCells; }

    /// xournalpp-calc: formats of parts of the text (bold, colors...), see TextFormat.h
    inline const xoj::text::Formats& getFormats() const { return formats; }
    inline void setFormats(xoj::text::Formats f) {
        this->formats = std::move(f);
        sizeCalculated = false;
    }
    /// Pango attributes of the formats for the given text (the text of the element if nullptr)
    void applyFormats(PangoLayout* layout, const std::string* text = nullptr) const;

    /// xournalpp-calc: a calculation region typeset in two dimensions (fractions, exponents, roots): as displayed,
    /// or as written while it is edited
    std::unique_ptr<xoj::calc::math::Formula> calcFormula() const;
    xoj::calc::math::Style calcStyle() const;

    /// Put the content that is displayed (text, or the calculation results) into the layout
    void applyDisplayContent(PangoLayout* layout) const;

    /// Fixed distance between the baselines of two lines, so that lines follow the alignment grid. 0: natural
    void setLineHeight(double h);
    inline double getLineHeight() const { return lineHeight; }
    /// Apply the fixed line height (if any) to a layout whose content is already set
    void applyLineHeight(PangoLayout* layout) const;
    /// Natural height of one line, and distance from the top of the text to the first baseline (with line height)
    double naturalLineHeight() const;
    double firstBaseline() const;

    auto cloneText() const -> std::unique_ptr<Text>;
    auto clone() const -> ElementPtr override;

public:
    // Serialize interface
    void serialize(ObjectOutputStream& out) const override;
    void readSerialized(ObjectInputStream& in) override;

    struct Boxes {
        xoj::util::Size<double> theoreticalSize;
        /// Could be larger or smaller than the theoretical size, and there could be an offset
        xoj::util::Rectangle<double> effectiveBounds;
    };
    /// Get the boxes out of a Pango context, ignoring any affine transformation
    static Boxes computeBoxesForLayout(PangoLayout* layout, double wrapWidth);

protected:
    void calcSize() const override;

public:
    std::vector<XojPdfRectangle> findText(const std::string& search) const;

private:
    XojFont font;

    std::string text;
    xoj::text::Formats formats;

    /**
     * The size of the Pango layout, without any affine transformation applied (so in Text element coordinates)
     *
     * Could be larger or smaller than the theoretical size this->naturalSize, and there could be an offset
     */
    mutable xoj::util::Rectangle<double> effectiveBounds;

    double wrapWidth = NO_WRAP;  ///< NO_WRAP for no wrap
    TextAlignment align = TextAlignment::LEFT;
    bool justify = false;  ///< Stretch whitespaces to make all complete lines have the same width

    double lineHeight = 0;   ///< Fixed line pitch (alignment grid), 0 for natural
    bool calc = false;       ///< Calculation region
    std::string calcMarkup;  ///< Displayed content of a calculation region (with results)
    std::string calcCells;   ///< Values read from Excel cells (serialized calc::CellCache)

    bool inEditing = false;
};
