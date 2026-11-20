/*
 * Xournal++ Calc
 *
 * One toolbar button with the current color; it opens the list of the palette colors
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

#include <gtk/gtk.h>

#include "control/actions/ActionRef.h"

#include "AbstractToolItem.h"

class ActionDatabase;
class Control;

class ColorListToolItem: public AbstractToolItem {
public:
    ColorListToolItem(Control* control, ActionDatabase& db);
    ~ColorListToolItem() override = default;

    std::string getToolDisplayName() const override;
    GtkWidget* getNewToolIcon() const override;
    xoj::util::WidgetSPtr createItem(bool horizontal) override;

private:
    Control* control;
    ActionRef gAction;  ///< Action::TOOL_COLOR
};
