#include "ExcelCellDialog.h"

#include <algorithm>
#include <cctype>
#include <memory>

#include "calc/ExcelLink.h"
#include "calc/XlsxFile.h"

namespace fs = std::filesystem;
using namespace xoj::calc;

namespace xoj::popup {

namespace {

constexpr int MAX_ROWS = 500;
constexpr int MAX_COLS = 60;
constexpr const char* SELECTED_COLOR = "#9ec5fe";
constexpr const char* HEADER_COLOR = "#e6e6e6";

struct State {
    fs::path workbook;
    bool input = true;
    bool live = false;  ///< the workbook is open in Excel
    std::vector<ExcelVariable> variables;

    GtkWidget* dialog = nullptr;
    GtkWidget* sheetCombo = nullptr;
    GtkWidget* tree = nullptr;
    GtkWidget* cellLabel = nullptr;
    GtkWidget* errorLabel = nullptr;
    GtkWidget* nameEntry = nullptr;  ///< input: variable name; output: entry of the variable combo
    GtkWidget* varCombo = nullptr;   ///< output only
    GtkWidget* unitEntry = nullptr;
    GtkWidget* showCheck = nullptr;  ///< input only

    SheetPreview preview;
    int selRow = 0;  ///< 1-based, 0 = none
    int selCol = 0;
    std::string suggestedName;
    std::string suggestedUnit;
};

std::string sheetName(State* s) {
    gchar* t = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(s->sheetCombo));
    std::string r = t ? t : "";
    g_free(t);
    return r;
}

std::string cellText(const State* s, int row, int col) {
    auto it = s->preview.cells.find({row, col});
    return it == s->preview.cells.end() ? std::string() : it->second;
}

/// "Hoja1!C2", with quotes if the sheet name needs them
std::string reference(State* s) {
    std::string sheet = sheetName(s);
    std::string cell = columnName(s->selCol) + std::to_string(s->selRow);
    bool plain = !sheet.empty() && std::all_of(sheet.begin(), sheet.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80;
    });
    if (plain) {
        return sheet + "!" + cell;
    }
    std::string quoted;
    for (char c: sheet) {
        quoted += c;
        if (c == '\'') {
            quoted += '\'';
        }
    }
    return "'" + quoted + "'!" + cell;
}

void setError(State* s, const std::string& msg) {
    if (msg.empty()) {
        gtk_label_set_text(GTK_LABEL(s->errorLabel), "");
        return;
    }
    gchar* esc = g_markup_escape_text(msg.c_str(), -1);
    std::string markup = std::string("<span foreground=\"#c01c28\">") + esc + "</span>";
    g_free(esc);
    gtk_label_set_markup(GTK_LABEL(s->errorLabel), markup.c_str());
}

/// A label next to a cell, like "q (kN/m)" or "L [m]": name and unit suggested for the variable
void suggestFromLabel(const std::string& label, std::string& name, std::string& unit) {
    name.clear();
    unit.clear();
    const char* p = label.c_str();
    while (*p == ' ') {
        p++;
    }
    const char* start = p;
    bool first = true;
    while (*p) {
        gunichar c = g_utf8_get_char(p);
        if (!(g_unichar_isalpha(c) || c == '_' || (!first && g_unichar_isdigit(c)))) {
            break;
        }
        first = false;
        p = g_utf8_next_char(p);
    }
    name.assign(start, static_cast<size_t>(p - start));
    // Only a name by itself: "q", "q (kN/m)", "L [m]", "fc:", "b =" (not a phrase like "Columna C3")
    while (*p == ' ') {
        p++;
    }
    if (g_utf8_strlen(name.c_str(), -1) > 12 || !(*p == '\0' || *p == '(' || *p == '[' || *p == ':' || *p == '=')) {
        name.clear();
    }
    size_t open = label.find_first_of("([");
    size_t close = label.find_first_of(")]", open == std::string::npos ? 0 : open);
    if (open != std::string::npos && close != std::string::npos && close > open + 1) {
        std::string u = label.substr(open + 1, close - open - 1);
        try {
            Environment none;
            evaluateExpression(u, none);  // only if it is a valid unit
            unit = u;
        } catch (const CalcError&) {}
    }
}

