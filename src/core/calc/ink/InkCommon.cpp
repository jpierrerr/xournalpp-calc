#include <algorithm>
#include <array>
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

GrayImage grayFromSurface(cairo_surface_t* surface) {
    GrayImage img;
    if (!surface || cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE) {
        return img;
    }
    // Draw it on white in a known format (transparent PNGs, any pixel format)
    const int w = cairo_image_surface_get_width(surface);
    const int h = cairo_image_surface_get_height(surface);
    if (w <= 0 || h <= 0 || static_cast<long long>(w) * h > 64LL * 1024 * 1024) {
        return img;
    }
    cairo_surface_t* rgb = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(rgb);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    cairo_set_source_surface(cr, surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(rgb);

    img.w = w;
    img.h = h;
    img.px.resize(static_cast<size_t>(w) * h);
    const unsigned char* data = cairo_image_surface_get_data(rgb);
    const int stride = cairo_image_surface_get_stride(rgb);
    unsigned long long sum = 0;
    for (int y = 0; y < h; y++) {
        const auto* row = reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(y) * stride);
        for (int x = 0; x < w; x++) {
            const uint32_t p = row[x];
            const unsigned r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
            const auto v = static_cast<unsigned char>((299 * r + 587 * g + 114 * b) / 1000);
            img.px[static_cast<size_t>(y) * w + x] = v;
            sum += v;
        }
    }
    cairo_surface_destroy(rgb);
    // A dark background (dark mode of a viewer, a blackboard): invert
    if (sum / img.px.size() < 110) {
        for (auto& v: img.px) {
            v = static_cast<unsigned char>(255 - v);
        }
    }
    return img;
}

namespace {

/// Threshold between writing and background: halfway between the background (the most common light value) and
/// the darkest pixels
int inkThreshold(const GrayImage& img) {
    std::array<size_t, 256> hist{};
    for (unsigned char v: img.px) {
        hist[v]++;
    }
    int background = 255;
    size_t best = 0;
    for (int v = 128; v < 256; v++) {
        if (hist[static_cast<size_t>(v)] > best) {
            best = hist[static_cast<size_t>(v)];
            background = v;
        }
    }
    int darkest = 0;
    while (darkest < 255 && hist[static_cast<size_t>(darkest)] == 0) {
        darkest++;
    }
    if (darkest > background - 50) {
        return 0;  // no contrast: nothing is writing
    }
    return (background + darkest) / 2;
}

GrayImage crop(const GrayImage& img, int x0, int y0, int x1, int y1) {
    GrayImage out;
    out.w = x1 - x0;
    out.h = y1 - y0;
    out.px.resize(static_cast<size_t>(out.w) * out.h);
    for (int y = 0; y < out.h; y++) {
        std::copy_n(img.px.begin() + static_cast<long>(static_cast<size_t>(y0 + y) * img.w + x0), out.w,
                    out.px.begin() + static_cast<long>(static_cast<size_t>(y) * out.w));
    }
    return out;
}

}  // namespace

