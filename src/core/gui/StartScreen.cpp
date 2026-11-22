#include "StartScreen.h"

#include <algorithm>  // for sort, find
#include <cstring>    // for strstr
#include <fstream>    // for ifstream, ofstream
#include <iterator>   // for size
#include <string>     // for string
#include <vector>     // for vector

#include <gdk/gdkkeysyms.h>  // for GDK_KEY_Escape
#include <glib/gstdio.h>     // for g_stat
#include <gtk/gtk.h>

#include "control/Control.h"                         // for Control
#include "control/PageBackgroundChangeController.h"  // for PageBackgroundChangeController
#include "control/RecentManager.h"                   // for getRecentFiles
#include "control/actions/ActionDatabase.h"          // for ActionDatabase
#include "control/pagetype/PageTypeHandler.h"        // for PageTypeHandler, PageTypeInfo
#include "control/settings/Settings.h"               // for Settings
#include "gui/CreatePreviewImage.h"                  // for createPreviewImage
#include "gui/GladeSearchpath.h"                     // for GladeSearchpath
#include "gui/MainWindow.h"                          // for MainWindow
#include "model/PageType.h"                          // for PageType
#include "undo/UndoRedoHandler.h"                    // for UndoRedoHandler
#include "util/PathUtil.h"                           // for fromUri, toUri, getConfigFile
#include "util/XojMsgBox.h"                          // for XojMsgBox
#include "util/i18n.h"                               // for _

#include "filesystem.h"  // for path

namespace xoj::start {
namespace {

constexpr auto PINNED_FILE = "pinned-files.txt";
constexpr auto GUIDE_FILE = "Guia-Xournal-JackMath.pdf";

struct RecentEntry {
    fs::path path;
    time_t modified{};
    bool pinned{};
};

struct State {
    Control* control = nullptr;
    GtkWidget* screen = nullptr;
    GtkWidget* recentList = nullptr;
    GtkWidget* search = nullptr;
    GtkWidget* more = nullptr;
    GtkWidget* moreLink = nullptr;
    std::vector<fs::path> pinned;
    std::vector<RecentEntry> recents;
};

State* st = nullptr;

/// UTF-8 text of a path (u8string() gives char8_t in C++20)
auto u8(const fs::path& p) -> std::string {
    auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

auto fromU8(const std::string& s) -> fs::path { return fs::path(reinterpret_cast<const char8_t*>(s.c_str())); }

auto loadPinned() -> std::vector<fs::path> {
    std::vector<fs::path> res;
    std::ifstream in(Util::getConfigFile(PINNED_FILE));
    for (std::string line; std::getline(in, line);) {
        if (!line.empty()) {
            res.emplace_back(fromU8(line));
        }
    }
    return res;
}

void savePinned(const std::vector<fs::path>& pinned) {
    std::ofstream out(Util::getConfigFile(PINNED_FILE));
    for (const auto& p: pinned) {
        out << u8(p) << "\n";
    }
}

auto icon(const char* name, int size = 20) -> GtkWidget* {
    GtkWidget* img = gtk_image_new_from_icon_name(name, GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(img), size);
    return img;
}

auto label(const std::string& text, const char* cssClass = nullptr, float xalign = 0.0f) -> GtkWidget* {
    GtkWidget* l = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(l), xalign);
    if (cssClass) {
        gtk_style_context_add_class(gtk_widget_get_style_context(l), cssClass);
    }
    return l;
}

void addClass(GtkWidget* w, const char* cssClass) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cssClass);
}

auto navButton(const char* iconName, const std::string& text, GCallback cb) -> GtkWidget* {
    GtkWidget* b = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    addClass(b, "jm-nav-item");
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(box), icon(iconName), false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), label(text), false, false, 0);
    gtk_container_add(GTK_CONTAINER(b), box);
    g_signal_connect(b, "clicked", cb, nullptr);
    return b;
}

/// "Documentos › Concreto II": the last two folders of the file's location
auto folderText(const fs::path& p) -> std::string {
    auto parent = p.parent_path();
    std::string last = u8(parent.filename());
    std::string before = u8(parent.parent_path().filename());
    if (before.empty() || before == u8(parent.parent_path().root_path())) {
        return last.empty() ? u8(parent) : last;
    }
    return before + " › " + last;
}

