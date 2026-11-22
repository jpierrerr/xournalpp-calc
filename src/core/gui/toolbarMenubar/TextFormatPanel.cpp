#include "TextFormatPanel.h"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#include "control/Control.h"
#include "control/settings/Settings.h"
#include "model/Font.h"
#include "control/tools/TextEditor.h"
#include "enums/Action.enum.h"
#include "gui/MainWindow.h"
#include "gui/XournalView.h"
#include "model/TextAlignment.h"
#include "model/TextFormat.h"
#include "util/i18n.h"
#include "util/raii/GVariantSPtr.h"

using xoj::text::FormatKind;

namespace {

struct ColorChoice {
    const char* hex;
    const char* name;
};

const std::array<ColorChoice, 15> HIGHLIGHTS{{{"#ffff00", N_("Yellow")},
                                              {"#00ff00", N_("Bright green")},
                                              {"#00ffff", N_("Turquoise")},
                                              {"#ff00ff", N_("Pink")},
                                              {"#0000ff", N_("Blue")},
                                              {"#ff0000", N_("Red")},
                                              {"#000080", N_("Dark blue")},
                                              {"#008080", N_("Teal")},
                                              {"#008000", N_("Green")},
                                              {"#800080", N_("Violet")},
                                              {"#800000", N_("Dark red")},
                                              {"#808000", N_("Dark yellow")},
                                              {"#808080", N_("Gray")},
                                              {"#c0c0c0", N_("Light gray")},
                                              {"#000000", N_("Black")}}};

const std::array<ColorChoice, 15> FONT_COLORS{{{"#000000", N_("Black")},
                                               {"#404040", N_("Dark gray")},
                                               {"#808080", N_("Gray")},
                                               {"#c00000", N_("Dark red")},
                                               {"#ff0000", N_("Red")},
                                               {"#ffc000", N_("Orange")},
                                               {"#ffff00", N_("Yellow")},
                                               {"#92d050", N_("Light green")},
                                               {"#00b050", N_("Green")},
                                               {"#00b0f0", N_("Light blue")},
                                               {"#0070c0", N_("Blue")},
                                               {"#002060", N_("Dark blue")},
                                               {"#7030a0", N_("Purple")},
                                               {"#ff00ff", N_("Pink")},
                                               {"#ffffff", N_("White")}}};

const std::array<double, 16> SIZES{8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72};

/// Everything a panel needs; freed with the panel
struct Panel {
    Control* control = nullptr;
    GtkWidget* root = nullptr;
    int listener = 0;
    bool updating = false;
    GtkWidget* family = nullptr;
    GtkWidget* size = nullptr;
    std::vector<std::pair<GtkWidget*, std::pair<FormatKind, std::string>>> toggles;
    GtkWidget* highlightLabel = nullptr;
    GtkWidget* colorLabel = nullptr;
    std::string highlight = "#ffff00";
    std::string fontColor = "#ff0000";

    TextEditor* editor() const { return control->getTextEditor(); }

    /// Give the keyboard back to the page, so that typing goes on in the text
    void refocus() const {
        if (auto* win = control->getWindow(); win) {
            gtk_widget_grab_focus(win->getXournal()->getWidget());
        }
    }

    void update();
};

void setMarkup(GtkWidget* label, const std::string& markup) { gtk_label_set_markup(GTK_LABEL(label), markup.c_str()); }

void Panel::update() {
    TextEditor* te = editor();
    gtk_widget_set_sensitive(root, te != nullptr);
    // Font and size: the one being edited, or else the one new text will get (never empty boxes)
    std::string fam;
    double fontSize = 0;
    if (te) {
        fam = te->currentFamily();
        fontSize = te->currentSize();
    } else {
        const XojFont& f = control->getSettings()->getFont();
        fam = f.getName();
        fontSize = f.getSize();
    }
    updating = true;
    if (family) {
        GtkWidget* entry = gtk_bin_get_child(GTK_BIN(family));
        if (fam != gtk_entry_get_text(GTK_ENTRY(entry))) {
            gtk_entry_set_text(GTK_ENTRY(entry), fam.c_str());
        }
    }
    if (size) {
        GtkWidget* entry = gtk_bin_get_child(GTK_BIN(size));
        char buf[32];
        g_snprintf(buf, sizeof(buf), "%g", fontSize);
        if (std::string(buf) != gtk_entry_get_text(GTK_ENTRY(entry))) {
            gtk_entry_set_text(GTK_ENTRY(entry), buf);
        }
    }
    if (!te) {
        updating = false;
        return;
    }
    for (auto& [w, kv]: toggles) {
        auto v = te->formatState(kv.first);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), v && (kv.second.empty() || *v == kv.second));
    }
    updating = false;
}