void updateSelectionLabel(State* s) {
    if (s->selRow == 0) {
        gtk_label_set_text(GTK_LABEL(s->cellLabel), "Haz clic en la celda (doble clic para insertar de una vez)");
        return;
    }
    std::string value = cellText(s, s->selRow, s->selCol);
    std::string text = "Celda: " + reference(s) + "      Valor: " + (value.empty() ? "(vacía)" : value);
    gtk_label_set_text(GTK_LABEL(s->cellLabel), text.c_str());
}

void cellDataFunc(GtkTreeViewColumn*, GtkCellRenderer* renderer, GtkTreeModel* model, GtkTreeIter* iter,
                  gpointer data) {
    auto* s = static_cast<State*>(data);
    int col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(renderer), "xoj-col"));
    GtkTreePath* path = gtk_tree_model_get_path(model, iter);
    int row = gtk_tree_path_get_indices(path)[0] + 1;
    gtk_tree_path_free(path);
    if (col == 0) {
        g_object_set(renderer, "cell-background", HEADER_COLOR, "foreground", "#444444", nullptr);
        return;
    }
    const bool sel = row == s->selRow && col == s->selCol;
    g_object_set(renderer, "cell-background", sel ? SELECTED_COLOR : nullptr, "foreground", sel ? "#000000" : nullptr,
                 "weight", sel ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL, nullptr);
}

void loadSheet(State* s) {
    std::string sheet = sheetName(s);
    s->selRow = s->selCol = 0;
    s->preview = SheetPreview{};
    setError(s, "");
    try {
        std::optional<SheetPreview> pv;
        if (s->live) {
            pv = excelapp::preview(s->workbook, sheet, MAX_ROWS, MAX_COLS);
        }
        s->preview = pv ? *pv : xlsx::preview(s->workbook, sheet, MAX_ROWS, MAX_COLS);
    } catch (const CalcError& e) {
        setError(s, e.what());
    }

    const int nRows = std::clamp(s->preview.rows + 5, 30, MAX_ROWS);
    const int nCols = std::clamp(s->preview.cols + 3, 12, MAX_COLS);

    GtkTreeView* tv = GTK_TREE_VIEW(s->tree);
    while (GtkTreeViewColumn* c = gtk_tree_view_get_column(tv, 0)) {
        gtk_tree_view_remove_column(tv, c);
    }
    std::vector<GType> types(static_cast<size_t>(nCols + 1), G_TYPE_STRING);
    GtkListStore* store = gtk_list_store_newv(nCols + 1, types.data());
    for (int r = 1; r <= nRows; r++) {
        GtkTreeIter it;
        gtk_list_store_append(store, &it);
        gtk_list_store_set(store, &it, 0, std::to_string(r).c_str(), -1);
        for (int c = 1; c <= nCols; c++) {
            auto cell = s->preview.cells.find({r, c});
            if (cell != s->preview.cells.end()) {
                gtk_list_store_set(store, &it, c, cell->second.c_str(), -1);
            }
        }
    }
    for (int c = 0; c <= nCols; c++) {
        GtkCellRenderer* rend = gtk_cell_renderer_text_new();
        g_object_set(rend, "ellipsize", PANGO_ELLIPSIZE_END, "xalign", c == 0 ? 0.5f : 0.0f, nullptr);
        g_object_set_data(G_OBJECT(rend), "xoj-col", GINT_TO_POINTER(c));
        std::string title = c == 0 ? "" : columnName(c);
        GtkTreeViewColumn* col = gtk_tree_view_column_new_with_attributes(title.c_str(), rend, "text", c, nullptr);
        g_object_set_data(G_OBJECT(col), "xoj-col", GINT_TO_POINTER(c));
        gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
        gtk_tree_view_column_set_fixed_width(col, c == 0 ? 42 : 86);
        gtk_tree_view_column_set_alignment(col, 0.5f);
        gtk_tree_view_column_set_cell_data_func(col, rend, cellDataFunc, s, nullptr);
        gtk_tree_view_append_column(tv, col);
    }
    gtk_tree_view_set_model(tv, GTK_TREE_MODEL(store));
    g_object_unref(store);
    updateSelectionLabel(s);
}

