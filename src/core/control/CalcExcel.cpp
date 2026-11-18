/*
 * Xournal++ Calc
 *
 * Excel tool: link a workbook, and insert the lines that read a cell into a variable or send a variable to a cell
 * (see calc/ExcelLink.h for the syntax)
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtk/gtk.h>

#include "calc/CalcDocument.h"
#include "calc/CalcEngine.h"
#include "calc/CalcGrid.h"
#include "calc/ExcelLink.h"
#include "calc/XlsxFile.h"
#include "control/Control.h"
#include "control/ToolHandler.h"
#include "control/settings/Settings.h"
#include "control/tools/TextEditor.h"
#include "gui/dialog/ExcelCellDialog.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/Matrix.h"
#include "util/XojMsgBox.h"

namespace fs = std::filesystem;

namespace {

/// The workbook of the last excel("...") of the document, if any
std::optional<fs::path> documentWorkbook(Document* doc) {
    fs::path baseDir;
    auto sources = xoj::calc::documentSources(doc, &baseDir);
    std::optional<std::string> last;
    for (const auto& src: sources) {
        std::string_view rest(src);
        while (!rest.empty()) {
            size_t nl = rest.find('\n');
            if (auto book = xoj::calc::excelStatement(rest.substr(0, nl))) {
                last = std::move(book);
            }
            rest = nl == std::string_view::npos ? std::string_view() : rest.substr(nl + 1);
        }
    }
    if (!last) {
        return std::nullopt;
    }
    try {
        return xoj::calc::ExcelLink(baseDir, false).resolve(*last);
    } catch (const xoj::calc::CalcError&) {
        return std::nullopt;
    }
}

}  // namespace

fs::path Control::excelLinkWorkbook() {
    try {
        return chooseExcelWorkbook();
    } catch (const std::exception& e) {
        XojMsgBox::showErrorToUser(getGtkWindow(), std::string("No se pudo ligar el libro de Excel: ") + e.what());
        return {};
    }
}

fs::path Control::chooseExcelWorkbook() {
    GtkFileChooserNative* fc = gtk_file_chooser_native_new("Ligar libro de Excel", getGtkWindow(),
                                                           GTK_FILE_CHOOSER_ACTION_OPEN, "_Ligar", "_Cancelar");
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Libros de Excel (*.xlsx, *.xlsm)");
    gtk_file_filter_add_pattern(filter, "*.xlsx");
    gtk_file_filter_add_pattern(filter, "*.xlsm");
    gtk_file_filter_add_pattern(filter, "*.XLSX");
    gtk_file_filter_add_pattern(filter, "*.XLSM");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(fc), filter);

    this->doc->lock_shared();
    fs::path docPath = this->doc->getFilepath();
    this->doc->unlock_shared();
    const fs::path docDir = docPath.empty() ? fs::path() : docPath.parent_path();
    if (!docDir.empty()) {
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(fc), xoj::calc::pathToUtf8(docDir).c_str());
    }

    fs::path chosen;
    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(fc)) == GTK_RESPONSE_ACCEPT) {
        if (gchar* f = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc))) {
            chosen = xoj::calc::pathFromUtf8(f);
            g_free(f);
        }
    }
    g_object_unref(fc);
    if (chosen.empty()) {
        return {};
    }

    // The full path: it is shown when the region is edited (the sheet shows only the name). If the folder is moved,
    // the workbook with the same name next to the document is used
    std::string written = xoj::calc::pathToUtf8(chosen);
    insertCalcLine("excel(\"" + written + "\")");
    return chosen;
}

void Control::excelPickCell(bool input) {
    // An unexpected error (out of memory, a file system error) must not escape into GTK: it would end the program
    try {
        auto workbook = documentWorkbook(this->doc);
        if (!workbook) {
            fs::path chosen = excelLinkWorkbook();
            if (chosen.empty()) {
                return;
            }
            workbook = chosen;
        }

        std::vector<xoj::popup::ExcelVariable> variables;
        if (!input) {
            auto env = xoj::calc::documentVariables(this->doc);
            for (const auto& [name, q]: env.vars) {
                std::string shown = xoj::calc::formatQuantity(q);
                size_t sp = shown.find(' ');
                variables.push_back({name, sp == std::string::npos ? std::string() : shown.substr(sp + 1), q.dim});
            }
        }

        auto choice = xoj::popup::pickExcelCell(getGtkWindow(), *workbook, input, variables);
        if (!choice) {
            return;
        }
        std::string line;
        if (input) {
            line = choice->variable + " := xl(\"" + choice->ref + "\")" +
                   (choice->unit.empty() ? "" : "·" + choice->unit);
            if (choice->showValue) {
                line += " =";
            }
        } else {
            line = "enviar(" + choice->variable + ", \"" + choice->ref + "\"" +
                   (choice->unit.empty() ? "" : ", " + choice->unit) + ")";
        }
        insertCalcLine(line);
    } catch (const std::exception& e) {
        XojMsgBox::showErrorToUser(getGtkWindow(), std::string("No se pudo usar el libro de Excel: ") + e.what());
    }
}

void Control::insertCalcLine(const std::string& line) {
    if (auto* te = getTextEditor(); te && te->getTextElement() && te->getTextElement()->isCalc()) {
        te->insertLine(line);
        return;
    }

    // A new calculation region on the current page, on the writing line below the lowest region
    this->doc->lock_shared();
    PageRef page = this->doc->getPage(getCurrentPageNo());
    this->doc->unlock_shared();
    if (!page) {
        return;
    }
    Layer* layer = page->getSelectedLayer();
    auto grid = xoj::calc::alignmentGrid(page->getBackgroundType(), page->getWidth(), page->getHeight());
    const double pitch = grid.linePitch();

    double x = grid.x0 > 0 ? grid.x0 : 2 * grid.dx;
    double writingLine = (grid.y0 > 0 ? grid.y0 : pitch) + pitch;
    this->doc->lock_shared();
    double lowest = -1;
    for (Layer* l: page->getLayers()) {
        for (auto& e: l->getElements()) {
            if (e->getType() != ELEMENT_TEXT) {
                continue;
            }
            auto* t = dynamic_cast<Text*>(e.get());
            if (!t || !t->isCalc()) {
                continue;
            }
            const auto& box = t->getBoundingBox();
            const double bottom = box.y + box.height;
            if (bottom > lowest) {
                lowest = bottom;
                x = box.x;
            }
        }
    }
    this->doc->unlock_shared();
    if (lowest >= 0) {
        writingLine = grid.snapY(lowest) + pitch;
        while (writingLine - xoj::calc::BASELINE_LIFT <= lowest) {
            writingLine += pitch;
        }
    }

    auto t = std::make_unique<Text>();
    t->setText(line);
    t->setFont(this->settings->getFont());
    t->setColor(this->toolHandler->getTool(TOOL_TEXT).getColor());
    t->setCalc(true);
    t->setLineHeight(grid.pitchFor(t->naturalLineHeight()));
    t->setTransformation(
            xoj::util::Matrix::TRANSLATION(x, writingLine - xoj::calc::BASELINE_LIFT - t->firstBaseline()));
    Text* ptr = t.get();
    this->doc->lock();
    layer->addElement(std::move(t));
    this->doc->unlock();
    page->fireElementChanged(ptr);
    this->undoRedo->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
    scheduleCalcUpdate();
}
