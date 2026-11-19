/*
 * Xournal++ Calc
 *
 * Dialog to pick a cell of an Excel workbook: sheet, a preview of its cells (like a spreadsheet), and the variable
 * that reads the cell (input) or is sent to it (output).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "calc/CalcEngine.h"

namespace xoj::popup {

struct ExcelVariable {
    std::string name;
    std::string unit;  ///< unit it is shown in (e.g. "kN·m"), "" if dimensionless
    xoj::calc::Dim dim;
};

struct ExcelCellChoice {
    std::string ref;        ///< e.g. "Hoja1!C2" or "'Mi hoja'!C2"
    std::string variable;   ///< variable name (input) or variable sent (output)
    std::string unit;       ///< "" for none
    bool showValue = true;  ///< input: append "=" to show the value
};

/// Show the dialog (modal). nullopt if cancelled or if the workbook cannot be read (an error is shown)
std::optional<ExcelCellChoice> pickExcelCell(GtkWindow* parent, const std::filesystem::path& workbook, bool input,
                                             const std::vector<ExcelVariable>& variables);

}  // namespace xoj::popup
