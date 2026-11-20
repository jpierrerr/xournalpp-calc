#include "ToolbarEditorDialog.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "control/Control.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "gui/MainWindow.h"
#include "gui/toolbarMenubar/AbstractToolItem.h"
#include "gui/toolbarMenubar/ToolMenuHandler.h"
#include "gui/toolbarMenubar/icon/ColorIcon.h"
#include "gui/toolbarMenubar/model/ColorPalette.h"
#include "gui/toolbarMenubar/model/ToolbarData.h"
#include "gui/toolbarMenubar/model/ToolbarModel.h"
#include "util/NamedColor.h"
#include "util/PathUtil.h"
#include "util/PlaceholderString.h"
#include "util/StringUtils.h"
#include "util/XojMsgBox.h"
#include "util/i18n.h"

#include "config-dev.h"  // for TOOLBAR_CONFIG

namespace xoj::popup {

ToolbarEditorDialog* ToolbarEditorDialog::instance = nullptr;

namespace {
constexpr const char* CUSTOM_SUFFIX = "_custom";
constexpr const char* FALLBACK_ORIGINAL = "Portrait";

struct BarDef {
    const char* id;
    const char* label;
};
// The floating toolbox bars are left out: they are configured in Preferences
const std::array<BarDef, 8> BARS{{{"toolbarTop1", N_("Top, row 1")},
                                  {"toolbarTop2", N_("Top, row 2")},
                                  {"toolbarLeft1", N_("Left, column 1")},
                                  {"toolbarLeft2", N_("Left, column 2")},
                                  {"toolbarRight1", N_("Right, column 1")},
                                  {"toolbarRight2", N_("Right, column 2")},
                                  {"toolbarBottom1", N_("Bottom, row 1")},
                                  {"toolbarBottom2", N_("Bottom, row 2")}}};

constexpr gint GROUP_DRAG_BASE = 100000;

GtkTargetEntry rowTarget{const_cast<gchar*>("JACKMATH_TOOLBAR_ROW"), GTK_TARGET_SAME_APP, 0};

bool isRepeatable(const std::string& item) { return item == "SEPARATOR" || item == "SPACER"; }

constexpr const char* HIDDEN_PREFIX = "jmHidden-";

/// A bar seen as groups of tools between separators: gaps[k] comes before groups[k], gaps.back() after the last group
struct Groups {
    std::vector<std::vector<std::string>> groups;
    std::vector<std::vector<std::string>> gaps;
};

Groups splitGroups(const std::vector<std::string>& items) {
    Groups g;
    g.gaps.emplace_back();
    for (const auto& it: items) {
        if (isRepeatable(it)) {
            g.gaps.back().push_back(it);
        } else {
            if (g.gaps.size() == g.groups.size() + 1 && (g.groups.empty() || !g.gaps.back().empty())) {
                g.groups.emplace_back();
                g.gaps.emplace_back();
            }
            g.groups.back().push_back(it);
        }
    }
    return g;
}

std::vector<std::string> joinGroups(const Groups& g) {
    std::vector<std::string> items;
    for (size_t k = 0; k < g.gaps.size(); k++) {
        items.insert(items.end(), g.gaps[k].begin(), g.gaps[k].end());
        if (k < g.groups.size()) {
            items.insert(items.end(), g.groups[k].begin(), g.groups[k].end());
        }
    }
    return items;
}

/// No separator at the ends of the bar or twice in a row (a spacer is kept)
std::vector<std::string> tidy(const std::vector<std::string>& items) {
    std::vector<std::string> out;
    for (const auto& it: items) {
        if (it == "SEPARATOR" && (out.empty() || isRepeatable(out.back()))) {
            continue;
        }
        out.push_back(it);
    }
    while (!out.empty() && out.back() == "SEPARATOR") {
        out.pop_back();
    }
    return out;
}

std::optional<size_t> colorIndex(const std::string& item) {
    if (!StringUtils::startsWith(item, "COLOR(") || !StringUtils::endsWith(item, ")")) {
        return std::nullopt;
    }
    try {
        return static_cast<size_t>(std::stoul(item.substr(6, item.size() - 7)));
    } catch (...) { return std::nullopt; }
}

GtkWidget* headingLabel(const std::string& text, bool big) {
    GtkWidget* l = gtk_label_new(nullptr);
    gchar* esc = g_markup_escape_text(text.c_str(), -1);
    std::string markup = big ? std::string("<b>") + esc + "</b>" : std::string("<small><b>") + esc + "</b></small>";
    g_free(esc);
    gtk_label_set_markup(GTK_LABEL(l), markup.c_str());
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(l), true);
    gtk_widget_set_margin_top(l, big ? 8 : 6);
    gtk_widget_set_margin_bottom(l, 2);
    gtk_widget_set_margin_start(l, 4);
    return l;
}
}  // namespace

