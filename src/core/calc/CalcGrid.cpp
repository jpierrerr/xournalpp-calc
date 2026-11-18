#include "CalcGrid.h"

#include <algorithm>
#include <cmath>

#include "model/BackgroundConfig.h"
#include "model/PageType.h"

namespace xoj::calc {

namespace {
constexpr double DEFAULT_STEP = 14.17;          // 5 mm, as the default graph background
constexpr double RULED_HEADER = 80.0;           // RuledBackgroundView::HEADER_SIZE
constexpr double RULED_DEFAULT_SPACING = 24.0;  // RuledBackgroundView::lineSpacing
constexpr double LINED_DEFAULT_MARGIN = 72.0;   // LinedBackgroundView::margin
}  // namespace

double AlignGrid::snapX(double x) const { return x0 + std::round((x - x0) / dx) * dx; }

double AlignGrid::snapY(double y) const { return y0 + std::round((y - y0) / dy) * dy; }

double AlignGrid::pitchFor(double h) const { return dy * std::max(1.0, std::ceil(h / dy - 0.05)); }

AlignGrid alignmentGrid(const PageType& background, double pageWidth, double pageHeight) {
    BackgroundConfig config(background.config);
    switch (background.format) {
        case PageTypeFormat::Graph:
        case PageTypeFormat::Dotted: {
            double step = DEFAULT_STEP;
            config.loadValue(background_config_strings::CFG_RASTER, step);
            if (step <= 0) {
                step = DEFAULT_STEP;
            }
            return {step, step, 0, 0};
        }
        case PageTypeFormat::Ruled:
        case PageTypeFormat::Lined: {
            double spacing = RULED_DEFAULT_SPACING;
            config.loadValue(background_config_strings::CFG_RASTER, spacing);
            if (spacing <= 0) {
                spacing = RULED_DEFAULT_SPACING;
            }
            double x0 = 0;
            if (background.format == PageTypeFormat::Lined) {
                x0 = LINED_DEFAULT_MARGIN;
                config.loadValue(background_config_strings::CFG_MARGIN, x0);
                if (x0 < 0) {
                    x0 += pageWidth;
                }
            }
            return {spacing / 2, spacing, x0, RULED_HEADER};
        }
        default:
            return {DEFAULT_STEP, DEFAULT_STEP, 0, 0};
    }
}

void drawAlignmentGrid(cairo_t* cr, const AlignGrid& g, double pageWidth, double pageHeight, double zoom) {
    if (g.dx < 2 || g.dy < 2) {
        return;
    }
    cairo_save(cr);
    cairo_set_source_rgba(cr, 0.10, 0.37, 0.71, 0.55);
    // About 1.6 screen pixels, whatever the zoom
    const double r = 0.8 / std::max(zoom, 0.1);
    double xStart = g.x0 - std::floor(g.x0 / g.dx) * g.dx;
    double yStart = g.y0 - std::floor(g.y0 / g.dy) * g.dy;
    for (double y = yStart; y <= pageHeight; y += g.dy) {
        for (double x = xStart; x <= pageWidth; x += g.dx) {
            cairo_rectangle(cr, x - r, y - r, 2 * r, 2 * r);
        }
    }
    cairo_fill(cr);
    cairo_restore(cr);
}

}  // namespace xoj::calc
