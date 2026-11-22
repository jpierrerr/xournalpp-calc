/*
 * Xournal++ Calc
 *
 * Tests for the two-dimensional typesetting of the calculation regions (fractions, exponents, roots)
 *
 * @license GNU GPLv2 or later
 */

#include <cstdlib>
#include <string>

#include <cairo.h>
#include <gtest/gtest.h>

#include "calc/MathLayout.h"

using namespace xoj::calc::math;

static Style style() {
    Style s;
    s.fontName = "Sans";
    s.fontSize = 12;
    s.firstBaseline = 14;
    s.lineHeight = 28.35;
    s.naturalLineHeight = 16;
    return s;
}

TEST(CalcMath, plainLineIsPlainMarkup) {
    EXPECT_EQ(structuredMarkup("a := 2 m", nullptr).find('\x01'), std::string::npos);
    EXPECT_FALSE(hasStructure("F := 300 kN"));
    EXPECT_FALSE(hasStructure("q := 5 kN/m"));  // division of units stays on the line
    EXPECT_TRUE(hasStructure("M := q*L^2/8"));
    EXPECT_TRUE(hasStructure("d := RAIZ(2*A)"));
}

TEST(CalcMath, fractionMarkup) {
    std::string m = structuredMarkup("M := q*L^(2)/(8)", nullptr);
    EXPECT_NE(m.find('\x01'), std::string::npos);
    EXPECT_NE(m.find('\x04'), std::string::npos);
    EXPECT_EQ(flattenMarkup(m).find("("), std::string::npos);
}

TEST(CalcMath, unitDivision) {
    EXPECT_TRUE(unitDivisionBefore("q := 5 kN", 9));
    EXPECT_FALSE(unitDivisionBefore("M := q*L", 8));
    EXPECT_FALSE(unitDivisionBefore("s := P", 6));
    EXPECT_TRUE(unitDivisionBefore("v := 3 m", 8));
}

TEST(CalcMath, cursorStops) {
    std::string t = "M:=q*L^(2)/(8)";
    auto f = Formula::forEditing(t, style());
    const size_t caret = t.find('^');
    const size_t open = caret + 1;
    const size_t slash = t.find('/');
    EXPECT_TRUE(f->isStop(caret));       // after L
    EXPECT_FALSE(f->isStop(caret + 1));  // between ^ and (
    EXPECT_TRUE(f->isStop(open + 1));    // start of the exponent
    EXPECT_TRUE(f->isStop(slash));       // end of the numerator
    EXPECT_FALSE(f->isStop(slash + 1));  // between / and (
    EXPECT_TRUE(f->isStop(slash + 2));   // start of the denominator
    EXPECT_TRUE(f->isStop(t.size()));    // after the fraction
    EXPECT_EQ(f->step(caret, 1), open + 1);
    EXPECT_EQ(f->step(open + 1, -1), caret);
    EXPECT_EQ(f->step(slash, 1), slash + 2);
    // The denominator is under the numerator
    auto up = f->vertical(slash + 3, -1);
    ASSERT_TRUE(up.has_value());
    EXPECT_LT(*up, slash);
    // From the start of the denominator, up goes to the numerator (not before the fraction)
    std::string t2 = "z:=(1+2)/(3-1)";
    auto f2 = Formula::forEditing(t2, style());
    auto up2 = f2->vertical(t2.find('3'), -1);
    ASSERT_TRUE(up2.has_value());
    EXPECT_GE(*up2, t2.find('1'));
    EXPECT_LE(*up2, t2.find('2') + 1);
    EXPECT_GT(f->caretAt(slash + 2).top, f->caretAt(3).bottom - 1);
    // Boxes
    auto s = f->slotAround(slash + 2);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->start, slash + 2);
    EXPECT_EQ(s->end, t.size() - 1);
}