GtkWidget* flatButton(GtkWidget* child, const char* tooltip, bool toggle = false) {
    GtkWidget* b = toggle ? gtk_toggle_button_new() : gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(b, false);
    gtk_widget_set_focus_on_click(b, false);
    gtk_container_add(GTK_CONTAINER(b), child);
    gtk_widget_set_tooltip_text(b, tooltip);
    return b;
}

GtkWidget* markupLabel(const std::string& markup) {
    GtkWidget* l = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(l), markup.c_str());
    gtk_widget_set_size_request(l, 18, -1);
    return l;
}

/// A popover with a grid of colors under a small arrow button. pick(hex or "" for none)
GtkWidget* colorMenuButton(Panel* p, const ColorChoice* colors, size_t n, const char* noneLabel, const char* tooltip,
                           void (*pick)(Panel*, const std::string&)) {
    GtkWidget* mb = gtk_menu_button_new();
    gtk_button_set_relief(GTK_BUTTON(mb), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(mb, false);
    gtk_widget_set_tooltip_text(mb, tooltip);
    gtk_container_add(GTK_CONTAINER(mb), gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_MENU));
    GtkWidget* pop = gtk_popover_new(mb);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(box), 6);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    for (size_t i = 0; i < n; i++) {
        GtkWidget* swatch = gtk_label_new(nullptr);
        std::string m = std::string("<span background=\"") + colors[i].hex + "\">      </span>";
        gtk_label_set_markup(GTK_LABEL(swatch), m.c_str());
        GtkWidget* b = flatButton(swatch, _(colors[i].name));
        g_object_set_data_full(G_OBJECT(b), "jm-color", g_strdup(colors[i].hex), g_free);
        g_object_set_data(G_OBJECT(b), "jm-pick", reinterpret_cast<gpointer>(pick));
        g_object_set_data(G_OBJECT(b), "jm-pop", pop);
        g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton* b, Panel* p) {
                             auto f = reinterpret_cast<void (*)(Panel*, const std::string&)>(
                                     g_object_get_data(G_OBJECT(b), "jm-pick"));
                             gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(b), "jm-pop")));
                             f(p, static_cast<const char*>(g_object_get_data(G_OBJECT(b), "jm-color")));
                         }),
                         p);
        gtk_grid_attach(GTK_GRID(grid), b, static_cast<int>(i % 5), static_cast<int>(i / 5), 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(box), grid, false, false, 0);
    GtkWidget* none = gtk_button_new_with_label(noneLabel);
    g_object_set_data(G_OBJECT(none), "jm-pick", reinterpret_cast<gpointer>(pick));
    g_object_set_data(G_OBJECT(none), "jm-pop", pop);
    g_signal_connect(none, "clicked", G_CALLBACK(+[](GtkButton* b, Panel* p) {
                         auto f = reinterpret_cast<void (*)(Panel*, const std::string&)>(
                                 g_object_get_data(G_OBJECT(b), "jm-pick"));
                         gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(b), "jm-pop")));
                         f(p, "");
                     }),
                     p);
    gtk_box_pack_start(GTK_BOX(box), none, false, false, 0);
    gtk_widget_show_all(box);
    gtk_container_add(GTK_CONTAINER(pop), box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
    return mb;
}

