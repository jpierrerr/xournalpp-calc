/*
 * Xournal++ Calc
 *
 * Handwriting → calculation region / text: the selected strokes are recognized offline and replaced by a text box
 * at the same place, in a single undo step
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
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
#include "util/Util.h"

namespace {

/// The pen strokes (not highlighter) of a layer
std::vector<Stroke*> penStrokes(Layer* layer) {
    std::vector<Stroke*> out;
    for (const Element* e: layer->getElementsView()) {
        if (e->getType() == ELEMENT_STROKE) {
            auto* s = const_cast<Stroke*>(static_cast<const Stroke*>(e));
            if (s->getToolType() == StrokeTool::PEN) {
                out.push_back(s);
            }
        }
    }
    return out;
}

xoj::calc::ink::InkStroke toInk(const Stroke* s) {
    xoj::calc::ink::InkStroke is;
    for (const auto& p: s->getPointVector()) {
        is.points.push_back({p.x, p.y});
    }
    return is;
}

/// Show a busy cursor while recognizing (about a second per block, more the first time while the model loads)
class BusyCursor {
public:
    explicit BusyCursor(GtkWindow* w) {
        win = w ? gtk_widget_get_window(GTK_WIDGET(w)) : nullptr;
        if (!win) {
            return;
        }
        GdkCursor* busy = gdk_cursor_new_from_name(gdk_window_get_display(win), "wait");
        gdk_window_set_cursor(win, busy);
        if (busy) {
            g_object_unref(busy);
        }
        gdk_display_flush(gdk_window_get_display(win));
    }
    ~BusyCursor() {
        if (win) {
            gdk_window_set_cursor(win, nullptr);
        }
    }

private:
    GdkWindow* win = nullptr;
};

}  // namespace

void Control::convertSelectedInk(bool math) {
    if (!this->win) {
        return;
    }
    flushLiveInk();
    XournalView* xournal = this->win->getXournal();
    PageRef page;
    Layer* layer = nullptr;
    std::vector<Stroke*> strokes;

    if (EditSelection* sel = xournal->getSelection()) {
        // The selected strokes: put them back on the page (with any move applied) and convert them there
        bool others = false;
        std::vector<const Element*> elements;
        for (const Element* e: sel->getElementsView()) {
            if (e->getType() == ELEMENT_STROKE) {
                elements.push_back(e);
            } else {
                others = true;
            }
        }
        if (others) {
            XojMsgBox::showMessageToUser(getGtkWindow(),
                                         "La selección tiene otros elementos además de trazos. Selecciona solo la "
                                         "escritura a mano.",
                                         GTK_MESSAGE_INFO);
            return;
        }
        page = sel->getSourcePage();
        layer = sel->getSourceLayer();
        xournal->clearSelection();
        for (const Element* e: elements) {
            if (layer->indexOf(e) != Element::InvalidIndex) {
                strokes.push_back(const_cast<Stroke*>(static_cast<const Stroke*>(e)));
            }
        }
    } else {
        // No selection: all the handwriting of the current page
        auto pageNr = getCurrentPageNo();
        if (pageNr == npos) {
            return;
        }
        this->doc->lock_shared();
        page = this->doc->getPage(pageNr);
        layer = page ? page->getSelectedLayer() : nullptr;
        if (layer) {
            strokes = penStrokes(layer);
        }
        this->doc->unlock_shared();
    }
    if (!page || !layer || strokes.empty()) {
        XojMsgBox::showMessageToUser(getGtkWindow(), "No hay escritura a mano para convertir en esta página.",
                                     GTK_MESSAGE_INFO);
        return;
    }
    convertInkStrokes(page, layer, strokes, math, true);
}

bool Control::convertInkStrokes(const PageRef& page, Layer* layer, const std::vector<Stroke*>& strokes, bool math,
                                bool interactive) {
    std::vector<xoj::calc::ink::InkStroke> ink;
    for (const Stroke* s: strokes) {
        ink.push_back(toInk(s));
    }

    struct Converted {
        std::vector<size_t> strokes;
        std::string text;
        double x;
        double bottom;  ///< bottom of the first line of writing
    };
    std::vector<Converted> results;
    std::string error;
    {
        BusyCursor busy(interactive ? getGtkWindow() : nullptr);
        for (const auto& block: xoj::calc::ink::blockIndices(ink)) {
            std::vector<xoj::calc::ink::InkStroke> blockInk;
            for (size_t i: block) {
                blockInk.push_back(ink[i]);
            }
            std::string err;
            auto recognized = math ? xoj::calc::ink::recognizeMath(blockInk, err) :
                                     xoj::calc::ink::recognizeText(blockInk, err);
            if (!recognized) {
                error = err;
                continue;
            }
            std::string text = math ? xoj::calc::latexToCalc(*recognized) : *recognized;
            g_message("Ink: %s → %s", recognized->c_str(), text.c_str());
            if (text.empty()) {
                continue;
            }
            auto box = xoj::calc::ink::boundingBox(blockInk);
            auto lines = xoj::calc::ink::splitLines(blockInk);
            double bottom = lines.empty() ? box.y1 : xoj::calc::ink::boundingBox(lines.front()).y1;
            results.push_back({block, text, box.x0, bottom});
        }
    }
    if (results.empty()) {
        if (interactive) {
            XojMsgBox::showErrorToUser(getGtkWindow(),
                                       error.empty() ? std::string("No se reconoció nada en la escritura.") : error);
        } else {
            g_message("Ink: sin conversión (%s)", error.c_str());
        }
        return false;
    }

    auto group = std::make_unique<GroupUndoAction>();

    // Remove the converted strokes (from the last position, so that the recorded positions stay right)
    std::vector<std::pair<Element::Index, Stroke*>> toRemove;
    for (const auto& r: results) {
        for (size_t i: r.strokes) {
            Element::Index pos = layer->indexOf(strokes[i]);
            if (pos != Element::InvalidIndex) {
                toRemove.emplace_back(pos, strokes[i]);
            }
        }
    }
    std::sort(toRemove.begin(), toRemove.end(), [](auto& a, auto& b) { return a.first > b.first; });
    auto del = std::make_unique<DeleteUndoAction>(page, false);
    std::vector<Element*> removed;
    this->doc->lock();
    for (auto& [pos, s]: toRemove) {
        auto ins = layer->removeElement(s);
        if (ins.e) {
            removed.push_back(ins.e.get());
            del->addElement(layer, std::move(ins.e), ins.pos);
        }
    }
    this->doc->unlock();
    for (Element* e: removed) {
        page->fireElementChanged(e);
    }
    group->addAction(std::move(del));

    // A text box at the place of each block: same left edge, baseline on the bottom of its first line
    for (const auto& r: results) {
        auto t = std::make_unique<Text>();
        t->setText(r.text);
        t->setFont(this->settings->getFont());
        t->setColor(this->toolHandler->getTool(TOOL_TEXT).getColor());
        t->setAlignment(this->toolHandler->getTool(TOOL_TEXT).getTextAlignment());
        t->setCalc(math);
        double x = r.x;
        double baseline = r.bottom;
        if (this->calcGrid) {
            auto grid = xoj::calc::alignmentGrid(page->getBackgroundType(), page->getWidth(), page->getHeight());
            t->setLineHeight(grid.pitchFor(t->naturalLineHeight()));
            x = grid.snapX(x);
            baseline = grid.snapY(r.bottom) - xoj::calc::BASELINE_LIFT;
        }
        t->setTransformation(xoj::util::Matrix::TRANSLATION(x, baseline - t->firstBaseline()));
        Text* ptr = t.get();
        this->doc->lock();
        layer->addElement(std::move(t));
        this->doc->unlock();
        page->fireElementChanged(ptr);
        group->addAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
    }
    this->undoRedo->addUndoAction(std::move(group));
    scheduleCalcUpdate();
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Live conversion: the pen strokes are converted into a calculation region after a short pause in the writing

void Control::setInkLive(bool enabled) {
    this->inkLive = enabled;
    if (!enabled) {
        flushLiveInk();
    }
}

void Control::inkStrokeStarted() {
    if (this->inkLiveSource) {
        g_source_remove(this->inkLiveSource);
        this->inkLiveSource = 0;
    }
}

void Control::inkStrokeFinished(const PageRef& page, Stroke* stroke) {
    if (!this->inkLive) {
        return;
    }
    if (this->inkLivePage && this->inkLivePage != page) {
        flushLiveInk();
    }
    this->inkLivePage = page;
    this->inkLiveStrokes.push_back(stroke);
    if (this->inkLiveSource) {
        g_source_remove(this->inkLiveSource);
    }
    this->inkLiveSource = g_timeout_add(
            INK_LIVE_DELAY_MS,
            +[](gpointer data) -> gboolean {
                auto* self = static_cast<Control*>(data);
                self->inkLiveSource = 0;
                self->flushLiveInk();
                return G_SOURCE_REMOVE;
            },
            this);
}

void Control::flushLiveInk() {
    if (this->inkLiveSource) {
        g_source_remove(this->inkLiveSource);
        this->inkLiveSource = 0;
    }
    PageRef page = std::move(this->inkLivePage);
    this->inkLivePage.reset();
    std::vector<Stroke*> pending = std::move(this->inkLiveStrokes);
    this->inkLiveStrokes.clear();
    if (!page || pending.empty()) {
        return;
    }
    // Keep only the strokes that are still on the page (they may have been erased, undone or selected)
    this->doc->lock_shared();
    Layer* layer = page->getSelectedLayer();
    std::vector<Stroke*> strokes;
    if (layer) {
        for (Stroke* s: pending) {
            if (layer->indexOf(s) != Element::InvalidIndex) {
                strokes.push_back(s);
            }
        }
    }
    this->doc->unlock_shared();
    if (!strokes.empty()) {
        convertInkStrokes(page, layer, strokes, true, false);
    }
}
