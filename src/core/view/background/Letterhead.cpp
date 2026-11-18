#include "Letterhead.h"

#include <algorithm>
#include <memory>

#include <gdk/gdk.h>
#include <glib.h>
#include <pango/pangocairo.h>

#include "util/PathUtil.h"

namespace xoj::view {

namespace {

std::vector<std::string> split(const char* s) {
    std::vector<std::string> out;
    if (!s) {
        return out;
    }
    gchar** parts = g_strsplit(s, ";", -1);
    for (gchar** p = parts; *p; p++) {
        gchar* t = g_strstrip(*p);
        if (*t) {
            out.emplace_back(t);
        }
    }
    g_strfreev(parts);
    return out;
}

/// Load a logo, at most 900 px, cropped to its visible part (not transparent, not white)
GdkPixbuf* loadLogo(const fs::path& file) {
    GdkPixbuf* full = gdk_pixbuf_new_from_file_at_scale(Util::toGFilename(file).c_str(), 900, 900, true, nullptr);
    if (!full) {
        g_warning("Letterhead: could not load %s", Util::toGFilename(file).c_str());
        return nullptr;
    }
    GdkPixbuf* pb = full;
    if (!gdk_pixbuf_get_has_alpha(full)) {
        pb = gdk_pixbuf_add_alpha(full, false, 0, 0, 0);
        g_object_unref(full);
    }
    const int w = gdk_pixbuf_get_width(pb), h = gdk_pixbuf_get_height(pb);
    const int stride = gdk_pixbuf_get_rowstride(pb);
    const guchar* px = gdk_pixbuf_read_pixels(pb);
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const guchar* p = px + y * stride + 4 * x;
            if (p[3] > 24 && (p[0] < 240 || p[1] < 240 || p[2] < 240)) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    if (x1 < x0) {
        return pb;
    }
    GdkPixbuf* crop = gdk_pixbuf_new_subpixbuf(pb, x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    GdkPixbuf* copy = gdk_pixbuf_copy(crop);
    g_object_unref(crop);
    g_object_unref(pb);
    return copy;
}

/// Draw the logo centered in the box, keeping its proportions; returns the width used
double drawLogo(cairo_t* cr, GdkPixbuf* logo, double x, double y, double maxW, double maxH, bool alignRight) {
    if (!logo) {
        return 0;
    }
    const double w = gdk_pixbuf_get_width(logo), h = gdk_pixbuf_get_height(logo);
    const double scale = std::min(maxW / w, maxH / h);
    const double dw = w * scale, dh = h * scale;
    const double dx = alignRight ? x + maxW - dw : x;
    cairo_save(cr);
    cairo_translate(cr, dx, y + (maxH - dh) / 2);
    cairo_scale(cr, scale, scale);
    gdk_cairo_set_source_pixbuf(cr, logo, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BEST);
    cairo_paint(cr);
    cairo_restore(cr);
    return dw;
}

}  // namespace

Letterhead::~Letterhead() {
    if (leftLogo) {
        g_object_unref(leftLogo);
    }
    if (rightLogo) {
        g_object_unref(rightLogo);
    }
}

const Letterhead* Letterhead::get() {
    static const std::unique_ptr<Letterhead> instance = []() -> std::unique_ptr<Letterhead> {
        const fs::path folder = Util::getConfigSubfolder("membrete");
        const fs::path iniFile = folder / "membrete.ini";
        GKeyFile* ini = g_key_file_new();
        if (!g_key_file_load_from_file(ini, Util::toGFilename(iniFile).c_str(), G_KEY_FILE_NONE, nullptr)) {
            g_key_file_free(ini);
            return nullptr;
        }
        auto lh = std::make_unique<Letterhead>();
        auto str = [&](const char* key) {
            gchar* v = g_key_file_get_string(ini, "membrete", key, nullptr);
            std::string r = v ? v : "";
            g_free(v);
            return r;
        };
        lh->name = str("nombre");
        if (lh->name.empty()) {
            lh->name = "Membrete";
        }
        if (auto f = str("logo_izquierdo"); !f.empty()) {
            lh->leftLogo = loadLogo(folder / fs::u8path(f));
        }
        if (auto f = str("logo_derecho"); !f.empty()) {
            lh->rightLogo = loadLogo(folder / fs::u8path(f));
        }
        lh->title = split(str("titulo").c_str());
        lh->fields = split(str("campos").c_str());
        g_key_file_free(ini);
        return lh;
    }();
    return instance.get();
}

void Letterhead::draw(cairo_t* cr, double left, double top, double right, double bottom, double s,
                      Color lineColor) const {
    // Upper part (logos and title) above, a row of 2 squares for the fields below
    const double fieldsTop = fields.empty() ? bottom : bottom - 2 * s;
    const double pad = 0.3 * s;

    cairo_save(cr);
    const double leftW = drawLogo(cr, leftLogo, left + pad, top + pad, 6 * s, fieldsTop - top - 2 * pad, false);
    const double rightW =
            drawLogo(cr, rightLogo, right - pad - 14 * s, top + pad, 14 * s, fieldsTop - top - 2 * pad, true);

    // Title, centered between the logos
    const double textLeft = left + pad + leftW + pad;
    const double textRight = right - pad - rightW - pad;
    PangoLayout* layout = pango_cairo_create_layout(cr);
    // As TextView does: no matrix in the Pango context. With the page transform in it, some font backends (Windows)
    // cache the fonts with that offset, and later texts are drawn displaced
    pango_context_set_matrix(pango_layout_get_context(layout), nullptr);
    pango_layout_set_alignment(layout, PANGO_ALIGN_CENTER);
    pango_layout_set_width(layout, static_cast<int>(std::max(10.0, textRight - textLeft) * PANGO_SCALE));
    std::string markup;
    for (size_t i = 0; i < title.size(); i++) {
        gchar* esc = g_markup_escape_text(title[i].c_str(), -1);
        markup += (i ? "\n" : "") + std::string(i == 0 ? "<b>" : "") + esc + (i == 0 ? "</b>" : "");
        g_free(esc);
    }
    PangoFontDescription* font = pango_font_description_from_string("Sans 9");
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_markup(layout, markup.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_size(layout, &tw, &th);
    cairo_set_source_rgb(cr, 0.05, 0.22, 0.45);
    cairo_move_to(cr, textLeft, top + (fieldsTop - top) / 2 - 0.5 * th / PANGO_SCALE);
    pango_cairo_show_layout(cr, layout);

    // Fields: a row of cells with their label on the left
    if (!fields.empty()) {
        Util::cairo_set_source_rgbi(cr, lineColor);
        cairo_set_line_width(cr, 0.5);
        cairo_move_to(cr, left, fieldsTop);
        cairo_line_to(cr, right, fieldsTop);
        const double cellW = std::round((right - left) / static_cast<double>(fields.size()) / s) * s;
        for (size_t i = 1; i < fields.size(); i++) {
            cairo_move_to(cr, left + i * cellW, fieldsTop);
            cairo_line_to(cr, left + i * cellW, bottom);
        }
        cairo_stroke(cr);

        font = pango_font_description_from_string("Sans 8");
        pango_layout_set_font_description(layout, font);
        pango_font_description_free(font);
        pango_layout_set_width(layout, -1);
        pango_layout_set_alignment(layout, PANGO_ALIGN_LEFT);
        cairo_set_source_rgb(cr, 0.05, 0.22, 0.45);
        for (size_t i = 0; i < fields.size(); i++) {
            pango_layout_set_text(layout, (fields[i] + ":").c_str(), -1);
            pango_layout_get_size(layout, &tw, &th);
            cairo_move_to(cr, left + i * cellW + pad, fieldsTop + s - 0.5 * th / PANGO_SCALE);
            pango_cairo_show_layout(cr, layout);
        }
    }
    g_object_unref(layout);
    cairo_new_path(cr);  // leave no current point behind
    cairo_restore(cr);
}

}  // namespace xoj::view