struct ToolbarEditorDialog::Row {
    ToolbarEditorDialog* dlg;
    bool shown;
    int index;  ///< position in the bar for a shown tool; -2 for the heading above the shown tools
    std::string item;
    int group = -1;           ///< group of a shown tool or of a group heading
    bool groupHeader = false;  ///< the row of a whole group
    std::string hiddenEntry;   ///< for a group taken out of the bars: where it is kept
};

void ToolbarEditorDialog::show(Control* control) {
    if (!instance) {
        instance = new ToolbarEditorDialog(control);
    }
    gtk_window_present(GTK_WINDOW(instance->window));
}

ToolbarEditorDialog::ToolbarEditorDialog(Control* control): control(control) {
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), _("Customize toolbar"));
    gtk_window_set_transient_for(GTK_WINDOW(window), control->getGtkWindow());
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window), true);
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 640);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DIALOG);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    layoutLabel = gtk_label_new(nullptr);
    gtk_label_set_xalign(GTK_LABEL(layoutLabel), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(layoutLabel), true);
    gtk_box_pack_start(GTK_BOX(vbox), layoutLabel, false, false, 0);

    GtkWidget* barBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(barBox), gtk_label_new(_("Bar:")), false, false, 0);
    barCombo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(barBox), barCombo, true, true, 0);
    gtk_box_pack_start(GTK_BOX(vbox), barBox, false, false, 0);

    scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_container_add(GTK_CONTAINER(scroll), list);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, true, true, 0);

    gtk_drag_dest_set(list, GTK_DEST_DEFAULT_ALL, &rowTarget, 1, GDK_ACTION_MOVE);
    g_signal_connect(list, "drag-data-received", G_CALLBACK(onDragDataReceived), this);
    g_signal_connect(list, "row-selected", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* r, ToolbarEditorDialog* self) {
                         auto* row = r ? static_cast<Row*>(g_object_get_data(G_OBJECT(r), "jm-row")) : nullptr;
                         self->selected = row && row->shown && !row->groupHeader ? row->index : -1;
                         self->selectedGroup = row && row->shown && row->groupHeader ? row->group : -1;
                     }),
                     this);

    // Up / Down / Move to another bar
    GtkWidget* moveBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* up = gtk_button_new_with_label(_("Up"));
    gtk_button_set_image(GTK_BUTTON(up), gtk_image_new_from_icon_name("go-up", GTK_ICON_SIZE_BUTTON));
    GtkWidget* down = gtk_button_new_with_label(_("Down"));
    gtk_button_set_image(GTK_BUTTON(down), gtk_image_new_from_icon_name("go-down", GTK_ICON_SIZE_BUTTON));
    gtk_box_pack_start(GTK_BOX(moveBox), up, false, false, 0);
    gtk_box_pack_start(GTK_BOX(moveBox), down, false, false, 0);
    GtkWidget* moveBtn = gtk_button_new_with_label(_("Move to:"));
    moveCombo = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(moveBox), moveBtn, false, false, 0);
    gtk_box_pack_start(GTK_BOX(moveBox), moveCombo, true, true, 0);
    gtk_box_pack_start(GTK_BOX(vbox), moveBox, false, false, 0);
    g_signal_connect(up, "clicked", G_CALLBACK(+[](GtkButton*, ToolbarEditorDialog* s) { s->moveSelected(-1); }),
                     this);
    g_signal_connect(down, "clicked", G_CALLBACK(+[](GtkButton*, ToolbarEditorDialog* s) { s->moveSelected(1); }),
                     this);
    g_signal_connect(moveBtn, "clicked",
                     G_CALLBACK(+[](GtkButton*, ToolbarEditorDialog* s) { s->moveSelectedToBar(); }), this);

    GtkWidget* bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* restore = gtk_button_new_with_label(_("Restore original"));
    GtkWidget* dragEditor = gtk_button_new_with_label(_("Drag onto the bars…"));
    gtk_widget_set_tooltip_text(dragEditor, _("The previous editor: drag tools from a window onto the bars"));
    GtkWidget* close = gtk_button_new_with_label(_("Close"));
    gtk_box_pack_start(GTK_BOX(bottom), restore, false, false, 0);
    gtk_box_pack_start(GTK_BOX(bottom), dragEditor, false, false, 0);
    gtk_box_pack_end(GTK_BOX(bottom), close, false, false, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bottom, false, false, 0);
    g_signal_connect(restore, "clicked",
                     G_CALLBACK(+[](GtkButton*, ToolbarEditorDialog* s) { s->restoreOriginal(); }), this);
    g_signal_connect(dragEditor, "clicked",
                     G_CALLBACK(+[](GtkButton*, ToolbarEditorDialog* s) { s->openDragEditor(); }), this);
    g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_widget_destroy), window);

    g_signal_connect(window, "key-press-event", G_CALLBACK(+[](GtkWidget* w, GdkEventKey* e, gpointer) -> gboolean {
                         if (e->keyval == GDK_KEY_Escape) {
                             gtk_widget_destroy(w);
                             return true;
                         }
                         return false;
                     }),
                     nullptr);
    g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, ToolbarEditorDialog* self) {
                         self->window = nullptr;
                         delete self;
                     }),
                     this);

    fillBarCombo();
    g_signal_connect(barCombo, "changed", G_CALLBACK(+[](GtkComboBox* c, ToolbarEditorDialog* self) {
                         if (const gchar* id = gtk_combo_box_get_active_id(c); id) {
                             self->currentBar = id;
                             self->selected = -1;
                             self->selectedGroup = -1;
                             self->rebuildList();
                         }
                     }),
                     this);
    rebuildList();
    gtk_widget_show_all(window);
}

