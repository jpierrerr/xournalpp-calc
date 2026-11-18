#include "GraphBackgroundView.h"

#include <algorithm>  // for max, min
#include <cmath>      // for floor
#include <memory>     // for allocator

#include "model/BackgroundConfig.h"                  // for BackgroundConfig
#include "view/background/BackgroundView.h"          // for view
#include "view/background/OneColorBackgroundView.h"  // for OneColorBackgrou...
#include "view/background/PlainBackgroundView.h"     // for PlainBackgroundView

using namespace background_config_strings;
using namespace xoj::view;

GraphBackgroundView::GraphBackgroundView(double pageWidth, double pageHeight, Color backgroundColor,
                                         const BackgroundConfig& config):
        OneColorBackgroundView(pageWidth, pageHeight, backgroundColor, config, DEFAULT_LINE_WIDTH, DEFAULT_LINE_COLOR,
                               ALT_DEFAULT_LINE_COLOR) {

    config.loadValue(CFG_RASTER, squareSize);
    config.loadValue(CFG_MARGIN, margin);
    config.loadValue(CFG_BOLD_LINE_INTERVAL, boldLineInterval);
    config.loadValue(CFG_BOLD_LINE_WIDTH, boldLineWidth);

    config.loadValue(CFG_V_LINE, vLine);
    if (vLine < 0) {
        vLine += pageWidth;
    }
    if (backgroundColor.isLight()) {
        vLineColor = getColorOr(config, CFG_FOREGROUND_COLOR_2, DEFAULT_V_LINE_COLOR);
    } else {
        vLineColor = getColorOr(config, CFG_ALT_FOREGROUND_COLOR_2, ALT_DEFAULT_V_LINE_COLOR);
    }

    if (int roundToGrid = 0; margin > 0.0 && config.loadValue(CFG_ROUND_MARGIN, roundToGrid) && roundToGrid) {
        roundUpMargin = true;
    }
}

void GraphBackgroundView::draw(cairo_t* cr) const {
    // Paint the background color
    PlainBackgroundView::draw(cr);

    // Get the bounds of the mask, in page coordinates
    double minX;
    double maxX;
    double minY;
    double maxY;
    cairo_clip_extents(cr, &minX, &minY, &maxX, &maxY);

    if (margin > 0.0) {
        minX = std::max(minX, margin);
        maxX = std::min(maxX, pageWidth - margin);
        minY = std::max(minY, margin);
        maxY = std::min(maxY, pageHeight - margin);
    }

    const double halfLineWidth = 0.5 * (boldLineInterval > 0 ? boldLineWidth : lineWidth);
    const double clearance = std::max(margin, halfLineWidth);
    /*  1- Add a halfLineWidth padding in case the line is just outside the mask but its thickness still makes it
     *     (partially) visible
     *  2- Set clearance so no line is drawn in the margin or to close to the edge of the sheet
     */
    auto [indexMinX, indexMaxX] =
            getIndexBounds(minX - halfLineWidth, maxX + halfLineWidth, squareSize, clearance, pageWidth);
    auto [indexMinY, indexMaxY] =
            getIndexBounds(minY - halfLineWidth, maxY + halfLineWidth, squareSize, clearance, pageHeight);

    if (roundUpMargin) {
        auto [pageIndexMinX, pageIndexMaxX] = getIndexBounds(margin - halfLineWidth, pageWidth - margin + halfLineWidth,
                                                             squareSize, clearance, pageWidth);
        auto [pageIndexMinY, pageIndexMaxY] = getIndexBounds(
                margin - halfLineWidth, pageHeight - margin + halfLineWidth, squareSize, clearance, pageHeight);
        minX = std::max(minX, squareSize * pageIndexMinX);
        maxX = std::min(maxX, squareSize * pageIndexMaxX);
        minY = std::max(minY, squareSize * pageIndexMinY);
        maxY = std::min(maxY, squareSize * pageIndexMaxY);
    }

    cairo_save(cr);
    Util::cairo_set_source_rgbi(cr, foregroundColor);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);

    if (boldLineInterval > 0) {
        cairo_set_line_width(cr, lineWidth);
        if (minY < maxY) {
            for (int i = indexMinX; i <= indexMaxX; ++i) {
                if (i % boldLineInterval != 0) {
                    double x = i * squareSize;
                    cairo_move_to(cr, x, minY);
                    cairo_line_to(cr, x, maxY);
                }
            }
        }
        if (minX < maxX) {
            for (int i = indexMinY; i <= indexMaxY; ++i) {
                if (i % boldLineInterval != 0) {
                    double y = i * squareSize;
                    cairo_move_to(cr, minX, y);
                    cairo_line_to(cr, maxX, y);
                }
            }
        }
        cairo_stroke(cr);

        cairo_set_line_width(cr, boldLineWidth);
        if (minY < maxY) {
            for (int i = indexMinX; i <= indexMaxX; ++i) {
                if (i % boldLineInterval == 0) {
                    double x = i * squareSize;
                    cairo_move_to(cr, x, minY);
                    cairo_line_to(cr, x, maxY);
                }
            }
        }
        if (minX < maxX) {
            for (int i = indexMinY; i <= indexMaxY; ++i) {
                if (i % boldLineInterval == 0) {
                    double y = i * squareSize;
                    cairo_move_to(cr, minX, y);
                    cairo_line_to(cr, maxX, y);
                }
            }
        }
        cairo_stroke(cr);
    } else {
        cairo_set_line_width(cr, lineWidth);
        if (minY < maxY) {
            for (int i = indexMinX; i <= indexMaxX; ++i) {
                double x = i * squareSize;
                cairo_move_to(cr, x, minY);
                cairo_line_to(cr, x, maxY);
            }
        }
        if (minX < maxX) {
            for (int i = indexMinY; i <= indexMaxY; ++i) {
                double y = i * squareSize;
                cairo_move_to(cr, minX, y);
                cairo_line_to(cr, maxX, y);
            }
        }
        cairo_stroke(cr);
    }

    if (vLine > 0) {
        // Vertical margin line, as on ruled paper with a vertical line
        Util::cairo_set_source_rgbi(cr, vLineColor);
        cairo_set_line_width(cr, lineWidth);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        cairo_move_to(cr, vLine, 0);
        cairo_line_to(cr, vLine, pageHeight);
        cairo_stroke(cr);
    }

    cairo_restore(cr);
}