auto dateText(time_t t) -> std::string {
    GDateTime* now = g_date_time_new_now_local();
    GDateTime* dt = g_date_time_new_from_unix_local(static_cast<gint64>(t));
    std::string res;
    if (dt) {
        int dayNow = g_date_time_get_day_of_year(now) + 400 * g_date_time_get_year(now);
        int day = g_date_time_get_day_of_year(dt) + 400 * g_date_time_get_year(dt);
        gchar* s = nullptr;
        if (day == dayNow) {
            s = g_date_time_format(dt, "%H:%M");
            res = std::string(_("Today")) + ", " + s;
        } else if (day == dayNow - 1) {
            res = _("Yesterday");
        } else {
            s = g_date_time_format(dt, "%d/%m/%Y");
            res = s;
        }
        g_free(s);
        g_date_time_unref(dt);
    }
    g_date_time_unref(now);
    return res;
}

auto greeting() -> std::string {
    GDateTime* now = g_date_time_new_now_local();
    int h = g_date_time_get_hour(now);
    g_date_time_unref(now);
    std::string res = h < 12 ? _("Good morning") : (h < 19 ? _("Good afternoon") : _("Good evening"));
    std::string name = g_get_real_name();
    if (name.empty() || name == "Unknown") {
        return res;
    }
    name = name.substr(0, name.find(' '));
    return res + ", " + name;
}

void openFile(const fs::path& p) {
    Control* control = st->control;
    close();
    control->openFile(p);
}

void applyFormat(PageType pt) {  // a copy: the button holding the original goes away in close()
    Control* control = st->control;
    close();
    control->getPageBackgroundChangeController()->changeCurrentPageBackground(pt);
    // A new sheet with the chosen format is not a change to be undone or saved
    control->getUndoRedoHandler()->clearContents();
}

auto formatCard(const PageTypeInfo& info) -> GtkWidget* {
    GtkWidget* b = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
    addClass(b, "jm-card");
    gtk_widget_set_tooltip_text(b, info.name.c_str());
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(frame, "jm-card-page");
    gtk_box_pack_start(GTK_BOX(frame), xoj::helper::createPreviewImage(info.page), false, false, 0);
    gtk_widget_set_halign(frame, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(box), frame, false, false, 0);
    GtkWidget* l = label(info.name, "jm-card-label", 0.5f);
    gtk_label_set_line_wrap(GTK_LABEL(l), true);
    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(l), 16);
    gtk_box_pack_start(GTK_BOX(box), l, false, false, 0);
    gtk_container_add(GTK_CONTAINER(b), box);
    g_signal_connect_data(
            b, "clicked", G_CALLBACK(+[](GtkButton*, PageType* pt) { applyFormat(*pt); }), new PageType(info.page),
            +[](gpointer d, GClosure*) { delete static_cast<PageType*>(d); }, GConnectFlags(0));
    return b;
}

/// The formats shown first, in this order (identified by format and configuration, the names are translated)
auto featuredRank(const PageType& pt) -> int {
    const std::pair<PageTypeFormat, const char*> featured[] = {{PageTypeFormat::Plain, ""},
                                                               {PageTypeFormat::Graph, ""},
                                                               {PageTypeFormat::Graph, "vm=70.85"},
                                                               {PageTypeFormat::Ruled, ""},
                                                               {PageTypeFormat::Graph, "vm=70.85,ex=1"},
                                                               {PageTypeFormat::Graph, "vm=70.85,lh=1"}};
    for (int i = 0; i < static_cast<int>(std::size(featured)); i++) {
        if (pt.format == featured[i].first && pt.config == featured[i].second) {
            return i;
        }
    }
    return -1;
}

void fillRecents();

auto recentRow(const RecentEntry& e) -> GtkWidget* {
    GtkWidget* row = gtk_list_box_row_new();
    addClass(row, "jm-recent-row");
    g_object_set_data_full(G_OBJECT(row), "jm-path", g_strdup(u8(e.path).c_str()), g_free);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_box_pack_start(GTK_BOX(box), icon(e.path.extension() == ".pdf" ? "xopp-start-pdf" : "xopp-start-file", 18),
                       false, false, 0);
    GtkWidget* text = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    GtkWidget* name = label(u8(e.path.filename()), "jm-recent-name");
    gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(text), name, false, false, 0);
    GtkWidget* folder = label(folderText(e.path), "jm-dim");
    gtk_label_set_ellipsize(GTK_LABEL(folder), PANGO_ELLIPSIZE_START);
    gtk_box_pack_start(GTK_BOX(text), folder, false, false, 0);
    gtk_box_pack_start(GTK_BOX(box), text, true, true, 0);
    gtk_box_pack_start(GTK_BOX(box), label(dateText(e.modified), "jm-dim", 1.0f), false, false, 0);

    GtkWidget* pin = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(pin), GTK_RELIEF_NONE);
    addClass(pin, "jm-pin");
    if (e.pinned) {
        addClass(pin, "pinned");
    }
    gtk_container_add(GTK_CONTAINER(pin), icon("xopp-start-pin", 16));
    gtk_widget_set_tooltip_text(pin, e.pinned ? _("Unpin") : _("Pin to the top"));
    g_signal_connect(pin, "clicked", G_CALLBACK(+[](GtkButton*, GtkWidget* row) {
                         fs::path p = fromU8(static_cast<const char*>(g_object_get_data(G_OBJECT(row), "jm-path")));
                         auto it = std::find(st->pinned.begin(), st->pinned.end(), p);
                         if (it == st->pinned.end()) {
                             st->pinned.insert(st->pinned.begin(), p);
                         } else {
                             st->pinned.erase(it);
                         }
                         savePinned(st->pinned);
                         // Rebuilt after this handler returns: the button belongs to the row being replaced
                         g_idle_add(
                                 +[](gpointer) -> gboolean {
                                     if (st) {
                                         fillRecents();
                                     }
                                     return G_SOURCE_REMOVE;
                                 },
                                 nullptr);
                     }),
                     row);
    gtk_box_pack_start(GTK_BOX(box), pin, false, false, 0);
    gtk_container_add(GTK_CONTAINER(row), box);
    return row;
}