ToolbarEditorDialog::~ToolbarEditorDialog() {
    if (refreshSource) {
        g_source_remove(refreshSource);
    }
    rows.clear();
    if (instance == this) {
        instance = nullptr;
    }
}

void ToolbarEditorDialog::fillBarCombo() {
    for (const auto& b: BARS) {
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(barCombo), b.id, _(b.label));
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(moveCombo), b.id, _(b.label));
    }
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(barCombo), currentBar.c_str());
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(moveCombo), "toolbarLeft1");
}

auto ToolbarEditorDialog::barLabel(const std::string& bar) const -> std::string {
    for (const auto& b: BARS) {
        if (bar == b.id) {
            return _(b.label);
        }
    }
    return bar;
}

auto ToolbarEditorDialog::itemsOf(const std::string& bar) const -> std::vector<std::string> {
    const ToolbarData* d = control->getWindow()->getSelectedToolbar();
    return d ? d->getItemNames(bar) : std::vector<std::string>{};
}

auto ToolbarEditorDialog::barOf(const std::string& item) const -> std::string {
    for (const auto& b: BARS) {
        auto items = itemsOf(b.id);
        if (std::find(items.begin(), items.end(), item) != items.end()) {
            return b.id;
        }
    }
    return {};
}

auto ToolbarEditorDialog::displayName(const std::string& item) const -> std::string {
    if (auto ci = colorIndex(item); ci) {
        const auto& palette = control->getPalette();
        if (*ci < palette.size()) {
            return FS(_F("Color: {1}") % palette.getColorAt(*ci).getName());
        }
        return item;
    }
    for (const auto& t: control->getWindow()->getToolMenuHandler()->getToolItems()) {
        if (t->getId() == item) {
            return t->getToolDisplayName();
        }
    }
    return item;
}

auto ToolbarEditorDialog::newIcon(const std::string& item) const -> GtkWidget* {
    if (auto ci = colorIndex(item); ci) {
        const auto& palette = control->getPalette();
        if (*ci < palette.size()) {
            return ColorIcon::newGtkImage(palette.getColorAt(*ci).getColor(), 16, true);
        }
    } else {
        for (const auto& t: control->getWindow()->getToolMenuHandler()->getToolItems()) {
            if (t->getId() == item) {
                if (GtkWidget* icon = t->getNewToolIcon(); icon) {
                    return icon;
                }
                break;
            }
        }
    }
    return gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_SMALL_TOOLBAR);
}

auto ToolbarEditorDialog::editableData() -> ToolbarData* {
    MainWindow* win = control->getWindow();
    ToolbarData* d = win->getSelectedToolbar();
    if (!d || !d->isPredefined()) {
        return d;
    }
    // A predefined layout cannot be changed: make (or reuse) a personal copy of it, without asking
    ToolbarModel* model = win->getToolbarModel();
    const std::string id = d->getId() + CUSTOM_SUFFIX;
    for (const auto& t: model->getToolbars()) {
        if (t->getId() == id && !t->isPredefined()) {
            t->copyContentsFrom(*d);
            ToolbarData* copy = t.get();
            win->toolbarSelected(copy);
            return copy;
        }
    }
    auto copy = std::make_unique<ToolbarData>(*d);
    copy->setId(id);
    copy->setName(FS(_F("{1} (custom)") % std::string(_(d->getName().c_str()))));
    ToolbarData* added = model->add(std::move(copy));
    win->toolbarSelected(added);
    win->updateToolbarMenu();
    return added;
}

