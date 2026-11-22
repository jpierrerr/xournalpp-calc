/*
 * Xournal JackMath (xournalpp-calc)
 *
 * Start screen: shown over the whole main window when the program starts without a file.
 * New sheet formats with a preview, recent files (search, pin), open, annotate a PDF, guide and preferences.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gdk/gdk.h>  // for GdkEventKey

class Control;

namespace xoj::start {

/// Shows the start screen over the main window (does nothing if it is already shown).
/// backstage: opened from "File" with a document in use (like Word): a format makes a new document, and the side
/// panel also has Back, Save, Save as, Export as PDF, Print and Quit
void show(Control* control, bool backstage = false);

/// Removes the start screen, leaving the current document as it is
void close();

/// Whether the start screen is currently shown
bool isShown();

/// Called first for every key of the main window: true if the start screen used it
bool handleKey(GdkEventKey* event);

}  // namespace xoj::start
