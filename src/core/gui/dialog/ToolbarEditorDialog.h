/*
 * Xournal++ Calc
 *
 * Window to choose which tools are shown in the toolbars, in which order and in which bar
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <gtk/gtk.h>

class Control;
class ToolbarData;

namespace xoj::popup {

/**
 * One list per bar: the tools shown (ticked, in order; drag them or use Up/Down) and below the hidden ones (tick one
 * to show it at the end of this bar; a tool that is in another bar moves here). Every change is applied at once and
 * saved. A predefined layout is copied the first time it is changed; "Restore original" goes back to it.
 */
class ToolbarEditorDialog {
public:
    /// Show the window (only one at a time)
    static void show(Control* control);

    ~ToolbarEditorDialog();

private:
    explicit ToolbarEditorDialog(Control* control);

    struct Row;

    void fillBarCombo();
    void rebuildList();
    void scheduleRefresh();

    /// The toolbar layout being edited; a predefined one is copied first
    ToolbarData* editableData();
    std::vector<std::string> itemsOf(const std::string& bar) const;
    void setItems(const std::string& bar, const std::vector<std::string>& items);
    void applyAndSave();

    void toggleRow(const Row& row, bool active);
    void moveSelected(int delta);
    void moveSelectedToBar();
    void moveVisible(int from, int to);
    void restoreOriginal();
    void openDragEditor();

    std::string displayName(const std::string& item) const;
    GtkWidget* newIcon(const std::string& item) const;
    std::string barLabel(const std::string& bar) const;
    std::string barOf(const std::string& item) const;

    static void onDragDataReceived(GtkWidget* w, GdkDragContext* ctx, int x, int y, GtkSelectionData* data,
                                   guint info, guint time, ToolbarEditorDialog* self);

    Control* control;
    GtkWidget* window = nullptr;
    GtkWidget* barCombo = nullptr;
    GtkWidget* moveCombo = nullptr;
    GtkWidget* scroll = nullptr;
    GtkWidget* list = nullptr;
    GtkWidget* layoutLabel = nullptr;
    std::string currentBar = "toolbarTop1";
    int selected = -1;  ///< index in the bar of the selected (shown) tool
    guint refreshSource = 0;
    std::vector<std::unique_ptr<Row>> rows;

    static ToolbarEditorDialog* instance;
};

}  // namespace xoj::popup
