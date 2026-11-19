/*
 * Xournal++ Calc
 *
 * Read and write numbers in the cells of an Excel workbook file (.xlsx / .xlsm), without Excel.
 *
 * Only the cells that are written change: the rest of the file is copied as it is, and the workbook is marked to be
 * recalculated when Excel opens it (the values of formulas saved in the file are those of the last time it was saved
 * by Excel).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace xoj::calc {

/// A cell reference split in its parts: "[libro.xlsx]Hoja1!C5", "'Mi hoja'!$C$5", "C5"
struct CellRef {
    std::string book;   ///< workbook named in the reference ("" = the current one)
    std::string sheet;  ///< "" = the first sheet
    std::string cell;   ///< normalized: upper case, without '$', e.g. "C5"
    int row = 0;        ///< 1-based
    int col = 0;        ///< 1-based (A = 1)

    /// "Hoja1!C5" (or "C5"), as it is shown
    std::string toString() const;
};

/// Parse a reference. Throws CalcError if it is not valid
CellRef parseCellRef(const std::string& ref);

/// UTF-8 text of a path, and the other way round
std::string pathToUtf8(const std::filesystem::path& p);
std::filesystem::path pathFromUtf8(const std::string& s);

/// Column letters for a 1-based column number (1 → "A", 28 → "AB")
std::string columnName(int col);

/// Contents of a sheet as they are shown, to pick a cell: (row, column) → text, 1-based
struct SheetPreview {
    std::map<std::pair<int, int>, std::string> cells;
    int rows = 0;  ///< last row with content
    int cols = 0;  ///< last column with content
};

namespace xlsx {

/// Names of the sheets of a workbook, in order. Throws CalcError
std::vector<std::string> sheetNames(const std::filesystem::path& file);

/// The cells with content of a sheet ("" = the first one), up to maxRows × maxCols. Throws CalcError
SheetPreview preview(const std::filesystem::path& file, const std::string& sheet, int maxRows, int maxCols);

/// Number in a cell. Throws CalcError (missing file, sheet or cell, text that is not a number, ...)
double read(const std::filesystem::path& file, const CellRef& ref);

/// Write a number in a cell (creating it if needed). Does nothing if the cell already has that value.
/// Throws CalcError (for example if the cell has a formula, or the file is open in another program)
void write(const std::filesystem::path& file, const CellRef& ref, double value);

}  // namespace xlsx

}  // namespace xoj::calc
