/*
 * Xournal++ Calc
 *
 * Evaluation of the calculation regions of a document, in reading order
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "CalcEngine.h"

class Document;

namespace xoj::calc {

/// Escape text for Pango markup and typeset it as math: x^2 → x², A_s → A with subscript s, * → ·
std::string prettifyMarkup(std::string_view source);

/// Pango markup for an evaluated block: sources are typeset, results in blue and errors in red
std::string buildMarkup(const std::vector<LineResult>& lines);

/**
 * Evaluate every calculation region of the document in reading order: page by page, and on each page from top to
 * bottom (then left to right). Variables defined in a region are available in all regions further down.
 *
 * The document must not be locked by the caller.
 *
 * @return the indices of the pages whose display changed
 */
std::vector<size_t> recalculateDocument(Document* doc);

}  // namespace xoj::calc
