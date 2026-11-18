/*
 * Xournal++ Calc
 *
 * Handwriting → calculation region / text: the selected strokes are recognized offline and replaced by a text box
 * at the same place, in a single undo step
 *
 * @license GNU GPLv2 or later
 */

#include <memory>
#include <string>
#include <vector>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "calc/CalcGrid.h"
#include "calc/LatexToCalc.h"
#include "calc/ink/InkRecognizer.h"
#include "control/Control.h"
#include "control/settings/Settings.h"
#include "control/tools/EditSelection.h"
#include "control/ToolHandler.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "model/Document.h"
#include "model/Element.h"
#include "model/Layer.h"
#include "model/Stroke.h"
#include "model/Text.h"
#include "model/XojPage.h"
#include "undo/DeleteUndoAction.h"
#include "undo/GroupUndoAction.h"
#include "undo/InsertUndoAction.h"
#include "undo/UndoRedoHandler.h"
#include "util/XojMsgBox.h"
#include "util/Matrix.h"

void Control::convertSelectedInk(bool math) {
    if (!this->win) {
        return;
    }
    XournalView* xournal = this->win->getXournal();
    EditSelection* sel = xournal->getSelection();
    if (!sel) {
        XojMsgBox::showMessageToUser(getGtkWindow(),
                                     math ? "Selecciona primero los trazos de la fórmula (con el lazo o el rectángulo) "
                                            "y vuelve a elegir \"Convertir escritura a ecuación\" (Ctrl+Alt+E)."
                                          : "Selecciona primero los trazos del texto (con el lazo o el rectángulo) "
                                            "y vuelve a elegir \"Convertir escritura a texto\" (Ctrl+Alt+T).",
                                     GTK_MESSAGE_INFO);
        return;
    }

    std::vector<xoj::calc::ink::InkStroke> strokes;
    bool others = false;
    for (const Element* e: sel->getElementsView()) {
        if (e->getType() != ELEMENT_STROKE) {
            others = true;
            continue;
        }
        const auto* s = static_cast<const Stroke*>(e);
        xoj::calc::ink::InkStroke is;
        for (const auto& p: s->getPointVector()) {
            is.points.push_back({p.x, p.y});
        }
        if (!is.points.empty()) {
            strokes.push_back(std::move(is));
        }
    }
    if (strokes.empty()) {
        XojMsgBox::showMessageToUser(getGtkWindow(), "La selección no tiene trazos a mano para convertir.",
                                     GTK_MESSAGE_INFO);
        return;
    }
    if (others) {
        XojMsgBox::showMessageToUser(getGtkWindow(),
                                     "La selección tiene otros elementos además de trazos. Selecciona solo la "
                                     "escritura a mano.",
                                     GTK_MESSAGE_INFO);
        return;
    }

    // Recognition takes about a second (more the first time, while the model loads): show a busy cursor
    GdkWindow* gdkWin = gtk_widget_get_window(GTK_WIDGET(getGtkWindow()));
    if (gdkWin) {
        GdkCursor* busy = gdk_cursor_new_from_name(gdk_window_get_display(gdkWin), "wait");
        gdk_window_set_cursor(gdkWin, busy);
        if (busy) {
            g_object_unref(busy);
        }
        gdk_display_flush(gdk_window_get_display(gdkWin));
        while (gtk_events_pending()) {
            gtk_main_iteration_do(false);
        }
    }
    std::string error;
    std::optional<std::string> recognized =
            math ? xoj::calc::ink::recognizeMath(strokes, error) : xoj::calc::ink::recognizeText(strokes, error);
    if (gdkWin) {
        gdk_window_set_cursor(gdkWin, nullptr);
    }
    // The selection may have been closed while the events were processed
    sel = xournal->getSelection();
    if (!sel) {
        return;
    }
    if (!recognized) {
        XojMsgBox::showErrorToUser(getGtkWindow(), error);
        return;
    }
    std::string text = math ? xoj::calc::latexToCalc(*recognized) : *recognized;
    if (text.empty()) {
        XojMsgBox::showErrorToUser(getGtkWindow(), "No se reconoció nada en la selección.");
        return;
    }
    g_message("Ink: %s → %s", recognized->c_str(), text.c_str());

    PageRef page = sel->getSourcePage();
    auto bounds = sel->getSnappedBounds();

    // Remove the strokes
    auto group = std::make_unique<GroupUndoAction>();
    auto del = std::make_unique<DeleteUndoAction>(page, false);
    sel->fillUndoItem(del.get());
    group->addAction(std::move(del));
    xournal->clearSelection();
    xournal->repaintSelection(true);

    // The text at the place of the writing: same left edge, baseline on the bottom of the first line
    auto t = std::make_unique<Text>();
    t->setText(text);
    t->setFont(this->settings->getFont());
    t->setColor(this->toolHandler->getTool(TOOL_TEXT).getColor());
    t->setAlignment(this->toolHandler->getTool(TOOL_TEXT).getTextAlignment());
    t->setCalc(math);

    auto lines = xoj::calc::ink::splitLines(strokes);
    double firstBottom = bounds.y + bounds.height;
    if (!lines.empty()) {
        firstBottom = xoj::calc::ink::boundingBox(lines.front()).y1;
        // The selection may have been moved: the strokes keep their own coordinates
        auto all = xoj::calc::ink::boundingBox(strokes);
        firstBottom += bounds.y - all.y0;
    }
    double x = bounds.x;
    double baseline = firstBottom;
    if (this->calcGrid) {
        auto grid = xoj::calc::alignmentGrid(page->getBackgroundType(), page->getWidth(), page->getHeight());
        double lineH = t->naturalLineHeight();
        t->setLineHeight(grid.pitchFor(lineH));
        x = grid.snapX(x);
        baseline = grid.snapY(firstBottom) - xoj::calc::BASELINE_LIFT;
    }
    t->setTransformation(xoj::util::Matrix::TRANSLATION(x, baseline - t->firstBaseline()));

    Text* ptr = t.get();
    this->doc->lock();
    Layer* layer = page->getSelectedLayer();
    layer->addElement(std::move(t));
    this->doc->unlock();
    page->fireElementChanged(ptr);
    group->addAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
    this->undoRedo->addUndoAction(std::move(group));
    scheduleCalcUpdate();
}
