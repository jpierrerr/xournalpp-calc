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

std::vector<InkStroke> smoothStrokes(const std::vector<InkStroke>& strokes, double step) {
    std::vector<InkStroke> out;
    out.reserve(strokes.size());
    step = std::max(step, 1e-6);
    for (const auto& s: strokes) {
        if (s.points.size() < 3) {
            out.push_back(s);
            continue;
        }
        // Resample along the length
        std::vector<InkPoint> r{s.points.front()};
        double carry = 0;
        for (size_t i = 1; i < s.points.size(); i++) {
            InkPoint a = s.points[i - 1];
            InkPoint b = s.points[i];
            double len = std::hypot(b.x - a.x, b.y - a.y);
            double t = step - carry;
            while (t <= len) {
                r.push_back({a.x + (b.x - a.x) * t / len, a.y + (b.y - a.y) * t / len});
                t += step;
            }
            carry = len - (t - step);
        }
        if (std::hypot(r.back().x - s.points.back().x, r.back().y - s.points.back().y) > step * 0.25) {
            r.push_back(s.points.back());
        }
        // Moving average (the ends stay in place)
        const int w = 4;
        InkStroke sm;
        sm.points.reserve(r.size());
        for (size_t i = 0; i < r.size(); i++) {
            int k = static_cast<int>(std::min({static_cast<size_t>(w), i, r.size() - 1 - i}));
            double sx = 0, sy = 0;
            for (int j = -k; j <= k; j++) {
                sx += r[i + j].x;
                sy += r[i + j].y;
            }
            sm.points.push_back({sx / (2 * k + 1), sy / (2 * k + 1)});
        }
        out.push_back(std::move(sm));
    }
    return out;
}

namespace {

/// Typical height of a character: median of the stroke heights, ignoring flat strokes (fraction bars, minus signs)
double characterHeight(const std::vector<InkBox>& boxes) {
    std::vector<double> heights;
    for (const auto& b: boxes) {
        heights.push_back(b.height());
    }
    if (heights.empty()) {
        return 1.0;
    }
    std::sort(heights.begin(), heights.end());
    double h = heights[heights.size() / 2];
    if (heights.size() > 2) {
        h = std::max(h, heights[(heights.size() * 3) / 4] * 0.7);
    }
    return std::max(h, 1.0);
}

std::vector<InkBox> strokeBoxes(const std::vector<InkStroke>& strokes) {
    std::vector<InkBox> boxes;
    boxes.reserve(strokes.size());
    for (const auto& s: strokes) {
        boxes.push_back(boundingBox({s}));
    }
    return boxes;
}

}  // namespace

std::vector<std::vector<size_t>> lineIndices(const std::vector<InkStroke>& strokes) {
    std::vector<InkBox> boxes = strokeBoxes(strokes);
    std::vector<size_t> order;
    std::vector<InkBox> nonEmpty;
    for (size_t i = 0; i < strokes.size(); i++) {
        if (!strokes[i].points.empty()) {
            order.push_back(i);
            nonEmpty.push_back(boxes[i]);
        }
    }
    if (order.empty()) {
        return {};
    }
    const double minGap = 0.45 * characterHeight(nonEmpty);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return boxes[a].y0 < boxes[b].y0; });
    std::vector<std::vector<size_t>> lines;
    double lineBottom = 0;
    for (size_t i: order) {
        if (lines.empty() || boxes[i].y0 > lineBottom + minGap) {
            lines.emplace_back();
            lineBottom = boxes[i].y1;
        } else {
            lineBottom = std::max(lineBottom, boxes[i].y1);
        }
        lines.back().push_back(i);
    }
    return lines;
}

std::vector<std::vector<InkStroke>> splitLines(const std::vector<InkStroke>& strokes) {
    std::vector<std::vector<InkStroke>> out;
    for (const auto& line: lineIndices(strokes)) {
        out.emplace_back();
        for (size_t i: line) {
            out.back().push_back(strokes[i]);
        }
    }
    return out;
}

std::vector<std::vector<size_t>> blockIndices(const std::vector<InkStroke>& strokes) {
    std::vector<InkBox> boxes = strokeBoxes(strokes);
    std::vector<std::vector<size_t>> blocks;
    for (auto line: lineIndices(strokes)) {
        std::vector<InkBox> lb;
        for (size_t i: line) {
            lb.push_back(boxes[i]);
        }
        const double maxGap = 2.5 * characterHeight(lb);
        std::sort(line.begin(), line.end(), [&](size_t a, size_t b) { return boxes[a].x0 < boxes[b].x0; });
        double right = 0;
        bool first = true;
        for (size_t i: line) {
            if (first || boxes[i].x0 > right + maxGap) {
                blocks.emplace_back();
                right = boxes[i].x1;
                first = false;
            } else {
                right = std::max(right, boxes[i].x1);
            }
            blocks.back().push_back(i);
        }
    }
    return blocks;
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