void ToolbarEditorDialog::setItems(const std::string& bar, const std::vector<std::string>& items) {
    if (ToolbarData* d = editableData(); d) {
        d->setItemNames(bar, items);
    }
}

void ToolbarEditorDialog::applyAndSave() {
    MainWindow* win = control->getWindow();
    win->reloadToolbars();
    // The rebuilt page box starts empty: show the page again
    if (Document* doc = control->getDocument(); doc) {
        const size_t page = control->getCurrentPageNo();
        doc->lock_shared();
        const size_t count = doc->getPageCount();
        const size_t pdfPage = page < count ? doc->getPage(page)->getPdfPageNr() : npos;
        doc->unlock_shared();
        win->updatePageNumbers(page, count, pdfPage);
    }
    win->getToolbarModel()->save(Util::getConfigFile(TOOLBAR_CONFIG));
}

void ToolbarEditorDialog::scheduleRefresh() {
    // The list is rebuilt from an idle callback: the change usually comes from a widget of the list itself
    if (refreshSource) {
        return;
    }
    refreshSource = g_idle_add(
            +[](gpointer data) -> gboolean {
                auto* self = static_cast<ToolbarEditorDialog*>(data);
                self->refreshSource = 0;
                self->applyAndSave();
                self->rebuildList();
                return G_SOURCE_REMOVE;
            },
            this);
}

