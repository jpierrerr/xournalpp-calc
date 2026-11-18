/*
 * Xournal++ Calc
 *
 * Link of the calculation regions with Excel workbooks (excel(), xl() and enviar()).
 *
 * When the workbook is open in Excel (Windows), cells are read and written in Excel itself, which recalculates its
 * formulas at once. Otherwise the .xlsx file is read and written directly.
 *
 * The values read are also kept with each region (CellCache, saved in the .xopp), so the sheet still shows its
 * results when the workbook is not available (another computer, file moved, ...).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "CalcEngine.h"
#include "XlsxFile.h"

namespace xoj::calc {

/// Values read from cells: "libro.xlsx|Hoja1!C5" → value
using CellCache = std::map<std::string, double>;

std::string serializeCellCache(const CellCache& cache);
CellCache parseCellCache(std::string_view s);

class ExcelLink: public CellLink {
public:
    /// baseDir: folder of the document (relative workbook paths start there; empty if the document is not saved).
    /// allowWrites: false to never change workbooks (exports from the command line)
    ExcelLink(std::filesystem::path baseDir, bool allowWrites);

    std::string useWorkbook(const std::string& path) override;
    double read(const std::string& ref) override;
    std::string write(const std::string& ref, double value) override;
    std::vector<std::string> takeNotes() override;

    /// Values saved with the region being evaluated (used when a workbook cannot be read), and where the values read
    /// for it are stored
    void setRegionCache(const CellCache* saved, CellCache* fresh);

    /// Absolute path of a workbook written in excel("..."): relative to the document, ".xlsx" added if there is no
    /// extension. Throws CalcError
    std::filesystem::path resolve(const std::string& path) const;

private:
    std::filesystem::path workbookFor(const CellRef& r) const;

    std::filesystem::path baseDir;
    bool allowWrites;
    std::optional<std::filesystem::path> workbook;  ///< set by excel("...")
    const CellCache* saved = nullptr;
    CellCache* fresh = nullptr;
    std::vector<std::string> notes;
};

/// Excel itself (Windows): only workbooks that are open in Excel are used, Excel is never started
namespace excelapp {

/// Whether the workbook is open in a running Excel
bool isOpen(const std::filesystem::path& file);

/// Value of a cell of an open workbook; nullopt if the workbook is not open in Excel. Throws CalcError
std::optional<double> read(const std::filesystem::path& file, const CellRef& ref);

/// Write a number in a cell of an open workbook; false if it is not open in Excel. Throws CalcError
bool write(const std::filesystem::path& file, const CellRef& ref, double value);

/// Sheet names of an open workbook; nullopt if it is not open in Excel
std::optional<std::vector<std::string>> sheetNames(const std::filesystem::path& file);

/// Cells of a sheet of an open workbook (its used range); nullopt if it is not open in Excel. Throws CalcError
std::optional<SheetPreview> preview(const std::filesystem::path& file, const std::string& sheet, int maxRows,
                                    int maxCols);

}  // namespace excelapp

}  // namespace xoj::calc