/// A small menu button with text entries; pick(index)
GtkWidget* listMenuButton(Panel* p, GtkWidget* face, const std::vector<std::string>& labels, const char* tooltip,
                          void (*pick)(Panel*, int)) {
    GtkWidget* mb = gtk_menu_button_new();
    gtk_button_set_relief(GTK_BUTTON(mb), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(mb, false);
    gtk_widget_set_tooltip_text(mb, tooltip);
    gtk_container_add(GTK_CONTAINER(mb), face);
    GtkWidget* pop = gtk_popover_new(mb);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(box), 4);
    for (size_t i = 0; i < labels.size(); i++) {
        GtkWidget* l = gtk_label_new(nullptr);
        gtk_label_set_markup(GTK_LABEL(l), labels[i].c_str());
        gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
        GtkWidget* b = flatButton(l, nullptr);
        g_object_set_data(G_OBJECT(b), "jm-index", GINT_TO_POINTER(static_cast<int>(i)));
        g_object_set_data(G_OBJECT(b), "jm-pick", reinterpret_cast<gpointer>(pick));
        g_object_set_data(G_OBJECT(b), "jm-pop", pop);
        g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton* b, Panel* p) {
                             auto f = reinterpret_cast<void (*)(Panel*, int)>(g_object_get_data(G_OBJECT(b), "jm-pick"));
                             gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(b), "jm-pop")));
                             f(p, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "jm-index")));
                         }),
                         p);
        gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
    }
    gtk_widget_show_all(box);
    gtk_container_add(GTK_CONTAINER(pop), box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), pop);
    return mb;
}

void addToggle(Panel* p, GtkWidget* box, const std::string& markup, const char* tooltip, FormatKind kind,
               const std::string& value) {
    GtkWidget* b = flatButton(markupLabel(markup), tooltip, true);
    g_object_set_data_full(G_OBJECT(b), "jm-value", g_strdup(value.c_str()), g_free);
    g_object_set_data(G_OBJECT(b), "jm-kind", GINT_TO_POINTER(static_cast<int>(kind)));
    g_signal_connect(b, "toggled", G_CALLBACK(+[](GtkToggleButton* b, Panel* p) {
                         if (p->updating) {
                             return;
                         }
                         if (auto* te = p->editor(); te) {
                             te->toggleFormat(static_cast<FormatKind>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(b), "jm-kind"))),
                                              static_cast<const char*>(g_object_get_data(G_OBJECT(b), "jm-value")));
                         }
                         p->refocus();
                         p->update();
                     }),
                     p);
    p->toggles.emplace_back(b, std::make_pair(kind, value));
    gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
}

GtkWidget* actionButton(const char* icon, const char* tooltip, Action a, const xoj::util::GVariantSPtr& target) {
    GtkWidget* b = gtk_toggle_button_new();
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    gtk_widget_set_can_focus(b, false);
    gtk_container_add(GTK_CONTAINER(b), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_SMALL_TOOLBAR));
    gtk_widget_set_tooltip_text(b, tooltip);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(b), (std::string("win.") + Action_toString(a)).c_str());
    if (target) {
        gtk_actionable_set_action_target_value(GTK_ACTIONABLE(b), target.get());
    }
    return b;
}

void setHighlightFace(Panel* p) {
    setMarkup(p->highlightLabel, "<span background=\"" + p->highlight + "\">ab</span>");
}
void setColorFace(Panel* p) {
    setMarkup(p->colorLabel, "<span foreground=\"" + p->fontColor + "\" underline=\"single\" underline_color=\"" +
                                     p->fontColor + "\"><b>A</b></span>");
}

GtkWidget* separator(bool horizontal) {
    GtkWidget* s = gtk_separator_new(horizontal ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_start(s, 3);
    gtk_widget_set_margin_end(s, 3);
    return s;
}

GtkWidget* fillFamilies(GtkWidget* combo) {
    PangoFontFamily** families = nullptr;
    int n = 0;
    pango_context_list_families(gtk_widget_get_pango_context(combo), &families, &n);
    std::vector<std::string> names;
    for (int i = 0; i < n; i++) {
        names.emplace_back(pango_font_family_get_name(families[i]));
    }
    g_free(families);
    std::sort(names.begin(), names.end(),
              [](const auto& a, const auto& b) { return g_utf8_collate(a.c_str(), b.c_str()) < 0; });
    for (const auto& nm: names) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), nm.c_str());
    }
    return combo;
}

}  // namespace

