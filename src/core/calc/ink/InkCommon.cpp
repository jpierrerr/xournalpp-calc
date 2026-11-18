#include <algorithm>
#include <cmath>
#include <cstdint>

#include <cairo.h>

#include "InkRecognizer.h"

namespace xoj::calc::ink {

InkBox boundingBox(const std::vector<InkStroke>& strokes) {
    InkBox b;
    bool first = true;
    for (const auto& s: strokes) {
        for (const auto& p: s.points) {
            if (first) {
                b = {p.x, p.y, p.x, p.y};
                first = false;
            } else {
                b.x0 = std::min(b.x0, p.x);
                b.y0 = std::min(b.y0, p.y);
                b.x1 = std::max(b.x1, p.x);
                b.y1 = std::max(b.y1, p.y);
            }
        }
    }
    return b;
}

std::vector<std::vector<InkStroke>> splitLines(const std::vector<InkStroke>& strokes) {
    struct Item {
        InkBox box;
        const InkStroke* stroke;
    };
    std::vector<Item> items;
    std::vector<double> heights;
    for (const auto& s: strokes) {
        if (s.points.empty()) {
            continue;
        }
        InkBox b = boundingBox({s});
        items.push_back({b, &s});
        heights.push_back(b.height());
    }
    if (items.empty()) {
        return {};
    }
    // Typical height of a character: median of the strokes that are not flat (fraction bars, minus signs)
    std::sort(heights.begin(), heights.end());
    double charHeight = heights[heights.size() / 2];
    if (heights.size() > 2) {
        charHeight = std::max(charHeight, heights[(heights.size() * 3) / 4] * 0.7);
    }
    charHeight = std::max(charHeight, 1.0);
    const double minGap = 0.45 * charHeight;

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.box.y0 < b.box.y0; });
    std::vector<std::vector<InkStroke>> lines;
    double lineBottom = 0;
    for (const auto& it: items) {
        if (lines.empty() || it.box.y0 > lineBottom + minGap) {
            lines.emplace_back();
            lineBottom = it.box.y1;
        } else {
            lineBottom = std::max(lineBottom, it.box.y1);
        }
        lines.back().push_back(*it.stroke);
    }
    return lines;
}

std::vector<unsigned char> renderStrokes(const std::vector<InkStroke>& strokes, int w, int h, double lineHeight,
                                         double penWidth, const std::string& debugPng) {
    std::vector<unsigned char> out(static_cast<size_t>(w) * static_cast<size_t>(h), 255);
    InkBox box = boundingBox(strokes);
    double bw = std::max(box.width(), 1e-3);
    double bh = std::max(box.height(), 1e-3);

    // Layout picture: the writing at lineHeight pixels high, with a margin
    double k = lineHeight / bh;
    // Very flat writing (a single dash) or very narrow (a "1"): keep the scale of a character
    k = std::min(k, lineHeight / std::max(bh, 0.25 * bw));
    double pad = 0.12 * lineHeight + penWidth;
    double pw = bw * k + 2 * pad;
    double ph = bh * k + 2 * pad;

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);

    // Stretch the layout picture to the image (as the image processor resizes it)
    cairo_scale(cr, w / pw, h / ph);
    cairo_translate(cr, pad, pad);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_line_width(cr, penWidth);
    for (const auto& s: strokes) {
        if (s.points.empty()) {
            continue;
        }
        cairo_move_to(cr, (s.points[0].x - box.x0) * k, (s.points[0].y - box.y0) * k);
        if (s.points.size() == 1) {
            cairo_line_to(cr, (s.points[0].x - box.x0) * k + 0.01, (s.points[0].y - box.y0) * k);
        }
        for (size_t i = 1; i < s.points.size(); i++) {
            cairo_line_to(cr, (s.points[i].x - box.x0) * k, (s.points[i].y - box.y0) * k);
        }
        cairo_stroke(cr);
    }
    cairo_destroy(cr);
    cairo_surface_flush(surf);

    const unsigned char* data = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    for (int y = 0; y < h; y++) {
        const auto* row = reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(y) * stride);
        for (int x = 0; x < w; x++) {
            uint32_t px = row[x];
            unsigned r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
            out[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>((r + g + b) / 3);
        }
    }
    if (!debugPng.empty()) {
        cairo_surface_write_to_png(surf, debugPng.c_str());
    }
    cairo_surface_destroy(surf);
    return out;
}

}  // namespace xoj::calc::ink
