#include "FunctionCompletion.h"

#include <algorithm>
#include <string>

#include <glib.h>

#include "util/glib_casts.h"
#include "util/gtk4_helper.h"

#include "MainWindow.h"
#include "XournalView.h"

namespace {
std::string escape(const char* s) {
    gchar* e = g_markup_escape_text(s, -1);
    std::string r(e);
    g_free(e);
    return r;
}
}  // namespace

FunctionCompletion::FunctionCompletion(MainWindow* mainWindow, Callback onChoose):
        mainWindow(mainWindow),
        overlay(GTK_OVERLAY(gtk_widget_get_ancestor(mainWindow->getXournal()->getWidget(), GTK_TYPE_OVERLAY))),
        widget(gtk_frame_new(nullptr), xoj::util::adopt),
        list(GTK_LIST_BOX(gtk_list_box_new())),
        onChoose(std::move(onChoose)) {
    gtk_widget_set_can_focus(widget.get(), false);
    gtk_widget_set_can_focus(GTK_WIDGET(list), false);
    gtk_list_box_set_selection_mode(list, GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(list, true);
    gtk_container_add(GTK_CONTAINER(widget.get()), GTK_WIDGET(list));
    gtk_widget_set_halign(widget.get(), GTK_ALIGN_START);
    gtk_widget_set_valign(widget.get(), GTK_ALIGN_START);
    g_signal_connect(list, "row-activated", G_CALLBACK(rowActivated), this);

    // A small shadow, so that the list stands out over the page
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
                                    "frame.calc-completion { background-color: @theme_base_color; "
                                    "box-shadow: 0 2px 6px rgba(0,0,0,0.35); }"
                                    "frame.calc-completion row { padding: 3px 8px; }",
                                    -1, nullptr);
    gtk_style_context_add_provider(gtk_widget_get_style_context(widget.get()), GTK_STYLE_PROVIDER(css),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    gtk_style_context_add_class(gtk_widget_get_style_context(widget.get()), "calc-completion");
    g_object_unref(css);

    positionSignal = g_signal_connect(overlay, "get-child-position",
                                      xoj::util::wrap_for_g_callback_v<getOverlayPosition>, this);
    gtk_overlay_add_overlay(overlay, widget.get());
    gtk_widget_hide(widget.get());
}

FunctionCompletion::~FunctionCompletion() {
    g_signal_handler_disconnect(overlay, positionSignal);
    gtk_overlay_remove_overlay(overlay, widget.get());
}

void FunctionCompletion::show(const std::vector<const xoj::calc::FunctionInfo*>& fs, xoj::util::Point<int> pos,
                              bool highlight) {
    if (fs.empty()) {
        hide();
        return;
    }
    if (fs != this->functions) {
        this->functions = fs;
        gtk_widget_hide(widget.get());  // so that the list gets the size of its new rows
        GList* children = gtk_container_get_children(GTK_CONTAINER(list));
        for (GList* c = children; c; c = c->next) {
            gtk_widget_destroy(GTK_WIDGET(c->data));
        }
        g_list_free(children);
        for (const auto* f: fs) {
            // SENO(ángulo)   Seno (se muestra sen)
            std::string syntax = f->syntax;
            std::string name = f->name;
            std::string markup = "<b>" + escape(name.c_str()) + "</b>" + escape(syntax.substr(name.size()).c_str()) +
                                 "   <span alpha=\"60%\" size=\"smaller\">" + escape(f->description) +
                                 "</span>";
            GtkWidget* label = gtk_label_new(nullptr);
            gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
            gtk_widget_set_halign(label, GTK_ALIGN_START);
            GtkWidget* row = gtk_list_box_row_new();
            gtk_widget_set_can_focus(row, false);
            gtk_container_add(GTK_CONTAINER(row), label);
            gtk_list_box_insert(list, row, -1);
        }
    }
    this->position = pos;
    this->current = highlight ? 0 : -1;
    if (current >= 0) {
        gtk_list_box_select_row(list, gtk_list_box_get_row_at_index(list, current));
    } else {
        gtk_list_box_unselect_all(list);
    }
    gtk_widget_show_all(widget.get());
    gtk_widget_queue_resize(GTK_WIDGET(overlay));
}

void FunctionCompletion::hide() {
    if (gtk_widget_get_visible(widget.get())) {
        gtk_widget_hide(widget.get());
    }
    current = -1;
}

bool FunctionCompletion::isVisible() const { return gtk_widget_get_visible(widget.get()); }

void FunctionCompletion::moveHighlight(int delta) {
    const int n = static_cast<int>(functions.size());
    if (n == 0) {
        return;
    }
    if (current < 0) {
        current = delta > 0 ? 0 : n - 1;
    } else {
        current = (current + delta + n) % n;
    }
    gtk_list_box_select_row(list, gtk_list_box_get_row_at_index(list, current));
}

auto FunctionCompletion::highlighted() const -> const xoj::calc::FunctionInfo* {
    return current >= 0 && current < static_cast<int>(functions.size()) ? functions[static_cast<size_t>(current)] :
                                                                           nullptr;
}

void FunctionCompletion::rowActivated(GtkListBox*, GtkListBoxRow* row, FunctionCompletion* self) {
    const int i = gtk_list_box_row_get_index(row);
    if (i >= 0 && i < static_cast<int>(self->functions.size())) {
        const xoj::calc::FunctionInfo* f = self->functions[static_cast<size_t>(i)];
        self->hide();
        self->onChoose(*f);
    }
}

bool FunctionCompletion::getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation,
                                            FunctionCompletion* self) {
    if (self->widget.get() != widget) {
        return false;
    }
    GtkRequisition natural;
    gtk_widget_get_preferred_size(widget, nullptr, &natural);
    allocation->width = natural.width;
    allocation->height = natural.height;

    GtkWidget* scrolledWindow =
            gtk_widget_get_ancestor(self->mainWindow->getXournal()->getWidget(), GTK_TYPE_SCROLLED_WINDOW);
    int x = 0, y = 0;
    gtk_widget_translate_coordinates(scrolledWindow, GTK_WIDGET(overlay), self->position.x, self->position.y, &x, &y);
    // Keep the list inside the window
    const int w = gtk_widget_get_allocated_width(GTK_WIDGET(overlay));
    const int h = gtk_widget_get_allocated_height(GTK_WIDGET(overlay));
    allocation->x = std::max(0, std::min(x, w - allocation->width));
    allocation->y = y + allocation->height > h ? std::max(0, y - allocation->height - 30) : y;
    return true;
}