namespace xoj::text {

GtkWidget* createFormatPanel(Control* control, bool horizontal, bool mini) {
    auto* p = new Panel();
    p->control = control;

    GtkWidget* row1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget* row2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    // Font and size
    p->family = fillFamilies(gtk_combo_box_text_new_with_entry());
    gtk_widget_set_tooltip_text(p->family, _("Font"));
    gtk_entry_set_width_chars(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(p->family))), mini ? 10 : 14);
    p->size = gtk_combo_box_text_new_with_entry();
    gtk_widget_set_tooltip_text(p->size, _("Font size"));
    gtk_entry_set_width_chars(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(p->size))), 3);
    for (double s: SIZES) {
        char buf[16];
        g_snprintf(buf, sizeof(buf), "%g", s);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(p->size), buf);
    }
    auto applyFamily = +[](GtkWidget*, Panel* p) {
        if (p->updating) {
            return;
        }
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(p->family)) < 0 && !gtk_widget_has_focus(gtk_bin_get_child(GTK_BIN(p->family)))) {
            return;
        }
        const char* t = gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(p->family))));
        if (auto* te = p->editor(); te && t && *t) {
            te->setFormatFamily(t);
        }
        p->refocus();
    };
    auto applySize = +[](GtkWidget*, Panel* p) {
        if (p->updating) {
            return;
        }
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(p->size)) < 0 && !gtk_widget_has_focus(gtk_bin_get_child(GTK_BIN(p->size)))) {
            return;
        }
        const char* t = gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(p->size))));
        double v = g_ascii_strtod(t, nullptr);
        if (auto* te = p->editor(); te && v > 0) {
            te->setFormatSize(v);
        }
        p->refocus();
    };
    g_signal_connect(p->family, "changed", G_CALLBACK(applyFamily), p);
    g_signal_connect(gtk_bin_get_child(GTK_BIN(p->family)), "activate",
                     G_CALLBACK(+[](GtkEntry*, Panel* p) {
                         if (auto* te = p->editor(); te) {
                             te->setFormatFamily(gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(p->family)))));
                         }
                         p->refocus();
                     }),
                     p);
    g_signal_connect(p->size, "changed", G_CALLBACK(applySize), p);
    g_signal_connect(gtk_bin_get_child(GTK_BIN(p->size)), "activate", G_CALLBACK(+[](GtkEntry* e, Panel* p) {
                         double v = g_ascii_strtod(gtk_entry_get_text(e), nullptr);
                         if (auto* te = p->editor(); te && v > 0) {
                             te->setFormatSize(v);
                         }
                         p->refocus();
                     }),
                     p);
    gtk_box_pack_start(GTK_BOX(row1), p->family, false, false, 0);
    gtk_box_pack_start(GTK_BOX(row1), p->size, false, false, 2);

    // Bigger / smaller
    GtkWidget* grow = flatButton(markupLabel("A<sup>▲</sup>"), _("Increase font size"));
    GtkWidget* shrink = flatButton(markupLabel("<small>A</small><sup>▼</sup>"), _("Decrease font size"));
    g_signal_connect(grow, "clicked", G_CALLBACK(+[](GtkButton*, Panel* p) {
                         if (auto* te = p->editor(); te) {
                             te->growFont(true);
                         }
                         p->update();
                     }),
                     p);
    g_signal_connect(shrink, "clicked", G_CALLBACK(+[](GtkButton*, Panel* p) {
                         if (auto* te = p->editor(); te) {
                             te->growFont(false);
                         }
                         p->update();
                     }),
                     p);
    gtk_box_pack_start(GTK_BOX(row1), grow, false, false, 0);
    gtk_box_pack_start(GTK_BOX(row1), shrink, false, false, 0);

    if (!mini) {
        // Case
        std::vector<std::string> cases{_("Sentence case."), _("lowercase"), _("UPPERCASE"), _("Capitalize Each Word"),
                                       _("tOGGLE cASE")};
        GtkWidget* face = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        gtk_box_pack_start(GTK_BOX(face), markupLabel("Aa"), false, false, 0);
        gtk_box_pack_start(GTK_BOX(face), gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_MENU), false,
                           false, 0);
        gtk_box_pack_start(GTK_BOX(row1),
                           listMenuButton(p, face, cases, _("Change case"), +[](Panel* p, int i) {
                               if (auto* te = p->editor(); te) {
                                   te->changeCase(static_cast<TextEditor::CaseChange>(i));
                               }
                               p->refocus();
                           }),
                           false, false, 0);
        // Clear formatting
        GtkWidget* clear = flatButton(gtk_image_new_from_icon_name("xopp-format-clear", GTK_ICON_SIZE_SMALL_TOOLBAR),
                                      _("Clear all formatting"));
        g_signal_connect(clear, "clicked", G_CALLBACK(+[](GtkButton*, Panel* p) {
                             if (auto* te = p->editor(); te) {
                                 te->clearFormatting();
                             }
                             p->update();
                         }),
                         p);
        gtk_box_pack_start(GTK_BOX(row1), clear, false, false, 0);
    }

    // Bold, italic, underline...
    GtkWidget* fmtRow = mini ? row1 : row2;
    if (mini) {
        gtk_box_pack_start(GTK_BOX(row1), separator(true), false, false, 0);
    }
    addToggle(p, fmtRow, "<b>N</b>", _("Bold (Ctrl+B)"), FormatKind::BOLD, "1");
    addToggle(p, fmtRow, "<i>K</i>", _("Italic (Ctrl+I)"), FormatKind::ITALIC, "1");
    addToggle(p, fmtRow, "<u>S</u>", _("Underline (Ctrl+U)"), FormatKind::UNDERLINE, "");
    // The kind of underline
    std::vector<std::string> underlines{std::string("<u>") + _("Underline") + "</u>",
                                        std::string("<span underline=\"double\">") + _("Double underline") + "</span>",
                                        std::string("<span underline=\"error\">") + _("Wavy underline") + "</span>"};
    gtk_box_pack_start(GTK_BOX(fmtRow),
                       listMenuButton(p, gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_MENU),
                                      underlines, _("Underline style"), +[](Panel* p, int i) {
                                          static const char* const V[] = {"single", "double", "wave"};
                                          if (auto* te = p->editor(); te) {
                                              te->applyFormat(FormatKind::UNDERLINE, std::string(V[i]));
                                          }
                                          p->refocus();
                                          p->update();
                                      }),
                       false, false, 0);
    if (!mini) {
        addToggle(p, fmtRow, "<s>ab</s>", _("Strikethrough"), FormatKind::STRIKE, "1");
        addToggle(p, fmtRow, "x<sub>2</sub>", _("Subscript"), FormatKind::SCRIPT, "sub");
        addToggle(p, fmtRow, "x<sup>2</sup>", _("Superscript"), FormatKind::SCRIPT, "sup");
    }
    gtk_box_pack_start(GTK_BOX(fmtRow), separator(true), false, false, 0);

    // Highlighter and font color: the button applies the last color, the arrow chooses another one
    p->highlightLabel = markupLabel("");
    setHighlightFace(p);
    GtkWidget* hl = flatButton(p->highlightLabel, _("Text highlight color"));
    g_signal_connect(hl, "clicked", G_CALLBACK(+[](GtkButton*, Panel* p) {
                         if (auto* te = p->editor(); te) {
                             te->applyFormat(FormatKind::BACKGROUND, p->highlight);
                         }
                         p->refocus();
                     }),
                     p);
    gtk_box_pack_start(GTK_BOX(fmtRow), hl, false, false, 0);
    gtk_box_pack_start(GTK_BOX(fmtRow),
                       colorMenuButton(p, HIGHLIGHTS.data(), HIGHLIGHTS.size(), _("No color"),
                                       _("Text highlight color"), +[](Panel* p, const std::string& c) {
                                           if (!c.empty()) {
                                               p->highlight = c;
                                               setHighlightFace(p);
                                           }
                                           if (auto* te = p->editor(); te) {
                                               te->applyFormat(FormatKind::BACKGROUND,
                                                               c.empty() ? std::nullopt : std::optional(c));
                                           }
                                           p->refocus();
                                       }),
                       false, false, 0);
    p->colorLabel = markupLabel("");
    setColorFace(p);
    GtkWidget* fc = flatButton(p->colorLabel, _("Font color"));
    g_signal_connect(fc, "clicked", G_CALLBACK(+[](GtkButton*, Panel* p) {
                         if (auto* te = p->editor(); te) {
                             te->applyFormat(FormatKind::FOREGROUND, p->fontColor);
                         }
                         p->refocus();
                     }),
                     p);
    gtk_box_pack_start(GTK_BOX(fmtRow), fc, false, false, 0);
    gtk_box_pack_start(GTK_BOX(fmtRow),
                       colorMenuButton(p, FONT_COLORS.data(), FONT_COLORS.size(), _("Automatic"), _("Font color"),
                                       +[](Panel* p, const std::string& c) {
                                           if (!c.empty()) {
                                               p->fontColor = c;
                                               setColorFace(p);
                                           }
                                           if (auto* te = p->editor(); te) {
                                               te->applyFormat(FormatKind::FOREGROUND,
                                                               c.empty() ? std::nullopt : std::optional(c));
                                           }
                                           p->refocus();
                                       }),
                       false, false, 0);

    // Alignment (the actions of the whole text)
    GtkWidget* alignRow = mini ? row2 : row2;
    gtk_box_pack_start(GTK_BOX(alignRow), separator(true), false, false, 0);
    gtk_box_pack_start(GTK_BOX(alignRow),
                       actionButton("xopp-format-justify-left", _("Align text to the left"), Action::TEXT_ALIGNMENT,
                                    xoj::util::makeGVariantSPtr(TextAlignment::LEFT)),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(alignRow),
                       actionButton("xopp-format-justify-center", _("Center text"), Action::TEXT_ALIGNMENT,
                                    xoj::util::makeGVariantSPtr(TextAlignment::CENTER)),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(alignRow),
                       actionButton("xopp-format-justify-right", _("Align text to the right"), Action::TEXT_ALIGNMENT,
                                    xoj::util::makeGVariantSPtr(TextAlignment::RIGHT)),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(alignRow),
                       actionButton("xopp-format-justify-fill", _("Justify text"), Action::TEXT_JUSTIFY, {}), false, false,
                       0);

    GtkWidget* root = gtk_box_new(horizontal ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(root), row1, false, false, 0);
    gtk_box_pack_start(GTK_BOX(root), row2, false, false, 0);
    p->root = root;

    // Make it compact enough for a toolbar
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
                                    "button { min-height: 18px; min-width: 18px; padding: 1px 3px; }"
                                    "entry { min-height: 20px; padding-top: 0; padding-bottom: 0; }"
                                    "combobox button { padding: 0 2px; }",
                                    -1, nullptr);
    std::function<void(GtkWidget*)> applyCss = [&](GtkWidget* w) {
        gtk_style_context_add_provider(gtk_widget_get_style_context(w), GTK_STYLE_PROVIDER(css),
                                       GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        if (GTK_IS_CONTAINER(w)) {
            GList* ch = gtk_container_get_children(GTK_CONTAINER(w));
            for (GList* c = ch; c; c = c->next) {
                applyCss(GTK_WIDGET(c->data));
            }
            g_list_free(ch);
        }
    };
    applyCss(root);
    g_object_unref(css);

    p->listener = control->addTextFormatListener([p]() { p->update(); });
    g_signal_connect(root, "destroy", G_CALLBACK(+[](GtkWidget*, Panel* p) {
                         p->control->removeTextFormatListener(p->listener);
                         delete p;
                     }),
                     p);
    gtk_widget_show_all(root);
    p->update();
    return root;
}

