/*
 * Xournal++ Calc
 *
 * Text formatting buttons as in a word processor: font, size, bold, italic, underline, colors, alignment...
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

#include <gtk/gtk.h>

#include "AbstractToolItem.h"

class Control;

namespace xoj::text {

/// The block of format buttons. They act on the text being edited and follow the format at its cursor.
/// mini: the short version shown with the right-click menu
GtkWidget* createFormatPanel(Control* control, bool horizontal, bool mini);

/// Right click in the text being edited: format buttons, cut, copy and paste. (x, y): in the coordinates of `parent`
void showTextContextMenu(Control* control, GtkWidget* parent, double x, double y);

}  // namespace xoj::text

/// The format block as a toolbar item ("TEXT_FORMAT")
class TextFormatToolItem: public AbstractToolItem {
public:
    explicit TextFormatToolItem(Control* control);
    std::string getToolDisplayName() const override;
    GtkWidget* getNewToolIcon() const override;
    xoj::util::WidgetSPtr createItem(bool horizontal) override;

private:
    Control* control;
};