void selectCell(State* s, int row, int col) {
    s->selRow = row;
    s->selCol = col;
    gtk_widget_queue_draw(s->tree);
    updateSelectionLabel(s);
    setError(s, "");
    if (s->input) {
        // Name and unit from the label at the left of the cell (only if the user did not type their own)
        std::string name, unit;
        suggestFromLabel(cellText(s, row, col - 1), name, unit);
        // Without a unit at the left, the header of the column may have it: "P (kN)"
        for (int r = row - 1; unit.empty() && r >= 1; r--) {
            std::string above = cellText(s, r, col);
            if (!above.empty() && above.find_first_of("([") != std::string::npos) {
                std::string ignored;
                suggestFromLabel(above, ignored, unit);
                break;
            }
        }
        const std::string currentName = gtk_entry_get_text(GTK_ENTRY(s->nameEntry));
        if (currentName.empty() || currentName == s->suggestedName) {
            gtk_entry_set_text(GTK_ENTRY(s->nameEntry), name.c_str());
            s->suggestedName = name;
        }
        const std::string currentUnit = gtk_entry_get_text(GTK_ENTRY(s->unitEntry));
        if (currentUnit.empty() || currentUnit == s->suggestedUnit) {
            gtk_entry_set_text(GTK_ENTRY(s->unitEntry), unit.c_str());
            s->suggestedUnit = unit;
        }
    }
}

gboolean onTreeButton(GtkWidget* tree, GdkEventButton* ev, State* s) {
    if (ev->button != 1 || (ev->type != GDK_BUTTON_PRESS && ev->type != GDK_2BUTTON_PRESS)) {
        return false;
    }
    GtkTreePath* path = nullptr;
    GtkTreeViewColumn* column = nullptr;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(tree), static_cast<int>(ev->x), static_cast<int>(ev->y), &path,
                                       &column, nullptr, nullptr)) {
        return true;
    }
    int row = gtk_tree_path_get_indices(path)[0] + 1;
    gtk_tree_path_free(path);
    int col = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column), "xoj-col"));
    if (col == 0) {
        return true;
    }
    selectCell(s, row, col);
    if (ev->type == GDK_2BUTTON_PRESS) {
        gtk_dialog_response(GTK_DIALOG(s->dialog), GTK_RESPONSE_ACCEPT);
    }
    return true;
}

void onVariableChanged(GtkComboBox*, State* s) {
    const std::string name = gtk_entry_get_text(GTK_ENTRY(s->nameEntry));
    for (const auto& v: s->variables) {
        if (v.name == name) {
            const std::string currentUnit = gtk_entry_get_text(GTK_ENTRY(s->unitEntry));
            if (currentUnit.empty() || currentUnit == s->suggestedUnit) {
                gtk_entry_set_text(GTK_ENTRY(s->unitEntry), v.unit.c_str());
                s->suggestedUnit = v.unit;
            }
        }
    }
}

bool isIdentifier(const std::string& n) {
    if (n.empty()) {
        return false;
    }
    bool first = true;
    for (const char* p = n.c_str(); *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (!(g_unichar_isalpha(c) || c == '_' || (!first && (g_unichar_isdigit(c) || c == '\'')))) {
            return false;
        }
        first = false;
    }
    return true;
}