void ToolbarEditorDialog::rebuildList() {
    if (!window) {
        return;
    }
    // Keep the list where it was scrolled to
    GtkAdjustment* adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
    const double scrollPos = gtk_adjustment_get_value(adj);

    // Removing the selected row resets the selection: keep it
    const int keepSelected = selected;
    const int keepGroup = selectedGroup;
    GList* children = gtk_container_get_children(GTK_CONTAINER(list));
    for (GList* c = children; c; c = c->next) {
        gtk_widget_destroy(GTK_WIDGET(c->data));
    }
    g_list_free(children);
    rows.clear();
    selected = keepSelected;
    selectedGroup = keepGroup;

    const ToolbarData* d = control->getWindow()->getSelectedToolbar();
    std::string layout = d ? d->getName() : std::string();
    gtk_label_set_text(GTK_LABEL(layoutLabel),
                       FC(_F("Layout: {1}. Changes are applied at once and kept for the next time.") % layout));

    auto addRow = [&](GtkWidget* content, std::unique_ptr<Row> data, bool selectable) -> GtkWidget* {
        GtkWidget* row = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(row), content);
        gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), selectable);
        gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), selectable);
        if (data) {
            g_object_set_data(G_OBJECT(row), "jm-row", data.get());
            rows.emplace_back(std::move(data));
        }
        gtk_container_add(GTK_CONTAINER(list), row);
        return row;
    };

    auto groupTitle = [&](const std::vector<std::string>& g) {
        std::string t;
        for (size_t i = 0; i < g.size() && i < 3; i++) {
            t += (i ? ", " : "") + displayName(g[i]);
        }
        if (g.size() > 3) {
            t += ", …";
        }
        return FS(_F("Group: {1}") % t);
    };

    auto makeLine = [&](Row* r, const std::string& label, GtkWidget* icon, const std::string& note, int indent,
                        bool bold) -> GtkWidget* {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_widget_set_margin_start(box, 4 + indent);
        gtk_widget_set_margin_end(box, 4);
        gtk_widget_set_margin_top(box, bold ? 6 : 2);
        gtk_widget_set_margin_bottom(box, 2);
        GtkWidget* handle = gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_MENU);
        gtk_widget_set_opacity(handle, r->shown ? 0.6 : 0.0);
        gtk_box_pack_start(GTK_BOX(box), handle, false, false, 0);
        GtkWidget* check = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), r->shown);
        if (r->groupHeader) {
            gtk_widget_set_tooltip_text(check, r->shown ? _("Untick to take the whole group out of the bar")
                                                        : _("Tick to put the whole group back at the end of this bar"));
        } else {
            gtk_widget_set_tooltip_text(check, r->shown ? _("Untick to hide it") : _("Tick to show it in this bar"));
        }
        g_signal_connect(check, "toggled", G_CALLBACK(+[](GtkToggleButton* b, Row* row) {
                             row->dlg->toggleRow(*row, gtk_toggle_button_get_active(b));
                         }),
                         r);
        gtk_box_pack_start(GTK_BOX(box), check, false, false, 0);
        if (icon) {
            gtk_box_pack_start(GTK_BOX(box), icon, false, false, 0);
        }
        GtkWidget* name = gtk_label_new(nullptr);
        gchar* esc = g_markup_escape_text(label.c_str(), -1);
        gtk_label_set_markup(GTK_LABEL(name), bold ? (std::string("<b>") + esc + "</b>").c_str() : esc);
        g_free(esc);
        gtk_label_set_xalign(GTK_LABEL(name), 0.0f);
        gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(box), name, true, true, 0);
        if (!note.empty()) {
            GtkWidget* n = gtk_label_new(note.c_str());
            gtk_style_context_add_class(gtk_widget_get_style_context(n), "dim-label");
            gtk_box_pack_end(GTK_BOX(box), n, false, false, 0);
        }
        if (!r->shown) {
            return box;
        }
        // A shown tool or group can be dragged to another place of the list
        GtkWidget* ebox = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(ebox), false);  // let the row show it is selected
        gtk_container_add(GTK_CONTAINER(ebox), box);
        gtk_drag_source_set(ebox, GDK_BUTTON1_MASK, &rowTarget, 1, GDK_ACTION_MOVE);
        gtk_drag_source_set_icon_name(ebox, "open-menu-symbolic");
        g_signal_connect(ebox, "drag-data-get",
                         G_CALLBACK(+[](GtkWidget*, GdkDragContext*, GtkSelectionData* sd, guint, guint, Row* row) {
                             gint code = row->groupHeader ? GROUP_DRAG_BASE + row->group : row->index;
                             gtk_selection_data_set(sd, gtk_selection_data_get_target(sd), 8,
                                                    reinterpret_cast<const guchar*>(&code), sizeof(code));
                         }),
                         r);
        return ebox;
    };

    const auto items = itemsOf(currentBar);

    auto top = std::make_unique<Row>(Row{this, false, -2, {}});
    addRow(headingLabel(_("Shown in this bar: each group (between separators) can be taken out, moved or dragged "
                          "as a whole, or tool by tool"),
                        true),
           std::move(top), false);

    // Shown tools, by groups
    std::vector<GtkWidget*> shownRows(items.size(), nullptr);
    std::vector<GtkWidget*> groupRows;
    {
        int group = -1;
        bool inGroup = false;
        const Groups g = splitGroups(items);
        for (size_t i = 0; i < items.size(); i++) {
            const bool sep = isRepeatable(items[i]);
            if (!sep && !inGroup) {
                group++;
                auto h = std::make_unique<Row>(Row{this, true, -1, {}, group, true});
                GtkWidget* line = makeLine(h.get(), groupTitle(g.groups[static_cast<size_t>(group)]),
                                           gtk_image_new_from_icon_name("view-list-symbolic", GTK_ICON_SIZE_MENU),
                                           {}, 0, true);
                groupRows.push_back(addRow(line, std::move(h), true));
            }
            inGroup = !sep;
            auto r = std::make_unique<Row>(Row{this, true, static_cast<int>(i), items[i], sep ? -1 : group});
            GtkWidget* line = makeLine(r.get(), displayName(items[i]), newIcon(items[i]), {}, sep ? 0 : 24, false);
            shownRows[i] = addRow(line, std::move(r), true);
        }
    }
    if (items.empty()) {
        GtkWidget* l = gtk_label_new(_("(empty: this bar is not shown)"));
        gtk_style_context_add_class(gtk_widget_get_style_context(l), "dim-label");
        addRow(l, nullptr, false);
    }

    // Groups taken out earlier
    std::vector<std::pair<std::string, std::vector<std::string>>> hidden;
    if (d) {
        for (const auto& e: d->getEntryNames()) {
            if (StringUtils::startsWith(e, HIDDEN_PREFIX)) {
                if (auto g = d->getItemNames(e); !g.empty()) {
                    hidden.emplace_back(e, std::move(g));
                }
            }
        }
    }
    if (!hidden.empty()) {
        addRow(headingLabel(_("Groups taken out (tick one to put it back at the end of this bar)"), true), nullptr,
               false);
        for (auto& [entry, g]: hidden) {
            auto r = std::make_unique<Row>(Row{this, false, -1, {}, -1, true, entry});
            GtkWidget* line = makeLine(r.get(), groupTitle(g),
                                       gtk_image_new_from_icon_name("view-list-symbolic", GTK_ICON_SIZE_MENU), {}, 0,
                                       false);
            addRow(line, std::move(r), false);
        }
    }

    addRow(headingLabel(_("Not in this bar (tick one to add it at the end; if it is in another bar, it moves here)"),
                        true),
           nullptr, false);

    using Cat = AbstractToolItem::Category;
    const std::array<std::pair<Cat, const char*>, 9> order{{{Cat::TOOLS, N_("Tools")},
                                                            {Cat::SELECTION, N_("Selection")},
                                                            {Cat::COLORS, N_("Colors")},
                                                            {Cat::NAVIGATION, N_("Navigation")},
                                                            {Cat::FILES, N_("Files")},
                                                            {Cat::MISC, N_("Miscellaneous")},
                                                            {Cat::AUDIO, N_("Audio")},
                                                            {Cat::SEPARATORS, N_("Separators")},
                                                            {Cat::PLUGINS, N_("Plugins")}}};
    const auto& tools = control->getWindow()->getToolMenuHandler()->getToolItems();
    const auto& palette = control->getPalette();
    auto inThisBar = [&](const std::string& id) { return std::find(items.begin(), items.end(), id) != items.end(); };

    for (const auto& [cat, catLabel]: order) {
        std::vector<std::string> pool;
        if (cat == Cat::COLORS) {
            for (size_t i = 0; i < palette.size(); i++) {
                std::string id = "COLOR(" + std::to_string(i) + ")";
                if (!inThisBar(id)) {
                    pool.push_back(id);
                }
            }
        }
        for (const auto& t: tools) {
            if (t->getCategory() == cat && (isRepeatable(t->getId()) || !inThisBar(t->getId()))) {
                pool.push_back(t->getId());
            }
        }
        if (pool.empty()) {
            continue;
        }
        if (cat != Cat::COLORS) {
            std::sort(pool.begin(), pool.end(), [&](const auto& a, const auto& b) {
                return g_utf8_collate(displayName(a).c_str(), displayName(b).c_str()) < 0;
            });
        }
        addRow(headingLabel(_(catLabel), false), nullptr, false);
        for (const auto& id: pool) {
            std::string note;
            if (!isRepeatable(id)) {
                if (auto other = barOf(id); !other.empty()) {
                    note = FS(_F("now in {1}") % barLabel(other));
                }
            }
            auto r = std::make_unique<Row>(Row{this, false, -1, id});
            GtkWidget* line = makeLine(r.get(), displayName(id), newIcon(id), note, 0, false);
            addRow(line, std::move(r), false);
        }
    }

    gtk_widget_show_all(list);
    gtk_adjustment_set_upper(adj, std::max(gtk_adjustment_get_upper(adj), scrollPos + gtk_adjustment_get_page_size(adj)));
    gtk_adjustment_set_value(adj, scrollPos);
    // Selecting would scroll to the row: do it without moving the list
    if (selected >= 0 && selected < static_cast<int>(shownRows.size())) {
        gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(shownRows[static_cast<size_t>(selected)]));
    } else if (selectedGroup >= 0 && selectedGroup < static_cast<int>(groupRows.size())) {
        gtk_list_box_select_row(GTK_LIST_BOX(list), GTK_LIST_BOX_ROW(groupRows[static_cast<size_t>(selectedGroup)]));
    } else {
        selected = -1;
        selectedGroup = -1;
    }
    gtk_adjustment_set_value(adj, scrollPos);
}