void collectRecents() {
    st->recents.clear();
    auto files = RecentManager::getRecentFiles();
    auto add = [](auto& list) {
        for (auto& info: list) {
            auto p = Util::fromUri(gtk_recent_info_get_uri(info.get()));
            if (!p) {
                continue;
            }
            std::error_code ec;
            if (!fs::exists(*p, ec)) {
                continue;
            }
            st->recents.push_back({*p, gtk_recent_info_get_modified(info.get()), false});
        }
    };
    add(files.recentXoppFiles);
    add(files.recentPdfFiles);
    // Pinned files stay even when they fall off the recent list
    for (const auto& p: st->pinned) {
        auto it = std::find_if(st->recents.begin(), st->recents.end(), [&](auto& e) { return e.path == p; });
        std::error_code ec;
        if (it == st->recents.end() && fs::exists(p, ec)) {
            GStatBuf sb;
            time_t mtime = g_stat(u8(p).c_str(), &sb) == 0 ? sb.st_mtime : 0;
            st->recents.push_back({p, mtime, true});
        } else if (it != st->recents.end()) {
            it->pinned = true;
        }
    }
    std::stable_sort(st->recents.begin(), st->recents.end(), [](auto& a, auto& b) {
        if (a.pinned != b.pinned) {
            return a.pinned;
        }
        return a.modified > b.modified;
    });
}

void fillRecents() {
    collectRecents();
    GList* children = gtk_container_get_children(GTK_CONTAINER(st->recentList));
    for (GList* c = children; c; c = c->next) {
        gtk_widget_destroy(GTK_WIDGET(c->data));
    }
    g_list_free(children);
    for (const auto& e: st->recents) {
        gtk_container_add(GTK_CONTAINER(st->recentList), recentRow(e));
    }
    gtk_widget_show_all(st->recentList);
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(st->recentList));
}

