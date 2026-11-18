/*
 * Xournal++
 *
 * Class for graph backgrounds
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cairo.h>  // for cairo_t

#include "util/Color.h"  // for Color

#include "OneColorBackgroundView.h"  // for OneColorBackgroundView

class BackgroundConfig;

namespace xoj::view {
class GraphBackgroundView: public OneColorBackgroundView {
public:
    GraphBackgroundView(double pageWidth, double pageHeight, Color backgroundColor, const BackgroundConfig& config);
    virtual ~GraphBackgroundView() = default;

    virtual void draw(cairo_t* cr) const override;

protected:
    bool roundUpMargin = false;
    double margin = 0.0;
    double squareSize = 14.17;  // 5mm
    int boldLineInterval = 0;   // 0 means no bold lines, otherwise every Nth line is bold
    double boldLineWidth = 1.5;
    double vLine =
            0.0;  ///< x of the vertical margin line, as on ruled paper ("vm"); 0 = none, negative = from the right
    Color vLineColor;
    bool examHeader = false;  ///< exam sheet: a header box (subject, name, ID, teacher, date, group, grade) on top

    /// Height of the exam header, a whole number of squares so that the grid below stays aligned
    double headerHeight() const { return examHeader ? 8 * squareSize : 0.0; }
    void drawExamHeader(cairo_t* cr) const;

    struct Frame {
        double left, top, right, bottom;
    };
    /// Exam sheet: the area of the grid below the header
    Frame examFrame() const;
    constexpr static double EXAM_CORNER_RADIUS = 8.5;  // 3 mm

    constexpr static Color DEFAULT_LINE_COLOR = Colors::xopp_silver;
    constexpr static Color ALT_DEFAULT_LINE_COLOR = Colors::xopp_darkslategray;
    constexpr static double DEFAULT_LINE_WIDTH = 0.5;
    constexpr static double DEFAULT_BOLD_LINE_WIDTH = 1.5;
    constexpr static Color DEFAULT_V_LINE_COLOR = Colors::xopp_deeppink;
    constexpr static Color ALT_DEFAULT_V_LINE_COLOR = Colors::xopp_midnightblue;
};
};  // namespace xoj::view