/// Check the fields; fills the choice or shows what is wrong
bool validate(State* s, ExcelCellChoice& out) {
    if (s->selRow == 0) {
        setError(s, "Elige una celda en la vista previa.");
        return false;
    }
    std::string name = gtk_entry_get_text(GTK_ENTRY(s->nameEntry));
    std::string unit = gtk_entry_get_text(GTK_ENTRY(s->unitEntry));
    auto trim = [](std::string& t) {
        t.erase(0, t.find_first_not_of(" \t"));
        t.erase(t.find_last_not_of(" \t") + 1);
    };
    trim(name);
    trim(unit);
    if (!isIdentifier(name)) {
        setError(s, s->input ? "Escribe el nombre de la variable (p. ej. q, L_1, Pu)." :
                               "Elige o escribe la variable que se envía.");
        return false;
    }
    Quantity u{1.0, {}};
    if (!unit.empty()) {
        try {
            Environment none;
            u = evaluateExpression(unit, none);
        } catch (const CalcError& e) {
            setError(s, "Unidad no válida: " + unit + " (" + e.what() + ")");
            return false;
        }
    }
    if (!s->input) {
        auto v = std::find_if(s->variables.begin(), s->variables.end(), [&](auto& x) { return x.name == name; });
        if (v == s->variables.end()) {
            setError(s, "La variable " + name + " no está definida en la hoja.");
            return false;
        }
        if (unit.empty() && !v->dim.isDimensionless()) {
            setError(s, "Indica la unidad en que se envía " + name + " (p. ej. " + v->unit + ").");
            return false;
        }
        if (!unit.empty() && !(u.dim == v->dim)) {
            setError(s, "La unidad " + unit + " no corresponde a " + name + " (" + v->unit + ").");
            return false;
        }
    }
    out.ref = reference(s);
    out.variable = name;
    out.unit = unit;
    out.showValue = s->showCheck && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->showCheck));
    return true;
}

GtkWidget* label(const char* text) {
    GtkWidget* l = gtk_label_new(text);
    gtk_widget_set_halign(l, GTK_ALIGN_START);
    return l;
}

}  // namespace

