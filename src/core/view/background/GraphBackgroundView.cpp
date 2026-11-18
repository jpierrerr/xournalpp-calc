#include "GraphBackgroundView.h"

#include <algorithm>  // for max, min
#include <cmath>      // for floor
#include <memory>     // for allocator

#include <pango/pangocairo.h>

#include "model/BackgroundConfig.h"                  // for BackgroundConfig
#include "util/i18n.h"
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
    if (int ex = 0; config.loadValue(CFG_EXAM_HEADER, ex) && ex) {
        examHeader = true;
    }
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

    // No grid behind the exam header
    minY = std::max(minY, headerHeight());

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
        cairo_move_to(cr, vLine, headerHeight());
        cairo_line_to(cr, vLine, pageHeight);
        cairo_stroke(cr);
    }

    cairo_restore(cr);

    if (examHeader) {
        drawExamHeader(cr);
    }
}

void GraphBackgroundView::drawExamHeader(cairo_t* cr) const {
    // Box from 1 to 7 squares down, 2 squares from each side; three rows of 2 squares with two fields each, and a
    // box for the grade on the right
    const double s = squareSize;
    const double left = 2 * s;
    const double right = pageWidth - 2 * s;
    const double top = s;
    const double bottom = 7 * s;
    const double gradeLeft = right - 5 * s;
    const double split = left + std::round((gradeLeft - left) * 0.68 / s) * s;  // between the long and short fields

    cairo_save(cr);
    Util::cairo_set_source_rgbi(cr, vLine > 0 ? vLineColor : foregroundColor);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, left, top, right - left, bottom - top);
    cairo_move_to(cr, gradeLeft, top);
    cairo_line_to(cr, gradeLeft, bottom);
    cairo_stroke(cr);

    // Field lines
    cairo_set_line_width(cr, 0.5);
    for (int r = 1; r <= 2; r++) {
        double y = top + 2 * s * r;
        cairo_move_to(cr, left, y);
        cairo_line_to(cr, gradeLeft, y);
    }
    cairo_move_to(cr, split, top);
    cairo_line_to(cr, split, bottom);
    cairo_stroke(cr);

    // Labels
    PangoLayout* layout = pango_cairo_create_layout(cr);
    PangoFontDescription* font = pango_font_description_from_string("Sans 7");
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    const char* labels[3][2] = {{_("Subject"), _("Date")}, {_("Name"), _("Student ID")}, {_("Teacher"), _("Group")}};
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 2; c++) {
            pango_layout_set_text(layout, labels[r][c], -1);
            cairo_move_to(cr, (c == 0 ? left : split) + 3, top + 2 * s * r + 2);
            pango_cairo_show_layout(cr, layout);
        }
    }
    pango_layout_set_text(layout, _("Grade"), -1);
    int w = 0, h = 0;
    pango_layout_get_size(layout, &w, &h);
    cairo_move_to(cr, (gradeLeft + right) / 2 - 0.5 * w / PANGO_SCALE, top + 2);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);
    cairo_restore(cr);
}