void ToolbarEditorDialog::takeOutOfOtherBars(const std::string& item) {
    if (isRepeatable(item)) {
        return;
    }
    // A tool is in one bar only
    if (auto other = barOf(item); !other.empty() && other != currentBar) {
        auto otherItems = itemsOf(other);
        otherItems.erase(std::find(otherItems.begin(), otherItems.end(), item));
        setItems(other, tidy(otherItems));
    }
    // ... and not kept in a group taken out
    if (const ToolbarData* d = control->getWindow()->getSelectedToolbar(); d) {
        for (const auto& e: d->getEntryNames()) {
            if (StringUtils::startsWith(e, HIDDEN_PREFIX)) {
                auto g = d->getItemNames(e);
                if (auto it = std::find(g.begin(), g.end(), item); it != g.end()) {
                    g.erase(it);
                    setItems(e, g);
                }
            }
        }
    }
}

void ToolbarEditorDialog::toggleRow(const Row& row, bool active) {
    auto items = itemsOf(currentBar);
    if (row.groupHeader && row.shown && !active) {
        // Take the whole group out; it is kept to be put back later
        Groups g = splitGroups(items);
        if (row.group >= 0 && row.group < static_cast<int>(g.groups.size())) {
            auto taken = g.groups[static_cast<size_t>(row.group)];
            g.groups.erase(g.groups.begin() + row.group);
            // merge the separators around it
            auto& before = g.gaps[static_cast<size_t>(row.group)];
            auto& after = g.gaps[static_cast<size_t>(row.group) + 1];
            before.insert(before.end(), after.begin(), after.end());
            g.gaps.erase(g.gaps.begin() + row.group + 1);
            setItems(currentBar, tidy(joinGroups(g)));
            int n = 1;
            const ToolbarData* d = control->getWindow()->getSelectedToolbar();
            auto names = d ? d->getEntryNames() : std::vector<std::string>{};
            std::string key;
            do {
                key = std::string(HIDDEN_PREFIX) + currentBar + "-" + std::to_string(n++);
            } while (std::find(names.begin(), names.end(), key) != names.end() && d && !d->getItemNames(key).empty());
            setItems(key, taken);
        }
        selected = -1;
        selectedGroup = -1;
    } else if (row.groupHeader && !row.shown && active) {
        // Put a group taken out back, at the end of this bar
        auto g = itemsOf(row.hiddenEntry);
        setItems(row.hiddenEntry, {});
        for (const auto& it: g) {
            takeOutOfOtherBars(it);
        }
        items = itemsOf(currentBar);
        if (!items.empty()) {
            items.push_back("SEPARATOR");
        }
        items.insert(items.end(), g.begin(), g.end());
        setItems(currentBar, tidy(items));
        selected = -1;
        selectedGroup = static_cast<int>(splitGroups(itemsOf(currentBar)).groups.size()) - 1;
    } else if (row.shown && !active) {
        if (row.index >= 0 && row.index < static_cast<int>(items.size())) {
            items.erase(items.begin() + row.index);
            setItems(currentBar, items);
        }
        selected = -1;
        selectedGroup = -1;
    } else if (!row.shown && active) {
        takeOutOfOtherBars(row.item);
        items = itemsOf(currentBar);
        items.push_back(row.item);
        setItems(currentBar, items);
        selected = static_cast<int>(items.size()) - 1;
        selectedGroup = -1;
    }
    scheduleRefresh();
}