void showTextContextMenu(Control* control, GtkWidget* parent, double x, double y) {
    GtkWidget* pop = gtk_popover_new(parent);
    GdkRectangle r{static_cast<int>(x), static_cast<int>(y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(pop), &r);
    gtk_popover_set_position(GTK_POPOVER(pop), GTK_POS_BOTTOM);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_set_border_width(GTK_CONTAINER(box), 4);
    gtk_box_pack_start(GTK_BOX(box), createFormatPanel(control, true, true), false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), false, false, 2);

    struct Entry {
        const char* label;
        const char* icon;
        void (*run)(TextEditor*);
    };
    const std::array<Entry, 4> entries{{{N_("Cut"), "edit-cut", +[](TextEditor* te) { te->cutToClipboard(); }},
                                        {N_("Copy"), "edit-copy", +[](TextEditor* te) { te->copyToClipboard(); }},
                                        {N_("Paste"), "edit-paste", +[](TextEditor* te) { te->pasteFromClipboard(); }},
                                        {N_("Select All"), "edit-select-all",
                                         +[](TextEditor* te) { te->selectAtCursor(TextEditor::SelectType::ALL); }}}};
    GtkWidget* firstEntry = nullptr;
    for (const auto& e: entries) {
        GtkWidget* b = gtk_button_new();
        if (!firstEntry) {
            firstEntry = b;
        }
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_box_pack_start(GTK_BOX(row), gtk_image_new_from_icon_name(e.icon, GTK_ICON_SIZE_MENU), false, false, 0);
        GtkWidget* l = gtk_label_new(_(e.label));
        gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
        gtk_box_pack_start(GTK_BOX(row), l, true, true, 0);
        gtk_container_add(GTK_CONTAINER(b), row);
        g_object_set_data(G_OBJECT(b), "jm-run", reinterpret_cast<gpointer>(e.run));
        g_object_set_data(G_OBJECT(b), "jm-pop", pop);
        g_signal_connect(b, "clicked", G_CALLBACK(+[](GtkButton* b, Control* control) {
                             auto run = reinterpret_cast<void (*)(TextEditor*)>(g_object_get_data(G_OBJECT(b), "jm-run"));
                             gtk_popover_popdown(GTK_POPOVER(g_object_get_data(G_OBJECT(b), "jm-pop")));
                             if (auto* te = control->getTextEditor(); te) {
                                 run(te);
                             }
                         }),
                         control);
        gtk_box_pack_start(GTK_BOX(box), b, false, false, 0);
    }
    gtk_widget_show_all(box);
    gtk_container_add(GTK_CONTAINER(pop), box);
    g_signal_connect(pop, "closed", G_CALLBACK(+[](GtkPopover* pop, Control* control) {
                         if (auto* win = control->getWindow(); win) {
                             gtk_widget_grab_focus(win->getXournal()->getWidget());
                         }
                         // destroy later: the popover may still be in use by the signal
                         g_idle_add(
                                 +[](gpointer w) -> gboolean {
                                     gtk_widget_destroy(GTK_WIDGET(w));
                                     return G_SOURCE_REMOVE;
                                 },
                                 pop);
                     }),
                     control);
    gtk_popover_popup(GTK_POPOVER(pop));
    gtk_widget_grab_focus(firstEntry);  // not the font entry: a key typed by mistake must not rename the font
}

}  // namespace xoj::text

TextFormatToolItem::TextFormatToolItem(Control* control): AbstractToolItem("TEXT_FORMAT", Category::TOOLS), control(control) {}

std::string TextFormatToolItem::getToolDisplayName() const { return _("Text format (font, bold, colors, alignment)"); }

GtkWidget* TextFormatToolItem::getNewToolIcon() const {
    return gtk_image_new_from_icon_name("xopp-format-text", GTK_ICON_SIZE_SMALL_TOOLBAR);
}

xoj::util::WidgetSPtr TextFormatToolItem::createItem(bool horizontal) {
    GtkWidget* panel = xoj::text::createFormatPanel(control, horizontal, false);
    GtkToolItem* it = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(it), panel);
    return xoj::util::WidgetSPtr(GTK_WIDGET(it), xoj::util::adopt);
}