auto buildNav() -> GtkWidget* {
    GtkWidget* nav = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    addClass(nav, "jm-start-nav");
    gtk_widget_set_size_request(nav, 240, -1);

    GtkWidget* brand = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    addClass(brand, "jm-brand");
    auto logo = st->control->getWindow()->getGladeSearchPath()->getFirstSearchPath() / "pixmaps" /
                "com.github.xournalpp.xournalpp.png";
    if (GdkPixbuf* pb = gdk_pixbuf_new_from_file_at_size(u8(logo).c_str(), 44, 44, nullptr)) {
        gtk_box_pack_start(GTK_BOX(brand), gtk_image_new_from_pixbuf(pb), false, false, 0);
        g_object_unref(pb);
    }
    GtkWidget* names = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_valign(names, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(names), label("Xournal JackMath", "jm-brand-name"), false, false, 0);
    gtk_box_pack_start(GTK_BOX(names), label(_("Calculation sheets by hand"), "jm-dim"), false, false, 0);
    gtk_box_pack_start(GTK_BOX(brand), names, false, false, 0);
    gtk_box_pack_start(GTK_BOX(nav), brand, false, false, 0);

    GtkWidget* newBtn = navButton("xopp-document-new", _("New"), G_CALLBACK(+[](GtkButton*, gpointer) { close(); }));
    addClass(newBtn, "active");
    gtk_widget_set_tooltip_text(newBtn, _("Blank sheet as it is now (Esc)"));
    gtk_box_pack_start(GTK_BOX(nav), newBtn, false, false, 0);
    gtk_box_pack_start(GTK_BOX(nav), navButton("xopp-document-open", _("Open…"), G_CALLBACK(+[](GtkButton*, gpointer) {
                                                   Control* c = st->control;
                                                   close();
                                                   c->askToOpenFile();
                                               })),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(nav),
                       navButton("xopp-start-pdf", _("Annotate a PDF…"), G_CALLBACK(+[](GtkButton*, gpointer) {
                                     Control* c = st->control;
                                     close();
                                     c->askToAnnotatePdf();
                                 })),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(nav), navButton("xopp-start-recent", _("Recent"), G_CALLBACK(+[](GtkButton*, gpointer) {
                                                   gtk_widget_grab_focus(st->search);
                                               })),
                       false, false, 0);

    GtkWidget* spacer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(nav), spacer, true, true, 0);

    gtk_box_pack_start(GTK_BOX(nav),
                       navButton("xopp-start-guide", _("User guide"), G_CALLBACK(+[](GtkButton*, gpointer) {
                                     GtkWindow* win = st->control->getGtkWindow();
                                     auto guide = Util::getConfigFile(GUIDE_FILE);
                                     std::error_code ec;
                                     auto uri = fs::exists(guide, ec) ? Util::toUri(guide) : std::nullopt;
                                     if (uri) {
                                         XojMsgBox::openURL(win, uri->c_str());
                                     } else {
                                         XojMsgBox::showHelp(win);
                                     }
                                 })),
                       false, false, 0);
    gtk_box_pack_start(GTK_BOX(nav),
                       navButton("xopp-start-settings", _("Preferences"), G_CALLBACK(+[](GtkButton*, gpointer) {
                                     st->control->getActionDatabase()->fireActivateAction(Action::PREFERENCES);
                                 })),
                       false, false, 0);

    GtkWidget* check = gtk_check_button_new_with_label(_("Show at startup"));
    addClass(check, "jm-dim");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), st->control->getSettings()->isShowStartScreen());
    g_signal_connect(check, "toggled", G_CALLBACK(+[](GtkToggleButton* b, gpointer) {
                         st->control->getSettings()->setShowStartScreen(gtk_toggle_button_get_active(b));
                     }),
                     nullptr);
    gtk_widget_set_margin_start(check, 12);
    gtk_widget_set_margin_top(check, 8);
    gtk_box_pack_start(GTK_BOX(nav), check, false, false, 0);
    return nav;
}

auto sectionHeader(const std::string& title, GtkWidget* right) -> GtkWidget* {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    addClass(box, "jm-section");
    gtk_box_pack_start(GTK_BOX(box), label(title, "jm-h2"), true, true, 0);
    if (right) {
        gtk_box_pack_end(GTK_BOX(box), right, false, false, 0);
    }
    return box;
}