void ToolbarEditorDialog::moveVisible(int from, int to) {
    auto items = itemsOf(currentBar);
    const int n = static_cast<int>(items.size());
    if (from < 0 || from >= n) {
        return;
    }
    to = std::clamp(to, 0, n - 1);
    if (from == to) {
        return;
    }
    std::string item = items[static_cast<size_t>(from)];
    items.erase(items.begin() + from);
    items.insert(items.begin() + to, item);
    setItems(currentBar, items);
    selected = to;
    selectedGroup = -1;
    scheduleRefresh();
}

void ToolbarEditorDialog::moveGroup(int from, int to) {
    Groups g = splitGroups(itemsOf(currentBar));
    const int n = static_cast<int>(g.groups.size());
    if (from < 0 || from >= n) {
        return;
    }
    to = std::clamp(to, 0, n - 1);
    if (from == to) {
        return;
    }
    auto moved = g.groups[static_cast<size_t>(from)];
    g.groups.erase(g.groups.begin() + from);
    g.groups.insert(g.groups.begin() + to, moved);
    setItems(currentBar, joinGroups(g));
    selected = -1;
    selectedGroup = to;
    scheduleRefresh();
}

void ToolbarEditorDialog::moveSelected(int delta) {
    if (selectedGroup >= 0) {
        moveGroup(selectedGroup, selectedGroup + delta);
    } else if (selected >= 0) {
        moveVisible(selected, selected + delta);
    }
}

void ToolbarEditorDialog::moveSelectedToBar() {
    const gchar* target = gtk_combo_box_get_active_id(GTK_COMBO_BOX(moveCombo));
    auto items = itemsOf(currentBar);
    if (!target || currentBar == target) {
        return;
    }
    std::vector<std::string> moving;
    if (selectedGroup >= 0) {
        Groups g = splitGroups(items);
        if (selectedGroup >= static_cast<int>(g.groups.size())) {
            return;
        }
        moving = g.groups[static_cast<size_t>(selectedGroup)];
        g.groups.erase(g.groups.begin() + selectedGroup);
        auto& before = g.gaps[static_cast<size_t>(selectedGroup)];
        auto& after = g.gaps[static_cast<size_t>(selectedGroup) + 1];
        before.insert(before.end(), after.begin(), after.end());
        g.gaps.erase(g.gaps.begin() + selectedGroup + 1);
        items = tidy(joinGroups(g));
    } else if (selected >= 0 && selected < static_cast<int>(items.size())) {
        moving.push_back(items[static_cast<size_t>(selected)]);
        items.erase(items.begin() + selected);
    } else {
        return;
    }
    auto targetItems = itemsOf(target);
    if (moving.size() > 1 && !targetItems.empty()) {
        targetItems.push_back("SEPARATOR");
    }
    targetItems.insert(targetItems.end(), moving.begin(), moving.end());
    setItems(currentBar, items);
    setItems(target, targetItems);
    selected = -1;
    selectedGroup = -1;
    scheduleRefresh();
}

