/*
 * Xournal++ Calc
 *
 * Evaluation of the calculation regions of a document, in reading order
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "CalcEngine.h"

class Document;

namespace xoj::calc {

/// Escape text for Pango markup and typeset it as math: x^2 → x², A_s → A with subscript s, * → ·
/// A unit with a power written without "^" (m3) is shown as m³, unless it is a variable of env
std::string prettifyMarkup(std::string_view source, const Environment* env = nullptr);

/// Pango markup for an evaluated block: sources are typeset, results in blue and errors in red
std::string buildMarkup(const std::vector<LineResult>& lines, const Environment* env = nullptr);

/**
 * Evaluate every calculation region of the document in reading order: page by page, and on each page from top to
 * bottom (then left to right). Variables defined in a region are available in all regions further down.
 *
 * Excel cells are read and written as the regions say (excel(), xl(), enviar()). Workbooks are only changed if
 * excelWrites is true.
 *
 * The document must not be locked by the caller.
 *
 * @return the indices of the pages whose display changed
 */
std::vector<size_t> recalculateDocument(Document* doc, bool excelWrites = true, bool keepIfExcelBusy = false);

/// Whether some calculation region of the document uses Excel (xl() or enviar())
bool documentUsesExcel(Document* doc);

/// The variables defined in the calculation regions of the document (evaluated without writing to Excel)
Environment documentVariables(Document* doc);

/// The text of the calculation regions in reading order, and (if baseDir is given) the folder of the document
std::vector<std::string> documentSources(Document* doc, std::filesystem::path* baseDir = nullptr);

}  // namespace xoj::calc
