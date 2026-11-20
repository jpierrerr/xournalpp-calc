#include "ColorListToolItem.h"

#include <gtk/gtk.h>

#include "control/Control.h"
#include "control/actions/ActionDatabase.h"
#include "gui/toolbarMenubar/icon/ColorIcon.h"
#include "gui/toolbarMenubar/model/ColorPalette.h"
#include "util/GtkUtil.h"
#include "util/NamedColor.h"
#include "util/gtk4_helper.h"
#include "util/i18n.h"

constexpr int ICON_SIZE = 22;
constexpr int COLUMNS = 6;

ColorListToolItem::ColorListToolItem(Control* control, ActionDatabase& db):
        AbstractToolItem("COLOR_LIST", Category::COLORS), control(control), gAction(db.getAction(Action::TOOL_COLOR)) {}

auto ColorListToolItem::createItem(bool horizontal) -> xoj::util::WidgetSPtr {
    GtkWidget* mb = gtk_menu_button_new();
    gtk_widget_set_can_focus(mb, false);
    gtk_button_set_relief(GTK_BUTTON(mb), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(mb, getToolDisplayName().c_str());
    gtk_menu_button_set_direction(GTK_MENU_BUTTON(mb), horizontal ? GTK_ARROW_DOWN : GTK_ARROW_RIGHT);

    auto setIcon = [](GtkWidget* btn, Color c) {
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        gtk_box_append(GTK_BOX(box), ColorIcon::newGtkImage(c, ICON_SIZE, true));
        gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_MENU));
        gtk_widget_show_all(box);
        gtk_button_set_child(GTK_BUTTON(btn), box);
    };
    xoj::util::GVariantSPtr v(g_action_get_state(G_ACTION(gAction.get())), xoj::util::adopt);
    setIcon(mb, getGVariantValue<Color>(v.get()));

    // The list of colors
    GtkWidget* popover = gtk_popover_new(mb);
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 2);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 2);
    const std::string colorAction = std::string("win.") + Action_toString(Action::TOOL_COLOR);
    const Palette& palette = control->getPalette();
    for (size_t i = 0; i < palette.size(); i++) {
        const NamedColor& nc = palette.getColorAt(i);
        GtkWidget* b = gtk_button_new();
        gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
        gtk_widget_set_can_focus(b, false);
        gtk_button_set_child(GTK_BUTTON(b), ColorIcon::newGtkImage(nc.getColor(), ICON_SIZE, true));
        gtk_widget_set_tooltip_text(b, nc.getName().c_str());
        gtk_actionable_set_action_name(GTK_ACTIONABLE(b), colorAction.c_str());
        xoj::util::GVariantSPtr target = xoj::util::makeGVariantSPtr(nc.getColor());
        gtk_actionable_set_action_target_value(GTK_ACTIONABLE(b), target.get());
        g_signal_connect_swapped(b, "clicked", G_CALLBACK(gtk_popover_popdown), popover);
        gtk_grid_attach(GTK_GRID(grid), b, static_cast<int>(i % COLUMNS), static_cast<int>(i / COLUMNS), 1, 1);
    }
    gtk_box_append(GTK_BOX(vbox), grid);
    GtkWidget* other = gtk_button_new_with_label(_("Other color…"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(other),
                                   (std::string("win.") + Action_toString(Action::SELECT_COLOR)).c_str());
    g_signal_connect_swapped(other, "clicked", G_CALLBACK(gtk_popover_popdown), popover);
    gtk_box_append(GTK_BOX(vbox), other);
    gtk_widget_show_all(vbox);
    gtk_container_add(GTK_CONTAINER(popover), vbox);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(mb), popover);

    // The button shows the current color and is off when the tool has no color
    g_object_bind_property(gAction.get(), "enabled", mb, "sensitive", G_BINDING_SYNC_CREATE);
    g_signal_connect_object(gAction.get(), "notify::state", G_CALLBACK(+[](GObject* a, GParamSpec*, gpointer btn) {
                                xoj::util::GVariantSPtr state(g_action_get_state(G_ACTION(a)), xoj::util::adopt);
                                Color c = getGVariantValue<Color>(state.get());
                                GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
                                gtk_box_append(GTK_BOX(box), ColorIcon::newGtkImage(c, ICON_SIZE, true));
                                gtk_box_append(GTK_BOX(box), gtk_image_new_from_icon_name("pan-down-symbolic",
                                                                                          GTK_ICON_SIZE_MENU));
                                gtk_widget_show_all(box);
                                gtk_button_set_child(GTK_BUTTON(btn), box);
                            }),
                            mb, GConnectFlags(0));

    GtkToolItem* it = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(it), mb);
    return xoj::util::WidgetSPtr(GTK_WIDGET(it), xoj::util::adopt);
}

auto ColorListToolItem::getToolDisplayName() const -> std::string { return _("Colors (list)"); }

auto ColorListToolItem::getNewToolIcon() const -> GtkWidget* {
    return ColorIcon::newGtkImage(Colors::gray, 16, true);
}