void ToolbarEditorDialog::onDragDataReceived(GtkWidget*, GdkDragContext*, int, int y, GtkSelectionData* data, guint,
                                             guint, ToolbarEditorDialog* self) {
    if (gtk_selection_data_get_length(data) != sizeof(gint)) {
        return;
    }
    gint code = *reinterpret_cast<const gint*>(gtk_selection_data_get_data(data));
    Row* row = nullptr;
    if (GtkListBoxRow* r = gtk_list_box_get_row_at_y(GTK_LIST_BOX(self->list), y); r) {
        row = static_cast<Row*>(g_object_get_data(G_OBJECT(r), "jm-row"));
    }
    if (code >= GROUP_DRAG_BASE) {
        const int n = static_cast<int>(splitGroups(self->itemsOf(self->currentBar)).groups.size());
        int to = n - 1;
        if (row && row->shown && row->group >= 0) {
            to = row->group;
        } else if (row && row->index == -2) {
            to = 0;
        }
        self->moveGroup(code - GROUP_DRAG_BASE, to);
        return;
    }
    const int n = static_cast<int>(self->itemsOf(self->currentBar).size());
    int to = n - 1;
    if (row) {
        if (row->shown && !row->groupHeader) {
            to = row->index;
        } else if (row->shown && row->groupHeader) {
            // dropped on a group title: first place of that group
            Groups g = splitGroups(self->itemsOf(self->currentBar));
            int pos = 0;
            for (int k = 0; k <= row->group && k < static_cast<int>(g.groups.size()); k++) {
                pos += static_cast<int>(g.gaps[static_cast<size_t>(k)].size());
                if (k < row->group) {
                    pos += static_cast<int>(g.groups[static_cast<size_t>(k)].size());
                }
            }
            to = pos > code ? pos - 1 : pos;
        } else if (row->index == -2) {
            to = 0;
        }
    }
    self->moveVisible(code, to);
}

void ToolbarEditorDialog::restoreOriginal() {
    MainWindow* win = control->getWindow();
    ToolbarData* d = win->getSelectedToolbar();
    if (!d) {
        return;
    }
    if (d->isPredefined()) {
        XojMsgBox::showMessageToUser(GTK_WINDOW(window), _("This toolbar is already the original one."),
                                     GTK_MESSAGE_INFO);
        return;
    }
    std::string baseId = d->getId();
    if (StringUtils::endsWith(baseId, CUSTOM_SUFFIX)) {
        baseId.resize(baseId.size() - std::string(CUSTOM_SUFFIX).size());
    } else {
        baseId = FALLBACK_ORIGINAL;
    }
    ToolbarData* base = nullptr;
    for (const auto& t: win->getToolbarModel()->getToolbars()) {
        if (t->isPredefined() && (t->getId() == baseId || !base)) {
            base = t.get();
            if (t->getId() == baseId) {
                break;
            }
        }
    }
    if (!base) {
        return;
    }
    enum { RESTORE = 1, CANCEL };
    XojMsgBox::askQuestion(GTK_WINDOW(window), FS(_F("Go back to the original toolbar \"{1}\"?") % base->getName()),
                           _("The changes made to this toolbar are discarded."),
                           {{_("Restore"), RESTORE}, {_("Cancel"), CANCEL}}, [this, d, base](int response) {
                               if (response != RESTORE || instance != this) {
                                   return;
                               }
                               MainWindow* win = control->getWindow();
                               win->toolbarSelected(base);
                               if (!StringUtils::endsWith(d->getId(), CUSTOM_SUFFIX)) {
                                   // A layout made by hand earlier: keep it, the user may still want it
                                   win->getToolbarModel()->save(Util::getConfigFile(TOOLBAR_CONFIG));
                               } else {
                                   win->getToolbarModel()->remove(d);
                                   win->updateToolbarMenu();
                                   win->getToolbarModel()->save(Util::getConfigFile(TOOLBAR_CONFIG));
                               }
                               selected = -1;
                               selectedGroup = -1;
                               rebuildList();
                           });
}

void ToolbarEditorDialog::openDragEditor() {
    Control* ctrl = control;
    editableData();
    applyAndSave();
    gtk_widget_destroy(window);  // deletes this
    ctrl->customizeToolbarsByDrag();
}

}  // namespace xoj::popup
