/*
 * Xournal++ Calc
 *
 * Mathcad-style alignment grid: a mesh of dots that follows the page background (squares or ruled lines), so that
 * text and calculation regions can be aligned on it.
 *
 * The grid is the same on graph, ruled and plain paper (5 mm squares, ruled lines every 2 squares), so that notes
 * keep their layout when the background is changed.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cairo.h>

class PageType;

namespace xoj::calc {

/// The baseline of the text is 1 mm above the writing line
constexpr double BASELINE_LIFT = 72.0 / 25.4;

struct AlignGrid {
    double dx;  ///< horizontal distance between dots
    double dy;  ///< vertical distance between dots
    double x0;  ///< a column of dots goes through x0
    double y0;  ///< a writing line goes through y0 (on ruled paper: the first ruled line)
    double margin = 0;  ///< where lines of text start (0: no margin line, 2 squares from the edge)

    /// Distance between two writing lines: 2 rows of dots (= the distance between ruled lines)
    double linePitch() const { return 2 * dy; }

    double snapX(double x) const;
    /// The writing line closest to y
    double snapY(double y) const;
    /// The row of dots closest to y
    double snapRow(double y) const;
    /// Smallest multiple of the line pitch that is at least h (minus a small tolerance)
    double pitchFor(double h) const;
};

/// The grid matching a page background: squares or dots (their raster), ruled lines (half their spacing), 5 mm
/// otherwise
AlignGrid alignmentGrid(const PageType& background, double pageWidth, double pageHeight);

/// Draw the dots (screen only), in page coordinates
void drawAlignmentGrid(cairo_t* cr, const AlignGrid& grid, double pageWidth, double pageHeight, double zoom);

}  // namespace xoj::calc
