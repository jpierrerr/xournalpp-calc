/*
 * Xournal++ Calc
 *
 * Floating palette with Greek letters and math symbols
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gtk/gtk.h>

class Control;

namespace xoj::popup {

/**
 * Create the palette window. Clicking a symbol inserts it in the text being edited; if no text is being edited,
 * the symbol is copied to the clipboard. The window does not take the keyboard focus, so typing can go on.
 * Closing the window only hides it.
 */
GtkWidget* createGreekPalette(Control* control, GtkWindow* parent);

}  // namespace xoj::popup