std::optional<ExcelCellChoice> pickExcelCell(GtkWindow* parent, const fs::path& workbook, bool input,
                                             const std::vector<ExcelVariable>& variables) {
    auto s = std::make_unique<State>();
    s->workbook = workbook;
    s->input = input;
    s->variables = variables;

    std::vector<std::string> sheets;
    try {
        if (auto live = excelapp::sheetNames(workbook)) {
            sheets = *live;
            s->live = true;
        } else {
            sheets = xlsx::sheetNames(workbook);
        }
    } catch (const CalcError& e) {
        GtkWidget* msg = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                "No se pudo leer el libro de Excel");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(msg), "%s", e.what());
        gtk_dialog_run(GTK_DIALOG(msg));
        gtk_widget_destroy(msg);
        return std::nullopt;
    }

    const char* title = input ? "Valor de entrada desde Excel" : "Valor de salida hacia Excel";
    s->dialog = gtk_dialog_new_with_buttons(
            title, parent, GtkDialogFlags(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT), "_Cancelar",
            GTK_RESPONSE_CANCEL, "_Insertar", GTK_RESPONSE_ACCEPT, nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(s->dialog), GTK_RESPONSE_ACCEPT);
    gtk_window_set_default_size(GTK_WINDOW(s->dialog), 760, 560);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(s->dialog));
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_box_pack_start(GTK_BOX(content), box, true, true, 0);

    // Workbook
    {
        std::string name = pathToUtf8(workbook.filename());
        gchar* esc = g_markup_escape_text(name.c_str(), -1);
        std::string markup = std::string("<b>Libro:</b> ") + esc +
                             (s->live ? "   <i>(abierto en Excel)</i>" : "   <i>(archivo)</i>");
        g_free(esc);
        GtkWidget* l = label(nullptr);
        gtk_label_set_markup(GTK_LABEL(l), markup.c_str());
        gtk_box_pack_start(GTK_BOX(box), l, false, false, 0);
    }

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_box_pack_start(GTK_BOX(box), grid, false, false, 0);
    int row = 0;

    // Output: the variable comes first
    if (!input) {
        s->varCombo = gtk_combo_box_text_new_with_entry();
        for (const auto& v: variables) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->varCombo), v.name.c_str());
        }
        s->nameEntry = gtk_bin_get_child(GTK_BIN(s->varCombo));
        gtk_entry_set_placeholder_text(GTK_ENTRY(s->nameEntry), "variable de la hoja, p. ej. M");
        g_signal_connect(s->varCombo, "changed", G_CALLBACK(onVariableChanged), s.get());
        gtk_grid_attach(GTK_GRID(grid), label("1. Variable que se envía:"), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), s->varCombo, 1, row++, 1, 1);
    }

    s->sheetCombo = gtk_combo_box_text_new();
    for (const auto& n: sheets) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->sheetCombo), n.c_str());
    }
    gtk_grid_attach(GTK_GRID(grid), label(input ? "1. Hoja:" : "2. Hoja:"), 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), s->sheetCombo, 1, row++, 1, 1);

    gtk_box_pack_start(GTK_BOX(box), label(input ? "2. Elige la celda:" : "3. Elige la celda de destino:"), false,
                       false, 0);

    s->tree = gtk_tree_view_new();
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(s->tree), GTK_TREE_VIEW_GRID_LINES_BOTH);
    gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(s->tree), false);
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(s->tree)), GTK_SELECTION_NONE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(s->tree), false);
    g_signal_connect(s->tree, "button-press-event", G_CALLBACK(onTreeButton), s.get());
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);
    gtk_widget_set_size_request(scroll, 640, 300);
    gtk_container_add(GTK_CONTAINER(scroll), s->tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, true, true, 0);

    s->cellLabel = label("");
    gtk_box_pack_start(GTK_BOX(box), s->cellLabel, false, false, 0);

    GtkWidget* grid2 = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid2), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid2), 8);
    gtk_box_pack_start(GTK_BOX(box), grid2, false, false, 0);
    row = 0;
    if (input) {
        s->nameEntry = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(s->nameEntry), "p. ej. q");
        gtk_entry_set_activates_default(GTK_ENTRY(s->nameEntry), true);
        gtk_grid_attach(GTK_GRID(grid2), label("3. Nombre de la variable:"), 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid2), s->nameEntry, 1, row++, 1, 1);
    }
    s->unitEntry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->unitEntry), input ? "p. ej. kN (vacío si no tiene)" : "p. ej. kN·m");
    gtk_entry_set_activates_default(GTK_ENTRY(s->unitEntry), true);
    gtk_grid_attach(GTK_GRID(grid2), label(input ? "4. Unidad del valor:" : "4. Unidad en que se escribe:"), 0, row, 1,
                    1);
    gtk_grid_attach(GTK_GRID(grid2), s->unitEntry, 1, row++, 1, 1);
    if (input) {
        s->showCheck = gtk_check_button_new_with_label("Mostrar el valor en la hoja");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->showCheck), true);
        gtk_grid_attach(GTK_GRID(grid2), s->showCheck, 1, row++, 1, 1);
    }

    s->errorLabel = label("");
    gtk_box_pack_start(GTK_BOX(box), s->errorLabel, false, false, 0);

    g_signal_connect_swapped(s->sheetCombo, "changed", G_CALLBACK(+[](State* st) { loadSheet(st); }), s.get());
    gtk_combo_box_set_active(GTK_COMBO_BOX(s->sheetCombo), 0);  // loads the first sheet

    gtk_widget_show_all(s->dialog);
    std::optional<ExcelCellChoice> result;
    while (true) {
        int resp = gtk_dialog_run(GTK_DIALOG(s->dialog));
        if (resp != GTK_RESPONSE_ACCEPT) {
            break;
        }
        ExcelCellChoice choice;
        if (validate(s.get(), choice)) {
            result = choice;
            break;
        }
    }
    gtk_widget_destroy(s->dialog);
    return result;
}

}  // namespace xoj::popup
