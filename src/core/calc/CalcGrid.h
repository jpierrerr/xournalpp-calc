/*
 * Xournal++ Calc
 *
 * Mathcad-style alignment grid: a mesh of dots that follows the page background (squares or ruled lines), so that
 * text and calculation regions can be aligned on it.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cairo.h>

class PageType;

namespace xoj::calc {

struct AlignGrid {
    double dx;  ///< horizontal distance between dots
    double dy;  ///< vertical distance between dots (= distance between ruled lines)
    double x0;  ///< a column of dots goes through x0
    double y0;  ///< a row of dots goes through y0 (on ruled paper: the first line)

    double snapX(double x) const;
    /// The row of dots closest to y
    double snapY(double y) const;
    /// Smallest multiple of dy that is at least h (minus a small tolerance)
    double pitchFor(double h) const;
};

/// The grid matching a page background: squares, dots or ruled lines; 5 mm otherwise
AlignGrid alignmentGrid(const PageType& background, double pageWidth, double pageHeight);

/// Draw the dots (screen only), in page coordinates
void drawAlignmentGrid(cairo_t* cr, const AlignGrid& grid, double pageWidth, double pageHeight, double zoom);

}  // namespace xoj::calc