std::vector<GrayImage> splitImageLines(const GrayImage& img) {
    std::vector<GrayImage> lines;
    if (img.w <= 0 || img.h <= 0) {
        return lines;
    }
    const int t = inkThreshold(img);
    std::vector<int> rowInk(static_cast<size_t>(img.h), 0);
    std::vector<int> colInk(static_cast<size_t>(img.w), 0);
    for (int y = 0; y < img.h; y++) {
        for (int x = 0; x < img.w; x++) {
            if (img.px[static_cast<size_t>(y) * img.w + x] < t) {
                rowInk[static_cast<size_t>(y)]++;
                colInk[static_cast<size_t>(x)]++;
            }
        }
    }
    // Bands of rows with writing
    struct Band {
        int y0, y1;
    };
    std::vector<Band> bands;
    for (int y = 0; y < img.h; y++) {
        if (rowInk[static_cast<size_t>(y)] > 0) {
            if (!bands.empty() && bands.back().y1 == y) {
                bands.back().y1 = y + 1;
            } else {
                bands.push_back({y, y + 1});
            }
        }
    }
    if (bands.empty()) {
        return lines;
    }
    // Join bands separated by small gaps (the parts of a fraction, an exponent, the dot of an i)
    std::vector<Band> joined{bands.front()};
    for (size_t i = 1; i < bands.size(); i++) {
        Band& last = joined.back();
        const int gap = bands[i].y0 - last.y1;
        const int smaller = std::min(last.y1 - last.y0, bands[i].y1 - bands[i].y0);
        const int larger = std::max(last.y1 - last.y0, bands[i].y1 - bands[i].y0);
        if (gap <= std::max(3, larger / 3) || smaller < larger / 3) {
            last.y1 = bands[i].y1;
        } else {
            joined.push_back(bands[i]);
        }
    }
    for (const Band& b: joined) {
        int x0 = img.w, x1 = 0;
        for (int y = b.y0; y < b.y1; y++) {
            for (int x = 0; x < img.w; x++) {
                if (img.px[static_cast<size_t>(y) * img.w + x] < t) {
                    x0 = std::min(x0, x);
                    x1 = std::max(x1, x + 1);
                }
            }
        }
        if (x1 - x0 < 2 || b.y1 - b.y0 < 2) {
            continue;  // a speck
        }
        lines.push_back(crop(img, x0, b.y0, x1, b.y1));
    }
    return lines;
}

std::vector<unsigned char> fitImage(const GrayImage& img, int w, int h, const std::string& debugPng) {
    std::vector<unsigned char> out(static_cast<size_t>(w) * h, 255);
    if (img.w <= 0 || img.h <= 0) {
        return out;
    }
    // The picture of the writing with a margin of about a quarter of its height (as in typeset formulas)
    const int pad = std::max(4, std::min(img.w, img.h) / 4);
    const int pw = img.w + 2 * pad;
    const int ph = img.h + 2 * pad;
    cairo_surface_t* src = cairo_image_surface_create(CAIRO_FORMAT_RGB24, pw, ph);
    {
        unsigned char* data = cairo_image_surface_get_data(src);
        const int stride = cairo_image_surface_get_stride(src);
        cairo_surface_flush(src);
        for (int y = 0; y < ph; y++) {
            auto* row = reinterpret_cast<uint32_t*>(data + static_cast<size_t>(y) * stride);
            for (int x = 0; x < pw; x++) {
                const int ix = x - pad, iy = y - pad;
                const unsigned v = (ix >= 0 && iy >= 0 && ix < img.w && iy < img.h) ?
                                           img.px[static_cast<size_t>(iy) * img.w + ix] :
                                           255u;
                row[x] = 0xFF000000u | (v << 16) | (v << 8) | v;
            }
        }
        cairo_surface_mark_dirty(src);
    }
    cairo_surface_t* dst = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* cr = cairo_create(dst);
    cairo_scale(cr, static_cast<double>(w) / pw, static_cast<double>(h) / ph);
    cairo_set_source_surface(cr, src, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_PAD);  // white up to the edges
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(dst);
    const unsigned char* data = cairo_image_surface_get_data(dst);
    const int stride = cairo_image_surface_get_stride(dst);
    for (int y = 0; y < h; y++) {
        const auto* row = reinterpret_cast<const uint32_t*>(data + static_cast<size_t>(y) * stride);
        for (int x = 0; x < w; x++) {
            out[static_cast<size_t>(y) * w + x] = static_cast<unsigned char>(row[x] & 0xFF);
        }
    }
    if (!debugPng.empty()) {
        cairo_surface_write_to_png(dst, debugPng.c_str());
    }
    cairo_surface_destroy(src);
    cairo_surface_destroy(dst);
    return out;
}

}  // namespace xoj::calc::ink