auto buildMain() -> GtkWidget* {
    GtkWidget* main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    addClass(main, "jm-start-main");

    gtk_box_pack_start(GTK_BOX(main), label(greeting(), "jm-h1"), false, false, 0);
    gtk_box_pack_start(GTK_BOX(main), label(_("Choose a format to start a new sheet, or open a recent one."), "jm-sub"),
                       false, false, 0);

    // New sheet: featured formats, then the others behind "More formats…"
    st->moreLink = gtk_button_new_with_label(_("More formats…"));
    gtk_button_set_relief(GTK_BUTTON(st->moreLink), GTK_RELIEF_NONE);
    addClass(st->moreLink, "jm-link");
    g_signal_connect(st->moreLink, "clicked", G_CALLBACK(+[](GtkButton* b, gpointer) {
                         bool open = !gtk_revealer_get_reveal_child(GTK_REVEALER(st->more));
                         gtk_revealer_set_reveal_child(GTK_REVEALER(st->more), open);
                         gtk_button_set_label(b, open ? _("Fewer formats") : _("More formats…"));
                     }),
                     nullptr);
    gtk_box_pack_start(GTK_BOX(main), sectionHeader(_("New sheet"), st->moreLink), false, false, 0);

    std::vector<std::pair<int, const PageTypeInfo*>> featured;
    std::vector<const PageTypeInfo*> others;
    for (const auto& info: st->control->getPageTypes()->getPageTypes()) {
        int r = featuredRank(info->page);
        if (r >= 0) {
            featured.emplace_back(r, info.get());
        } else {
            others.push_back(info.get());
        }
    }
    std::sort(featured.begin(), featured.end(), [](auto& a, auto& b) { return a.first < b.first; });

    // Flow boxes: the cards wrap to the next line when the window is narrow
    auto cardFlow = [] {
        GtkWidget* flow = gtk_flow_box_new();
        gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
        gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), true);
        gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 12);
        gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 10);
        gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 10);
        gtk_widget_set_halign(flow, GTK_ALIGN_START);
        addClass(flow, "jm-cards");
        return flow;
    };
    GtkWidget* cards = cardFlow();
    for (auto& [r, info]: featured) {
        gtk_container_add(GTK_CONTAINER(cards), formatCard(*info));
    }
    gtk_box_pack_start(GTK_BOX(main), cards, false, false, 0);

    st->more = gtk_revealer_new();
    GtkWidget* flow = cardFlow();
    for (auto* info: others) {
        gtk_container_add(GTK_CONTAINER(flow), formatCard(*info));
    }
    gtk_container_add(GTK_CONTAINER(st->more), flow);
    gtk_box_pack_start(GTK_BOX(main), st->more, false, false, 0);

    // Recent files
    st->search = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(st->search), _("Search in recent files"));
    gtk_entry_set_width_chars(GTK_ENTRY(st->search), 22);
    g_signal_connect(st->search, "search-changed", G_CALLBACK(+[](GtkSearchEntry*, gpointer) {
                         gtk_list_box_invalidate_filter(GTK_LIST_BOX(st->recentList));
                     }),
                     nullptr);
    gtk_box_pack_start(GTK_BOX(main), sectionHeader(_("Recent"), st->search), false, false, 0);

    st->recentList = gtk_list_box_new();
    addClass(st->recentList, "jm-recent");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(st->recentList), GTK_SELECTION_NONE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(st->recentList), true);
    gtk_list_box_set_placeholder(GTK_LIST_BOX(st->recentList), [] {
        GtkWidget* l = label(_("No recent files yet."), "jm-dim", 0.5f);
        gtk_widget_set_margin_top(l, 18);
        gtk_widget_set_margin_bottom(l, 18);
        gtk_widget_show(l);
        return l;
    }());
    gtk_list_box_set_filter_func(
            GTK_LIST_BOX(st->recentList),
            +[](GtkListBoxRow* row, gpointer) -> gboolean {
                const char* q = gtk_entry_get_text(GTK_ENTRY(st->search));
                if (!q || !*q) {
                    return true;
                }
                const char* path = static_cast<const char*>(g_object_get_data(G_OBJECT(row), "jm-path"));
                gchar* name = g_utf8_casefold(u8(fromU8(path).filename()).c_str(), -1);
                gchar* needle = g_utf8_casefold(q, -1);
                bool match = strstr(name, needle) != nullptr;
                g_free(name);
                g_free(needle);
                return match;
            },
            nullptr, nullptr);
    g_signal_connect(st->recentList, "row-activated", G_CALLBACK(+[](GtkListBox*, GtkListBoxRow* row, gpointer) {
                         openFile(fromU8(static_cast<const char*>(g_object_get_data(G_OBJECT(row), "jm-path"))));
                     }),
                     nullptr);
    gtk_box_pack_start(GTK_BOX(main), st->recentList, false, false, 0);
    fillRecents();

    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), main);
    return scroll;
}

}  // namespace

void show(Control* control) {
    if (st) {
        return;
    }
    auto* win = control->getWindow();
    GtkWidget* overlay = win ? win->get("mainOverlay") : nullptr;
    if (!overlay) {
        return;
    }
    st = new State();
    st->control = control;
    st->pinned = loadPinned();

    st->screen = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    addClass(st->screen, "jm-start");
    gtk_widget_set_halign(st->screen, GTK_ALIGN_FILL);
    gtk_widget_set_valign(st->screen, GTK_ALIGN_FILL);
    gtk_box_pack_start(GTK_BOX(st->screen), buildNav(), false, false, 0);
    gtk_box_pack_start(GTK_BOX(st->screen), buildMain(), true, true, 0);

    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), st->screen);
    gtk_widget_show_all(st->screen);  // the revealer stays closed until "More formats…"
}

void close() {
    if (!st) {
        return;
    }
    State* s = st;
    st = nullptr;
    gtk_widget_destroy(s->screen);
    delete s;
}

bool isShown() { return st != nullptr; }

bool handleKey(GdkEventKey* ev) {
    if (!st || ev->type != GDK_KEY_PRESS) {
        return false;
    }
    // Esc goes to the blank sheet; a shortcut with Ctrl (open, new, …) closes the screen and is then run as usual
    if (ev->keyval == GDK_KEY_Escape) {
        close();
        return true;
    }
    if ((ev->state & GDK_CONTROL_MASK) && !gtk_widget_has_focus(st->search)) {
        close();
    }
    return false;
}

}  // namespace xoj::start
