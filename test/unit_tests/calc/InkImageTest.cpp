/*
 * Xournal++ Calc
 *
 * Tests for the preparation of pictures (screenshots of formulas) before they are recognized
 *
 * @license GNU GPLv2 or later
 */

#include <string>

#include <cairo.h>
#include <gtest/gtest.h>
#include <pango/pangocairo.h>

#include "calc/ink/InkRecognizer.h"

using namespace xoj::calc::ink;

namespace {

/// A screenshot: text drawn with Pango (markup), dark on light or light on dark
cairo_surface_t* screenshot(const std::string& markup, bool dark, int w = 600, int h = 260) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(s);
    if (dark) {
        cairo_set_source_rgb(cr, 0.12, 0.12, 0.14);
    } else {
        cairo_set_source_rgb(cr, 0.98, 0.98, 0.96);
    }
    cairo_paint(cr);
    PangoLayout* l = pango_cairo_create_layout(cr);
    PangoFontDescription* f = pango_font_description_from_string("Serif 26");
    pango_layout_set_font_description(l, f);
    pango_layout_set_markup(l, markup.c_str(), -1);
    pango_layout_set_spacing(l, 30 * PANGO_SCALE);
    cairo_set_source_rgb(cr, dark ? 0.95 : 0.05, dark ? 0.95 : 0.05, dark ? 0.95 : 0.05);
    cairo_move_to(cr, 40, 30);
    pango_cairo_show_layout(cr, l);
    pango_font_description_free(f);
    g_object_unref(l);
    cairo_destroy(cr);
    return s;
}

}  // namespace

TEST(InkImage, linesOfAScreenshot) {
    for (bool dark: {false, true}) {
        cairo_surface_t* s = screenshot("σ′ = σ − u\nτ = c′ + σ′·tan φ′", dark);
        GrayImage img = grayFromSurface(s);
        cairo_surface_destroy(s);
        ASSERT_EQ(img.w, 600);
        // Dark backgrounds are inverted: the background is light
        EXPECT_GT(img.px[5], 200) << "dark=" << dark;
        auto lines = splitImageLines(img);
        ASSERT_EQ(lines.size(), 2u) << "dark=" << dark;
        // Cropped to the writing
        EXPECT_LT(lines[0].w, 400);
        EXPECT_LT(lines[0].h, 60);
        auto fitted = fitImage(lines[1], 384, 384);
        ASSERT_EQ(fitted.size(), 384u * 384u);
        EXPECT_EQ(fitted[0], 255);  // white margin
        int dark_px = 0;
        for (auto v: fitted) {
            dark_px += v < 128;
        }
        EXPECT_GT(dark_px, 500);
    }
}

TEST(InkImage, exponentsAndFractionsStayInTheirLine) {
    cairo_surface_t* s = screenshot("q<sup><small>2</small></sup> = <span size='small'>P</span>\n"
                                    "<span size='x-small'> </span>",
                                    false);
    GrayImage img = grayFromSurface(s);
    cairo_surface_destroy(s);
    EXPECT_EQ(splitImageLines(img).size(), 1u);

    // A fraction drawn by hand: numerator, bar, denominator
    cairo_surface_t* f = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 300, 200);
    cairo_t* cr = cairo_create(f);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_rectangle(cr, 120, 40, 40, 40);   // numerator
    cairo_rectangle(cr, 100, 90, 80, 3);    // bar
    cairo_rectangle(cr, 120, 102, 40, 40);  // denominator
    cairo_fill(cr);
    cairo_destroy(cr);
    GrayImage frac = grayFromSurface(f);
    cairo_surface_destroy(f);
    auto lines = splitImageLines(frac);
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_EQ(lines[0].h, 102);
}

TEST(InkImage, emptyPictures) {
    GrayImage none;
    EXPECT_TRUE(splitImageLines(none).empty());
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 50, 50);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_destroy(cr);
    GrayImage white = grayFromSurface(s);
    cairo_surface_destroy(s);
    EXPECT_TRUE(splitImageLines(white).empty());
    EXPECT_TRUE(grayFromSurface(nullptr).px.empty());
}