TEST(CalcMath, emptyBoxes) {
    std::string t = "x^()";
    auto f = Formula::forEditing(t, style());
    EXPECT_TRUE(f->isStop(3));
    auto s = f->slotAround(3);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->delStart, 1u);
    EXPECT_EQ(s->delEnd, 4u);

    t = "a/()";
    f = Formula::forEditing(t, style());
    s = f->slotAround(3);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->delStart, 1u);
    EXPECT_EQ(s->delEnd, 4u);
}

static void render(const std::string& name, const std::string& text, bool editing, size_t cursor = std::string::npos) {
    const char* dir = std::getenv("CALC_MATH_PNG");
    if (!dir) {
        return;
    }
    std::unique_ptr<Formula> f = editing ? Formula::forEditing(text, style()) : Formula::forDisplay(text, style());
    const double scale = 4;
    const int w = static_cast<int>((f->width() + 20) * scale),
              h = static_cast<int>((f->bottom() - f->top() + 20) * scale);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_scale(cr, scale, scale);
    cairo_translate(cr, 10, 10 - f->top());
    cairo_set_source_rgb(cr, 0, 0, 0);
    f->draw(cr, cursor);
    if (cursor != std::string::npos) {
        auto c = f->caretAt(cursor);
        cairo_set_source_rgb(cr, 0.9, 0, 0);
        cairo_rectangle(cr, c.x - 0.3, c.top, 0.6, c.bottom - c.top);
        cairo_fill(cr);
    }
    cairo_destroy(cr);
    cairo_surface_write_to_png(surf, (std::string(dir) + "/" + name + ".png").c_str());
    cairo_surface_destroy(surf);
}

TEST(CalcMath, renderSamples) {
    render("e1", "M≔q*L", true, 5);
    render("e2", "M≔q*L^()", true, 7);
    render("e3", "M≔q*L^(2)", true, 10);
    render("e4", "M≔q*L^(2)/()", true, 13);
    render("e5", "M≔q*L^(2)/(8)", true, 15);
    render("e6", "d≔RAIZ(A_s*f_y/(0.85*f'c*b))", true, 12);
    render("e7", "x≔(-b+RAIZ(b^2-4*a*c))/(2*a)\nq≔5 kN/m\ny≔SENO(a/b)+ABS(x/2)", true, std::string::npos);
    render("d1", structuredMarkup("M := q*L^(2)/(8) = ", nullptr) + "<span foreground=\"#1a5fb4\">12.5 kN·m</span>",
           false);
    render("d2",
           structuredMarkup("x := (-b+RAIZ(b^2-4*a*c))/(2*a)", nullptr) + "\n" +
                   structuredMarkup("q := 5 kN/m", nullptr) + "\n" +
                   structuredMarkup("y := SENO(a/b) + ABS(x/2) + RAIZ3(8)", nullptr),
           false);
}

TEST(CalcMath, functionNames) {
    std::string t = "y:=SENO(x)";
    auto f = Formula::forEditing(t, style());
    EXPECT_TRUE(f->isStop(3));    // before the name
    EXPECT_FALSE(f->isStop(5));   // inside SENO
    EXPECT_TRUE(f->isStop(7));    // after the name
    EXPECT_EQ(f->step(3, 1), 7u);
    auto n = f->functionNameAt(7, -1);
    ASSERT_TRUE(n.has_value());
    EXPECT_EQ(n->first, 3u);
    EXPECT_EQ(n->second, 7u);
    render("fn", "y:=SENO(x)+LOG10(a/b)+ASENO()", true, 8);
}

TEST(CalcMath, requestedUnitsStayOnTheLine) {
    using xoj::calc::math::unitDivisionBefore;
    // "M = ? kN·m/m": the "/" divides the requested units, it does not make a fraction
    std::string t = "M = ? kN*m";
    EXPECT_TRUE(unitDivisionBefore(t, t.size()));
    t = "M = ?kN";
    EXPECT_TRUE(unitDivisionBefore(t, t.size()));
    t = "M = a*b";
    EXPECT_FALSE(unitDivisionBefore(t, t.size()));
}
