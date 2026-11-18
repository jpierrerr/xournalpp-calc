#include "GreekPalette.h"

#include <string>

#include "control/Control.h"
#include "control/tools/TextEditor.h"
#include "util/i18n.h"

namespace xoj::popup {

namespace {

struct Section {
    const char* title;
    const char* const* symbols;
    int columns;
};

const char* const LOWER[] = {"α", "β", "γ", "δ", "ε", "ζ", "η", "θ", "ι", "κ", "λ", "μ", "ν", "ξ",    "ο",
                             "π", "ρ", "σ", "τ", "υ", "φ", "χ", "ψ", "ω", "ϵ", "ϑ", "ϕ", "ς", nullptr};
const char* const UPPER[] = {"Γ", "Δ", "Θ", "Λ", "Ξ", "Π", "Σ", "Υ", "Φ", "Ψ", "Ω", nullptr};
const char* const MATH[] = {"·", "×", "÷", "±", "√", "²", "³", "⁻¹", "≤", "≥", "≠", "≈",    "∞",
                            "∂", "∑", "∫", "∇", "°", "′", "∠", "⊥",  "∥", "→", "≔", nullptr};

void onSymbolClicked(GtkButton* button, Control* control) {
    const char* symbol = static_cast<const char*>(g_object_get_data(G_OBJECT(button), "xoj-symbol"));
    if (!symbol) {
        return;
    }
    if (auto* te = control->getTextEditor(); te) {
        te->insertText(symbol);
    } else {
        // No text box being edited: copy it, so that it can be pasted anywhere
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), symbol, -1);
    }
}

GtkWidget* makeSection(Control* control, const Section& s) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* label = gtk_label_new(nullptr);
    std::string markup = std::string("<b>") + s.title + "</b>";
    gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), label, false, false, 2);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), true);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), true);
    int i = 0;
    for (const char* const* p = s.symbols; *p; p++, i++) {
        GtkWidget* b = gtk_button_new_with_label(*p);
        gtk_widget_set_can_focus(b, false);
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        gtk_widget_set_tooltip_text(b, *p);
        g_object_set_data(G_OBJECT(b), "xoj-symbol", const_cast<char*>(*p));
        g_signal_connect(b, "clicked", G_CALLBACK(onSymbolClicked), control);
        gtk_grid_attach(GTK_GRID(grid), b, i % s.columns, i / s.columns, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(box), grid, false, false, 0);
    return box;
}

}  // namespace

GtkWidget* createGreekPalette(Control* control, GtkWindow* parent) {
    GtkWidget* win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), _("Greek letters and symbols"));
    gtk_window_set_transient_for(GTK_WINDOW(win), parent);
    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_UTILITY);
    gtk_window_set_accept_focus(GTK_WINDOW(win), false);  // keep typing in the text box
    gtk_window_set_resizable(GTK_WINDOW(win), false);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), true);
    g_signal_connect(win, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), nullptr);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(box), 8);

    const Section sections[] = {{_("Lowercase"), LOWER, 7}, {_("Uppercase"), UPPER, 7}, {_("Math"), MATH, 8}};
    for (const auto& s: sections) {
        gtk_box_pack_start(GTK_BOX(box), makeSection(control, s), false, false, 0);
    }

    GtkWidget* hint = gtk_label_new(_("Tip: in a text box, type a latin letter and press Ctrl+G (a→α, q→θ, W→Ω)"));
    gtk_label_set_line_wrap(GTK_LABEL(hint), true);
    gtk_label_set_max_width_chars(GTK_LABEL(hint), 40);
    gtk_widget_set_sensitive(hint, false);
    gtk_box_pack_start(GTK_BOX(box), hint, false, false, 4);

    gtk_container_add(GTK_CONTAINER(win), box);
    return win;
}

}  // namespace xoj::popup
