/*
 * Xournal JackMath
 *
 * List of functions offered while typing in a calculation region, as in Excel: typing "se" offers SENO(ángulo)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <gtk/gtk.h>

#include "calc/CalcEngine.h"
#include "util/Point.h"
#include "util/raii/GObjectSPtr.h"

class MainWindow;

class FunctionCompletion {
public:
    using Callback = std::function<void(const xoj::calc::FunctionInfo&)>;

    /// onChoose is called when a function is clicked
    FunctionCompletion(MainWindow* mainWindow, Callback onChoose);
    ~FunctionCompletion();

    FunctionCompletion(const FunctionCompletion&) = delete;
    FunctionCompletion& operator=(const FunctionCompletion&) = delete;

    /// Show the list with its top left corner at position (in the coordinates of the drawing area). If highlight is
    /// true, the first function is highlighted (Tab or Enter choose it)
    void show(const std::vector<const xoj::calc::FunctionInfo*>& functions, xoj::util::Point<int> position,
              bool highlight);
    void hide();
    bool isVisible() const;

    /// Move the highlight up (-1) or down (+1)
    void moveHighlight(int delta);
    /// The highlighted function, nullptr if none
    const xoj::calc::FunctionInfo* highlighted() const;

private:
    static bool getOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* alloc,
                                   FunctionCompletion* self);
    static void rowActivated(GtkListBox* box, GtkListBoxRow* row, FunctionCompletion* self);

    MainWindow* mainWindow;
    GtkOverlay* overlay;
    xoj::util::raii::WidgetSPtr widget;  ///< frame around the list
    GtkListBox* list;
    Callback onChoose;

    std::vector<const xoj::calc::FunctionInfo*> functions;  ///< in the order of the rows
    int current = -1;                                       ///< highlighted row
    xoj::util::Point<int> position = {};
    gulong positionSignal = 0;
};
