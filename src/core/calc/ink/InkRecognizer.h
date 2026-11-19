/*
 * Xournal++ Calc
 *
 * Offline recognition of handwriting: strokes → LaTeX formula (ONNX model) or → text (Windows recognizer)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include <cairo.h>

namespace xoj::calc::ink {

struct InkPoint {
    double x;
    double y;
};

struct InkStroke {
    std::vector<InkPoint> points;
};

/// Bounding box of a group of strokes
struct InkBox {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double width() const { return x1 - x0; }
    double height() const { return y1 - y0; }
};

InkBox boundingBox(const std::vector<InkStroke>& strokes);

/// Smooth the strokes: writing with a touchpad or a mouse gives few points in steps (horizontal and vertical
/// segments); the strokes are resampled every step units and averaged with their neighbours
std::vector<InkStroke> smoothStrokes(const std::vector<InkStroke>& strokes, double step);

/// Split strokes into lines of writing, from top to bottom (fractions and powers stay in their line)
std::vector<std::vector<InkStroke>> splitLines(const std::vector<InkStroke>& strokes);

/// Indices of the strokes of each line, from top to bottom
std::vector<std::vector<size_t>> lineIndices(const std::vector<InkStroke>& strokes);

/// Indices of the strokes of each block of writing: a line, cut where there is a wide horizontal gap (several
/// formulas written side by side on the same line are separate blocks). In reading order
std::vector<std::vector<size_t>> blockIndices(const std::vector<InkStroke>& strokes);

/// Render strokes black on white into a grayscale image of w×h pixels (row-major, 0 = black, 255 = white).
/// The writing is first laid out as a picture of lineHeight pixels per line with a pen of penWidth pixels (as a
/// typeset formula would be), then that picture is stretched to w×h like the image processor of the model does.
/// If debugPng is not empty, the image is also saved there
std::vector<unsigned char> renderStrokes(const std::vector<InkStroke>& strokes, int w, int h, double lineHeight,
                                         double penWidth, const std::string& debugPng = "");

/// A grayscale picture (row-major, 0 = black, 255 = white), e.g. a screenshot of a formula
struct GrayImage {
    int w = 0;
    int h = 0;
    std::vector<unsigned char> px;
};

/// The picture of an image (any cairo image surface): transparent parts become white, and light writing on a dark
/// background is inverted so that the writing is always dark
GrayImage grayFromSurface(cairo_surface_t* surface);

/// The lines of writing of a picture, cropped to the writing, from top to bottom. A line is cut where there is a
/// band of empty rows taller than half of the lines around it (fractions and exponents stay in their line)
std::vector<GrayImage> splitImageLines(const GrayImage& img);

/// A picture stretched to w×h with a white margin around the writing, as the model sees it. If debugPng is not
/// empty, the result is also saved there
std::vector<unsigned char> fitImage(const GrayImage& img, int w, int h, const std::string& debugPng = "");

/// Recognize a formula in a picture (a screenshot of a PDF, a photo of the board...). LaTeX of each line joined by
/// "\\", or nullopt with a message in error
std::optional<std::string> recognizeMathImage(const GrayImage& img, std::string& error);

/// Recognize printed text in a picture with the text recognition (OCR) of Windows. Lines joined with '\n'
std::optional<std::string> recognizeTextImage(const GrayImage& img, std::string& error);

/// Recognize a handwritten formula (one or several lines). Returns the LaTeX of each line joined by "\\\\",
/// or nullopt with a message in error
std::optional<std::string> recognizeMath(const std::vector<InkStroke>& strokes, std::string& error);

/// Recognize handwritten text (several lines are joined with '\n'). Nullopt with a message in error
std::optional<std::string> recognizeText(const std::vector<InkStroke>& strokes, std::string& error);

}  // namespace xoj::calc::ink
