#include "TextEditor.h"

#include <algorithm>  // for count
#include <cctype>
#include <cmath>      // for ceil
#include <cstring>    // for strcmp, size_t
#include <memory>     // for allocator, make_unique, __shared_p...
#include <string>     // for std::string()
#include <utility>    // for move

#include <gdk/gdkkeysyms.h>  // for GDK_KEY_B, GDK_KEY_ISO_Enter, GDK_...
#include <glib-object.h>     // for g_object_get, g_object_unref, G_CA...

#include "calc/CalcDocument.h"  // for documentVariables
#include "calc/CalcEngine.h"    // for greekForLatin
#include "calc/CalcGrid.h"      // for alignmentGrid
#include "calc/MathLayout.h"    // for Formula
#include "control/AudioController.h"
#include "control/Control.h"  // for Control
#include "control/actions/ActionDatabase.h"
#include "control/settings/Settings.h"
#include "control/tools/SpellChecker.h"  // for misspelled, suggest
#include "gui/FlyingClickableIcon.h"
#include "gui/FunctionCompletion.h"
#include "gui/XournalppCursor.h"  // for XournalppCursor
#include "model/Document.h"       // for Document
#include "model/Font.h"           // for XojFont
#include "model/Text.h"           // for Text
#include "model/TextAlignment.h"  // for TextAlignment
#include "model/XojPage.h"        // for XojPage
#include "undo/DeleteUndoAction.h"
#include "undo/InsertUndoAction.h"
#include "undo/TextBoxUndoAction.h"
#include "undo/UndoRedoHandler.h"  // for UndoRedoHandler
#include "util/Assert.h"
#include "util/DispatchPool.h"
#include "util/Range.h"
#include "util/glib_casts.h"  // for wrap_for_once_v
#include "util/gtk4_helper.h"
#include "util/matrix/RectangleMultiply.h"
#include "util/raii/CStringWrapper.h"
#include "util/safe_casts.h"  // for round_cast, as_unsigned
#include "view/overlays/TextEditionView.h"

#include "TextEditorKeyBindings.h"

class UndoAction;

static constexpr auto MOVE_ICON_NAME = "xopp-move";
static constexpr auto EXTEND_ICON_NAME = "xopp-wrap";

/** GtkTextBuffer helper functions **/
static auto getIteratorAtCursor(GtkTextBuffer* buffer) -> GtkTextIter {
    GtkTextIter cursorIter = {nullptr};
    gtk_text_buffer_get_iter_at_mark(buffer, &cursorIter, gtk_text_buffer_get_insert(buffer));
    return cursorIter;
}

/**
 * @brief Compute the byte offset of an iterator in the GtkTextBuffer
 *
 * NB: This is much faster than relying on g_utf8_offset_to_pointer
 */
static auto getByteOffsetOfIterator(GtkTextIter it) -> int {
    // Bytes from beginning of line to iterator
    int pos = gtk_text_iter_get_line_index(&it);
    gtk_text_iter_set_line_index(&it, 0);
    // Count bytes of previous lines
    while (gtk_text_iter_backward_line(&it)) {
        pos += gtk_text_iter_get_bytes_in_line(&it);
    }
    return pos;
}

static auto getByteOffsetOfCursor(GtkTextBuffer* buffer) -> int {
    return getByteOffsetOfIterator(getIteratorAtCursor(buffer));
}

/**
 * @brief Get an iterator at the prescribed byte index.
 *
 * NB: This is much faster than relying on g_utf8_pointer_to_offset for long texts
 */
static auto getIteratorAtByteOffset(GtkTextBuffer* buf, int byteIndex) {
    xoj_assert(byteIndex >= 0);
    GtkTextIter it = {nullptr};
    gtk_text_buffer_get_start_iter(buf, &it);

    // Fast forward to the beginning of the line containing our target destination
    for (int linelength = gtk_text_iter_get_bytes_in_line(&it);
         linelength <= byteIndex && gtk_text_iter_forward_line(&it);
         byteIndex -= std::exchange(linelength, gtk_text_iter_get_bytes_in_line(&it))) {}

    if (!gtk_text_iter_is_end(&it)) {
        gtk_text_iter_set_line_index(&it, byteIndex);
    }
    // else { // byteIndex was either past-the-end or pointed to the end }

    return it;
}

static auto cloneToCString(GtkTextBuffer* buf) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    return xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&start, &end));
}

/**
 * @brief Clone the buffer's content
 *
 * This is pretty inefficient: the text gets copied twice
 */
static auto cloneToStdString(GtkTextBuffer* buf) -> std::string { return cloneToCString(buf).get(); }

/**
 * @brief Clone the buffer's content and insert a string into the clone
 * This makes one less copy operation over using cloneToStdString followed by insert()
 * (The text is copied "only" twice, and not three times)
 */
static auto cloneWithInsertToStdString(GtkTextBuffer* buf, std::string_view insertedStr) -> std::string {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buf, &start, &end);
    GtkTextIter insertionPoint = getIteratorAtCursor(buf);

    auto firstHalf = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&start, &insertionPoint));
    auto secondHalf = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&insertionPoint, &end));

    std::string_view str1(firstHalf);
    std::string_view str2(secondHalf);

    std::string res;
    res.reserve(str1.length() + str2.length() + insertedStr.length());
    res += str1;
    res += insertedStr;
    res += str2;

    return res;
}

TextEditor::TextEditor(Control* control, const PageRef& page, GtkWidget* xournalWidget, double x, double y):
        control(control),
        page(page),
        xournalWidget(xournalWidget),
        imContext(gtk_im_multicontext_new(), xoj::util::adopt),
        buffer(gtk_text_buffer_new(nullptr), xoj::util::adopt),
        viewPool(std::make_shared<xoj::util::DispatchPool<xoj::view::TextEditionView>>()) {
    // Informs the windowing system of the selection -- i.e. for accessibility purposes
    gtk_text_buffer_add_selection_clipboard(buffer.get(), gtk_clipboard_get(GDK_SELECTION_PRIMARY));

    this->initializeEditionAt(x, y);
    g_signal_connect(this->buffer.get(), "paste-done", G_CALLBACK(bufferPasteDoneCallback), this);
    // xournalpp-calc: keep the formats of parts of the text in place while it is edited
    g_signal_connect_after(this->buffer.get(), "insert-text", G_CALLBACK(bufferInsertCallback), this);
    g_signal_connect(this->buffer.get(), "delete-range", G_CALLBACK(bufferDeleteCallback), this);
    this->trackFormats = true;

    {  // Get cursor blinking settings
        GtkSettings* settings = gtk_widget_get_settings(this->xournalWidget);
        g_object_get(settings, "gtk-cursor-blink", &this->cursorBlink, nullptr);
        if (this->cursorBlink) {
            int tmp = 0;
            g_object_get(settings, "gtk-cursor-blink-time", &tmp, nullptr);
            xoj_assert(tmp >= 0);
            auto cursorBlinkingPeriod = static_cast<unsigned int>(tmp);
            this->cursorBlinkingTimeOn = cursorBlinkingPeriod * CURSOR_ON_MULTIPLIER / CURSOR_DIVIDER;
            this->cursorBlinkingTimeOff = cursorBlinkingPeriod - this->cursorBlinkingTimeOn;
        }
    }

    gtk_im_context_set_client_widget(this->imContext.get(), this->xournalWidget);
    gtk_im_context_focus_in(this->imContext.get());

    g_signal_connect(this->imContext.get(), "commit", G_CALLBACK(iMCommitCallback), this);
    g_signal_connect(this->imContext.get(), "preedit-changed", G_CALLBACK(iMPreeditChangedCallback), this);
    g_signal_connect(this->imContext.get(), "retrieve-surrounding", G_CALLBACK(iMRetrieveSurroundingCallback), this);
    g_signal_connect(this->imContext.get(), "delete-surrounding", G_CALLBACK(imDeleteSurroundingCallback), this);

    if (this->originalTextElement) {
        // If editing a preexisting text, put the cursor at the right location
        this->mousePressed(x, y);
    } else if (this->cursorBlink) {
        blinkCallback(this);
    } else {
        this->cursorVisible = true;
    }

    this->moveIcon = [&]() {
        auto icon = std::make_unique<FlyingClickableIcon>(control->getWindow(), MOVE_ICON_NAME,
                                                          FlyingClickableIcon::Anchor::SOUTH_EAST);

        GtkWidget* w = icon->getWidget();

#if GTK_MAJOR_VERSION == 3
        gtk_widget_add_css_class(gtk_bin_get_child(GTK_BIN(w)), "TL");
        GtkGesture* drag = gtk_gesture_drag_new(w);
#else
        gtk_widget_add_css_class(w, "TL");
        GtkGesture* drag = gtk_gesture_drag_new();
        gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(drag));
#endif

        icon->addSignal(G_OBJECT(drag),
                        g_signal_connect(drag, "drag-update",
                                         G_CALLBACK(+[](GtkGestureDrag*, gdouble offsetX, gdouble offsetY, gpointer p) {
                                             // Warning: because the icon is moved, the parameters offsetX and offsetY
                                             // are NOT relative to the starting point, but rather to the last update's
                                             // position.
                                             auto* self = static_cast<TextEditor*>(p);
                                             if (!self->viewPool->empty()) {
                                                 // We use the first view as the main view
                                                 auto* text = self->getTextElement();
                                                 const auto zoom = self->viewPool->front().getZoom();
                                                 const xoj::util::Point<double> move(offsetX / zoom, offsetY / zoom);
                                                 auto newBox = self->previousBoundingBox;
                                                 newBox.translate(move.x, move.y);
                                                 if (newBox.minX > 0 && newBox.maxX < self->page->getWidth() &&
                                                     newBox.minY > 0 && newBox.maxY < self->page->getHeight()) {
                                                     // The text stays entirely in the page
                                                     text->move(move.x, move.y);
                                                     self->repaintEditor(true);
                                                 }
                                             }
                                         }),
                                         this));
        // Should we implement signals drag-end/cancel here?
        return icon;
    }();
    this->extendIcon = [&]() {
        auto icon = std::make_unique<FlyingClickableIcon>(control->getWindow(), EXTEND_ICON_NAME,
                                                          FlyingClickableIcon::Anchor::SOUTH_WEST);

        GtkWidget* w = icon->getWidget();

#if GTK_MAJOR_VERSION == 3
        gtk_widget_add_css_class(gtk_bin_get_child(GTK_BIN(w)), "TR");
        GtkGesture* drag = gtk_gesture_drag_new(w);
#else
        gtk_widget_add_css_class(w, "TR");
        GtkGesture* drag = gtk_gesture_drag_new();
        gtk_widget_add_controller(w, GTK_EVENT_CONTROLLER(drag));
#endif

        icon->addSignal(G_OBJECT(drag),
                        g_signal_connect(drag, "drag-begin",
                                         G_CALLBACK(+[](GtkGestureDrag*, gdouble startX, gdouble startY, gpointer p) {
                                             auto* self = static_cast<TextEditor*>(p);
                                             if (self->currentWrapWidth == Text::NO_WRAP) {
                                                 self->currentWrapWidth = self->previousBoundingBox.getWidth();
                                             }
                                         }),
                                         this));
        icon->addSignal(G_OBJECT(drag),
                        g_signal_connect(drag, "drag-update",
                                         G_CALLBACK(+[](GtkGestureDrag*, gdouble offsetX, gdouble offsetY, gpointer p) {
                                             // Warning: because the icon is moved, the parameters offsetX and offsetY
                                             // are NOT relative to the starting point, but rather to the last update's
                                             // position.
                                             auto* self = static_cast<TextEditor*>(p);
                                             if (self->viewPool->empty()) {
                                                 // We use the first view as the main view. Return if there is none.
                                                 return;
                                             }
                                             auto o = xoj::util::Point<double>(offsetX, offsetY) /
                                                      self->viewPool->front().getZoom();
                                             auto wrapVector =
                                                     self->getTextElement()->getTransformation().applyToVector(
                                                             {self->currentWrapWidth, 0});
                                             double len = wrapVector.distance({0, 0});
                                             /// signed length of the projection of the offset onto the wrap vector
                                             double ps = (wrapVector.x * o.x + wrapVector.y * o.y) / len;
                                             if (ps <= -len) {
                                                 // This means non-positive wrapping width
                                                 return;
                                             }
                                             double ratio = 1. + ps / len;
                                             auto newWrapPos = wrapVector * ratio + self->getTextElement()->getOrigin();
                                             if (newWrapPos.x < 0 || newWrapPos.x > self->page->getWidth() ||
                                                 newWrapPos.y < 0 || newWrapPos.y > self->page->getHeight()) {
                                                 // The wrapping handle lands out of the page
                                                 return;
                                             }

                                             // The move is valid
                                             self->currentWrapWidth *= ratio;
                                             self->layoutStatus = LayoutStatus::NEEDS_PARAMETERS_UPDATE;
                                             self->repaintEditor(true);
                                         }),
                                         this));
        icon->addSignal(G_OBJECT(drag),
                        g_signal_connect(drag, "drag-end",
                                         G_CALLBACK(+[](GtkGestureDrag*, gdouble offsetX, gdouble offsetY, gpointer p) {
                                             auto* self = static_cast<TextEditor*>(p);
                                             self->textElement->setWrap(self->currentWrapWidth);
                                         }),
                                         this));
        icon->addSignal(G_OBJECT(drag),
                        g_signal_connect(drag, "cancel", G_CALLBACK(+[](GtkGesture*, GdkEventSequence*, gpointer p) {
                                             auto* self = static_cast<TextEditor*>(p);
                                             self->currentWrapWidth = self->textElement->getWrap();
                                             self->layoutStatus = LayoutStatus::NEEDS_PARAMETERS_UPDATE;
                                             self->repaintEditor(true);
                                         }),
                                         this));
        // Move both icons when scrolling/zooming
        auto cb = G_CALLBACK(+[](GtkAdjustment*, gpointer p) { static_cast<TextEditor*>(p)->updateDraggableIcons(); });
        auto* hadj = G_OBJECT(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(xournalWidget)));
        icon->addSignal(hadj, g_signal_connect(hadj, "value-changed", cb, this));
        icon->addSignal(hadj, g_signal_connect(hadj, "changed", cb, this));
        auto* vadj = G_OBJECT(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(xournalWidget)));
        icon->addSignal(vadj, g_signal_connect(vadj, "value-changed", cb, this));
        icon->addSignal(vadj, g_signal_connect(vadj, "changed", cb, this));
        return icon;
    }();
}

TextEditor::~TextEditor() {
    gtk_im_context_focus_out(this->imContext.get());

    this->xournalWidget = nullptr;
    control->setCopyCutEnabled(false);
    control->fireTextFormatChanged();  // the format buttons go insensitive

    this->contentsChanged(true);

    finalizeEdition();
}

auto TextEditor::getViewPool() const -> const std::shared_ptr<xoj::util::DispatchPool<xoj::view::TextEditionView>>& {
    return viewPool;
}

void TextEditor::onViewCreation() const {
    this->updateDraggableIcons();  // The icons are placed on the first view. The view must have been created for that
}

auto TextEditor::getTextElement() const -> Text* { return this->textElement.get(); }

bool TextEditor::bufferEmpty() const { return gtk_text_buffer_get_char_count(this->buffer.get()) == 0; }

void TextEditor::replaceBufferContent(const std::string& text) {
    gtk_text_buffer_set_text(this->buffer.get(), text.c_str(), -1);

    GtkTextIter first = {nullptr};
    gtk_text_buffer_get_iter_at_offset(this->buffer.get(), &first, 0);
    gtk_text_buffer_place_cursor(this->buffer.get(), &first);
    this->layoutStatus = LayoutStatus::NEEDS_COMPLETE_UPDATE;
    this->cursorBox = computeCursorBox();
}

void TextEditor::setColor(Color color) {
    this->textElement->setColor(color);
    repaintEditor(false);
}

void TextEditor::setFont(XojFont font) {
    this->textElement->setFont(font);
    afterFontChange();
}

void TextEditor::setAlignment(TextAlignment al) {
    this->textElement->setAlignment(al);
    this->layoutStatus = LayoutStatus::NEEDS_PARAMETERS_UPDATE;
    repaintEditor(true);  // The size may change if the text overflows
}

void TextEditor::setJustify(bool justify) {
    this->textElement->setJustify(justify);
    this->layoutStatus = LayoutStatus::NEEDS_PARAMETERS_UPDATE;
    repaintEditor(true);
}

void TextEditor::afterFontChange() {
    this->textElement->updatePangoFont(this->layout.get());
    this->computeVirtualCursorPosition();
    this->repaintEditor();
}

namespace {
/// Unicode superscript / subscript of a character, or nullptr if there is none
const char* scriptChar(gunichar c, bool superscript) {
    static const char* const SUP_DIGITS[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};
    static const char* const SUB_DIGITS[] = {"₀", "₁", "₂", "₃", "₄", "₅", "₆", "₇", "₈", "₉"};
    if (c >= '0' && c <= '9') {
        return (superscript ? SUP_DIGITS : SUB_DIGITS)[c - '0'];
    }
    static const char* const SUP_LOWER[] = {"ᵃ", "ᵇ", "ᶜ", "ᵈ",     "ᵉ", "ᶠ", "ᵍ", "ʰ", "ⁱ", "ʲ", "ᵏ", "ˡ", "ᵐ",
                                            "ⁿ", "ᵒ", "ᵖ", nullptr, "ʳ", "ˢ", "ᵗ", "ᵘ", "ᵛ", "ʷ", "ˣ", "ʸ", "ᶻ"};
    static const char* const SUB_LOWER[] = {"ₐ", nullptr, nullptr, nullptr, "ₑ",     nullptr, nullptr, "ₕ",     "ᵢ",
                                            "ⱼ", "ₖ",     "ₗ",     "ₘ",     "ₙ",     "ₒ",     "ₚ",     nullptr, "ᵣ",
                                            "ₛ", "ₜ",     "ᵤ",     "ᵥ",     nullptr, "ₓ",     nullptr, nullptr};
    if (c >= 'a' && c <= 'z') {
        return (superscript ? SUP_LOWER : SUB_LOWER)[c - 'a'];
    }
    switch (c) {
        case '+':
            return superscript ? "⁺" : "₊";
        case '-':
            return superscript ? "⁻" : "₋";
        case '=':
            return superscript ? "⁼" : "₌";
        case '(':
            return superscript ? "⁽" : "₍";
        case ')':
            return superscript ? "⁾" : "₎";
        default:
            return nullptr;
    }
}
}  // namespace

void TextEditor::iMCommitCallback(GtkIMContext* context, const gchar* str, TextEditor* te) {
    gtk_text_buffer_begin_user_action(te->buffer.get());
    std::string inserted;  ///< what was finally typed (str may point to a temporary below)

    bool hadSelection = gtk_text_buffer_get_has_selection(te->buffer.get());
    if (hadSelection) {
        gtk_text_buffer_delete_selection(te->buffer.get(), true, true);
        te->control->setCopyCutEnabled(false);
    }

    if (!strcmp(str, "\n")) {
        if (!gtk_text_buffer_insert_interactive_at_cursor(te->buffer.get(), "\n", 1, true)) {
            gtk_widget_error_bell(te->xournalWidget);
        } else {
            te->contentsChanged(true);
        }
    } else {
        if (!hadSelection && te->cursorOverwrite) {
            auto insert = getIteratorAtCursor(te->buffer.get());
            if (!gtk_text_iter_ends_line(&insert)) {
                te->deleteFromCursor(GTK_DELETE_CHARS, 1);
            }
        }

        // xournalpp-calc: after Ctrl+Up / Ctrl+Down, the character typed goes up (exponent) or down (subscript)
        std::string scripted;
        if (te->pendingScript != 0 && !hadSelection && g_utf8_strlen(str, -1) == 1) {
            const gunichar c = g_utf8_get_char(str);
            const bool sup = te->pendingScript > 0;
            if (te->textElement->isCalc()) {
                // Calculations: "1 m" + Ctrl+Up + "2" → "1 m^2" (shown m²); "A" + Ctrl+Down + "s" → "A_s" (Aₛ).
                // Following digits or letters stay in the exponent / subscript by themselves
                te->pendingScript = 0;
                if (g_unichar_isalnum(c) || (sup && (c == '-' || c == '('))) {
                    scripted = std::string(sup ? "^" : "_") + str;
                    str = scripted.c_str();
                }
            } else if (const char* sc = scriptChar(c, sup)) {
                // Plain text: Unicode superscript / subscript characters, until one has none (e.g. a space)
                str = sc;
            } else {
                te->pendingScript = 0;
            }
        }

        // xournalpp-calc: a tilde (´) or an apostrophe typed right after a Greek letter is a prime: φ´ → φ′ (as in
        // the effective friction angle). The tilde may come together with the next key (dead key + space or letter)
        std::string primed;
        if (!hadSelection && (g_str_has_prefix(str, "´") || g_str_has_prefix(str, "'"))) {
            GtkTextIter end = getIteratorAtCursor(te->buffer.get());
            GtkTextIter start = end;
            if (gtk_text_iter_backward_char(&start)) {
                const gunichar prev = gtk_text_iter_get_char(&start);
                const bool greek = (prev >= 0x0391 && prev <= 0x03A9) || (prev >= 0x03B1 && prev <= 0x03C9) ||
                                   (prev >= 0x03D0 && prev <= 0x03F5);
                if (greek) {
                    const size_t len = g_str_has_prefix(str, "´") ? std::strlen("´") : 1;
                    std::string rest = str + len;
                    // dead key + space gives "´ " or "´": the space only completed the tilde
                    if (rest == " " || rest == "\u00a0") {
                        rest.clear();
                    }
                    primed = "′" + rest;
                    str = primed.c_str();
                }
            }
        }

        // xournalpp-calc: two signs typed in a row become one symbol: +- or -+ → ±, -= or =- → ≤, += or =+ → ≥,
        // ++ → >, -- → <, == → ≔  (not after ":", so that ":=-3" stays a definition of a negative value)
        if (!hadSelection && (!strcmp(str, "+") || !strcmp(str, "-") || !strcmp(str, "="))) {
            GtkTextIter end = getIteratorAtCursor(te->buffer.get());
            GtkTextIter start = end;
            if (gtk_text_iter_backward_char(&start)) {
                const gunichar prev = gtk_text_iter_get_char(&start);
                GtkTextIter before = start;
                const bool afterColon = gtk_text_iter_backward_char(&before) && gtk_text_iter_get_char(&before) == ':';
                const char now = str[0];
                const char* symbol = nullptr;
                auto pair = [&](char a, char b) { return (prev == static_cast<gunichar>(a) && now == b); };
                if (pair('+', '-') || pair('-', '+')) {
                    symbol = "±";
                } else if (pair('-', '=') || (pair('=', '-') && !afterColon)) {
                    symbol = "≤";
                } else if (pair('+', '=') || (pair('=', '+') && !afterColon)) {
                    symbol = "≥";
                } else if (pair('=', '=')) {
                    symbol = "≔";  // definition
                } else if (pair('+', '+')) {
                    symbol = ">";
                } else if (pair('-', '-')) {
                    symbol = "<";
                }
                if (symbol) {
                    gtk_text_buffer_delete(te->buffer.get(), &start, &end);
                    str = symbol;
                }
            }
        }

        // xournalpp-calc: in a calculation region the formula is built as it is typed (as in the equation editor of a
        // word processor): "^" opens the box of the exponent, "/" puts what is before it over a fraction bar and
        // opens the denominator; a space at the end of an exponent leaves it
        std::string afterCursor;
        std::string opened;
        bool skipInsert = false;
        if (te->textElement->isCalc() && !hadSelection && scripted.empty()) {
            const std::string text = cloneToStdString(te->buffer.get());
            const size_t cur = te->cursorByte();
            if (str[0] == '^') {
                opened = std::string("^(") + (str + 1);
                str = opened.c_str();
                afterCursor = ")";
            } else if (!strcmp(str, "/") && !xoj::calc::math::unitDivisionBefore(text, cur)) {
                str = "/(";
                afterCursor = ")";
            } else if (!strcmp(str, "(")) {
                // The parenthesis is closed at once (RAIZ(…), SENO(…)), unless it opens before something written
                const unsigned char next = cur < text.size() ? static_cast<unsigned char>(text[cur]) : '\n';
                if (!(std::isalnum(next) || next == '_' || next == '.' || next == '(' || next >= 0x80)) {
                    afterCursor = ")";
                }
            } else if (!strcmp(str, ")") && cur < text.size() && text[cur] == ')') {
                // Typing the parenthesis that is already there goes over it
                te->placeCursorAtByte(cur + 1);
                skipInsert = true;
            } else if (!strcmp(str, " ")) {
                if (const auto* f = te->getFormula()) {
                    auto slot = f->slotAround(cur);
                    if (slot && slot->exponent && cur == slot->end && slot->start != slot->end) {
                        te->placeCursorAtByte(slot->outerEnd);
                        skipInsert = true;
                    }
                }
            }
        }

        inserted = str;
        if (skipInsert) {
            // nothing to insert
        } else if (!gtk_text_buffer_insert_interactive_at_cursor(te->buffer.get(), str, -1, true)) {
            gtk_widget_error_bell(te->xournalWidget);
        } else if (!afterCursor.empty()) {
            gtk_text_buffer_insert_at_cursor(te->buffer.get(), afterCursor.c_str(), -1);
            GtkTextIter it = getIteratorAtCursor(te->buffer.get());
            gtk_text_iter_backward_chars(&it, static_cast<int>(g_utf8_strlen(afterCursor.c_str(), -1)));
            gtk_text_buffer_place_cursor(te->buffer.get(), &it);
        }
    }

    gtk_text_buffer_end_user_action(te->buffer.get());

    // xournalpp-calc: a definition (== → ≔, or ":=") typed in a plain text turns on the calculation mode: the text
    // becomes a calculation region
    if (!te->textElement->isCalc() && inserted == "=") {
        GtkTextIter end = getIteratorAtCursor(te->buffer.get());
        GtkTextIter start = end;
        if (gtk_text_iter_backward_chars(&start, 2)) {
            auto typed = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&start, &end));
            if (!strcmp(typed.get(), ":=")) {
                inserted = "≔";
            }
        }
    }
    if (!te->textElement->isCalc() && inserted == "≔") {
        te->control->getActionDatabase()->fireChangeActionState(Action::CALC_MODE, true);
    }

    te->contentsChanged();
    te->repaintEditor();
    te->updateCompletion();
}

void TextEditor::setCalc(bool calc) {
    this->textElement->setCalc(calc);
    this->repaintEditor();
}

void TextEditor::insertText(const std::string& str) {
    if (!str.empty()) {
        iMCommitCallback(nullptr, str.c_str(), this);
    }
}


void TextEditor::insertLine(const std::string& line) {
    GtkTextIter cursor = getIteratorAtCursor(this->buffer.get());
    GtkTextIter lineStart = cursor;
    gtk_text_iter_set_line_offset(&lineStart, 0);
    GtkTextIter lineEnd = lineStart;
    if (!gtk_text_iter_ends_line(&lineEnd)) {
        gtk_text_iter_forward_to_line_end(&lineEnd);
    }
    gchar* current = gtk_text_buffer_get_text(this->buffer.get(), &lineStart, &lineEnd, false);
    const bool blank = current == nullptr || g_strstrip(current)[0] == '\0';
    g_free(current);

    gtk_text_buffer_begin_user_action(this->buffer.get());
    if (!blank) {
        gtk_text_buffer_place_cursor(this->buffer.get(), &lineEnd);
        gtk_text_buffer_insert_at_cursor(this->buffer.get(), "\n", -1);
    }
    gtk_text_buffer_insert_at_cursor(this->buffer.get(), line.c_str(), -1);
    gtk_text_buffer_end_user_action(this->buffer.get());
    this->contentsChanged(true);
}

void TextEditor::insertDefinition() { insertText("≔"); }

void TextEditor::insertAround(const std::string& before, const std::string& after) {
    hideCompletion();
    resetImContext();
    gtk_text_buffer_begin_user_action(this->buffer.get());
    gtk_text_buffer_delete_selection(this->buffer.get(), true, true);
    gtk_text_buffer_insert_at_cursor(this->buffer.get(), (before + after).c_str(), -1);
    GtkTextIter it = getIteratorAtCursor(this->buffer.get());
    gtk_text_iter_backward_chars(&it, static_cast<int>(g_utf8_strlen(after.c_str(), -1)));
    gtk_text_buffer_place_cursor(this->buffer.get(), &it);
    gtk_text_buffer_end_user_action(this->buffer.get());
    this->contentsChanged(true);
    this->repaintEditor();
}

const xoj::calc::math::Formula* TextEditor::getFormula() const {
    if (!this->textElement || !this->textElement->isCalc()) {
        return nullptr;
    }
    std::string text = cloneToStdString(this->buffer.get());
    const auto st = this->textElement->calcStyle();
    std::string key =
            text + '\0' + st.fontName + '\0' + std::to_string(st.fontSize) + '\0' + std::to_string(st.lineHeight);
    if (!this->formula || key != this->formulaKey) {
        this->formula = xoj::calc::math::Formula::forEditing(text, st);
        this->formulaKey = std::move(key);
    }
    return this->formula.get();
}

size_t TextEditor::cursorByte() const { return static_cast<size_t>(getByteOffsetOfCursor(this->buffer.get())); }

std::pair<size_t, size_t> TextEditor::selectionBytes() const {
    GtkTextIter start, end;
    if (!gtk_text_buffer_get_selection_bounds(this->buffer.get(), &start, &end)) {
        return {0, 0};
    }
    return {static_cast<size_t>(getByteOffsetOfIterator(start)), static_cast<size_t>(getByteOffsetOfIterator(end))};
}

void TextEditor::placeCursorAtByte(size_t byte, bool extendSelection) {
    GtkTextIter it = getIteratorAtByteOffset(this->buffer.get(), static_cast<int>(byte));
    moveCursorIterator(&it, extendSelection);
    computeVirtualCursorPosition();
}

bool TextEditor::tabInFormula() {
    const auto* f = getFormula();
    if (!f) {
        return false;
    }
    const size_t cur = cursorByte();
    // The outermost structure around the cursor
    std::optional<xoj::calc::math::Slot> outer;
    for (const auto& s: f->slots()) {
        if (s.start <= cur && cur <= s.end &&
            (!outer || s.outerEnd - s.outerStart > outer->outerEnd - outer->outerStart)) {
            outer = s;
        }
    }
    if (!outer) {
        return false;
    }
    hideCompletion();
    for (const auto& s: f->slots()) {
        if (s.start > cur && s.start < outer->outerEnd) {
            placeCursorAtByte(s.end);
            return true;
        }
    }
    placeCursorAtByte(outer->outerEnd);
    return true;
}

void TextEditor::greekifyBeforeCursor() {
    GtkTextIter end = getIteratorAtCursor(this->buffer.get());
    GtkTextIter start = end;
    if (!gtk_text_iter_backward_char(&start)) {
        gtk_widget_error_bell(this->xournalWidget);
        return;
    }
    gunichar c = gtk_text_iter_get_char(&start);
    std::string greek = c < 128 ? xoj::calc::greekForLatin(static_cast<char>(c)) : "";
    if (greek.empty()) {
        gtk_widget_error_bell(this->xournalWidget);
        return;
    }
    gtk_text_buffer_begin_user_action(this->buffer.get());
    gtk_text_buffer_delete(this->buffer.get(), &start, &end);
    gtk_text_buffer_insert_at_cursor(this->buffer.get(), greek.c_str(), -1);
    gtk_text_buffer_end_user_action(this->buffer.get());
    this->contentsChanged();
    this->repaintEditor();
}

void TextEditor::iMPreeditChangedCallback(GtkIMContext* context, TextEditor* te) {
    xoj::util::OwnedCString str;
    gint cursor_pos = 0;
    GtkTextIter iter = getIteratorAtCursor(te->buffer.get());

    {
        PangoAttrList* attrs = nullptr;
        gtk_im_context_get_preedit_string(context, str.contentReplacer(), &attrs, &cursor_pos);
        if (attrs == nullptr) {
            attrs = pango_attr_list_new();
        }
        te->preeditAttrList.reset(attrs, xoj::util::adopt);
    }

    if (str && str[0] && !gtk_text_iter_can_insert(&iter, true)) {
        /*
         * Keypress events are passed to input method even if cursor position is
         * not editable; so beep here if it's multi-key input sequence, input
         * method will be reset in key-press-event handler.
         */
        gtk_widget_error_bell(te->xournalWidget);
        return;
    }

    te->preeditString = std::move(str);
    te->preeditCursor = cursor_pos;
    te->contentsChanged();
    te->repaintEditor();
}

auto TextEditor::iMRetrieveSurroundingCallback(GtkIMContext* context, TextEditor* te) -> bool {
    GtkTextIter start = getIteratorAtCursor(te->buffer.get());
    GtkTextIter end = start;

    gint pos = gtk_text_iter_get_line_index(&start);
    gtk_text_iter_set_line_offset(&start, 0);
    gtk_text_iter_forward_to_line_end(&end);

    auto text = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_slice(&start, &end));
    gtk_im_context_set_surrounding(context, text.get(), -1, pos);

    return true;
}

auto TextEditor::imDeleteSurroundingCallback(GtkIMContext* context, gint offset, gint n_chars, TextEditor* te) -> bool {
    GtkTextIter start = getIteratorAtCursor(te->buffer.get());
    GtkTextIter end = start;

    gtk_text_iter_forward_chars(&start, offset);
    gtk_text_iter_forward_chars(&end, offset + n_chars);

    gtk_text_buffer_delete_interactive(te->buffer.get(), &start, &end, true);

    te->contentsChanged();
    te->repaintEditor();

    return true;
}

auto TextEditor::onKeyPressEvent(const KeyEvent& event) -> bool {
    if (handleCompletionKey(event)) {
        return true;
    }
    // xournalpp-calc: Ctrl+= inserts ≔. On Spanish keyboards "=" is Shift+0, and with Ctrl held some systems report
    // the key as "0" or ")", so all of them are accepted (Ctrl+Shift+0)
    if ((event.state & GDK_CONTROL_MASK) && !(event.state & GDK_MOD1_MASK)) {
        const guint k = event.keyval;
        if (k == GDK_KEY_equal || k == GDK_KEY_KP_Equal || k == GDK_KEY_0 || k == GDK_KEY_parenright) {
            insertDefinition();
            return true;
        }
        // xournalpp-calc: Ctrl+R puts a square root with the cursor in it
        if ((k == GDK_KEY_r || k == GDK_KEY_R) && !(event.state & GDK_SHIFT_MASK) && this->textElement->isCalc()) {
            insertAround("RAIZ(", ")");
            return true;
        }
    }
    // Keys that are not text (arrows, Enter, Backspace…) cancel a pending exponent / subscript
    if (this->pendingScript != 0 && gdk_keyval_to_unicode(event.keyval) == 0 &&
        !(event.keyval >= GDK_KEY_Shift_L && event.keyval <= GDK_KEY_Hyper_R) &&
        !(event.keyval == GDK_KEY_ISO_Level3_Shift) &&
        !((event.state & GDK_CONTROL_MASK) && (event.keyval == GDK_KEY_Up || event.keyval == GDK_KEY_Down ||
                                               event.keyval == GDK_KEY_KP_Up || event.keyval == GDK_KEY_KP_Down))) {
        this->pendingScript = 0;
    }

    // IME needs to handle the input first so the candidate window works correctly
    if (gtk_im_context_filter_keypress(this->imContext.get(), event.sourceEvent)) {
        this->needImReset = true;

        GtkTextIter iter = getIteratorAtCursor(this->buffer.get());
        bool canInsert = gtk_text_iter_can_insert(&iter, true);

        if (canInsert) {
            control->getCursor()->setInvisible(true);
        } else {
            this->resetImContext();
        }
        return true;
    }

    if (handleGridNavigation(event)) {
        return true;
    }

    return keyBindings.processEvent(this, event);
}

std::string TextEditor::wordBeforeCursor(GtkTextIter* startOut) const {
    if (!this->textElement->isCalc() || gtk_text_buffer_get_has_selection(this->buffer.get()) ||
        (this->preeditString && *this->preeditString.get())) {
        return {};
    }
    GtkTextIter cursor = getIteratorAtCursor(this->buffer.get());
    // Not in the middle of a name
    const gunichar after = gtk_text_iter_get_char(&cursor);
    if (g_unichar_isalnum(after) || after == '_') {
        return {};
    }
    GtkTextIter start = cursor;
    int length = 0;
    while (length < 24) {
        GtkTextIter prev = start;
        if (!gtk_text_iter_backward_char(&prev)) {
            break;
        }
        const gunichar c = gtk_text_iter_get_char(&prev);
        if (!(g_unichar_isalnum(c) || c == '.' || c == '_')) {
            break;
        }
        start = prev;
        length++;
    }
    if (length == 0) {
        return {};
    }
    const gunichar first = gtk_text_iter_get_char(&start);
    auto word = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&start, &cursor));
    std::string w = word.get();
    if (!g_unichar_isalpha(first) || w.find('_') != std::string::npos) {
        return {};
    }
    // A unit after a number ("2 m", "(a + b) kN") is not a function
    GtkTextIter before = start;
    while (gtk_text_iter_backward_char(&before)) {
        const gunichar c = gtk_text_iter_get_char(&before);
        if (c == ' ' || c == '\t') {
            continue;
        }
        if (g_unichar_isdigit(c) || c == '.' || c == ')' || c == ']') {
            return {};
        }
        break;
    }
    // Not inside quotes (a cell reference or a file name)
    GtkTextIter lineStart = start;
    gtk_text_iter_set_line_offset(&lineStart, 0);
    auto line = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&lineStart, &start));
    std::string_view l = line.get();
    if (std::count(l.begin(), l.end(), '"') % 2 == 1) {
        return {};
    }
    if (startOut) {
        *startOut = start;
    }
    return w;
}

void TextEditor::updateCompletion() {
    GtkTextIter start;
    const std::string word = wordBeforeCursor(&start);
    auto functions =
            word.empty() ? std::vector<const xoj::calc::FunctionInfo*>{} : xoj::calc::functionsStartingWith(word);
    if (functions.empty() || viewPool->empty()) {
        hideCompletion();
        return;
    }
    if (!this->completion) {
        this->completion = std::make_unique<FunctionCompletion>(
                control->getWindow(), [this](const xoj::calc::FunctionInfo& f) { acceptCompletion(f.name); });
    }
    // With one letter nothing is chosen by Tab or Enter (the arrows choose): "L" is often a variable, and Tab and
    // Enter go to the next box. Neither when what is typed is a variable of the document
    if (!this->documentVariableNames) {
        std::set<std::string> names;
        auto addDefinitions = [&](std::string_view text) {
            size_t pos = 0;
            while (pos <= text.size()) {
                size_t end = text.find('\n', pos);
                std::string_view lineText =
                        text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
                size_t def = std::min(lineText.find(":="), lineText.find("≔"));
                if (def != std::string_view::npos) {
                    std::string_view name = lineText.substr(0, def);
                    while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) {
                        name.remove_prefix(1);
                    }
                    while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) {
                        name.remove_suffix(1);
                    }
                    names.emplace(name);
                }
                if (end == std::string_view::npos) {
                    break;
                }
                pos = end + 1;
            }
        };
        for (const auto& src: xoj::calc::documentSources(this->control->getDocument())) {
            addDefinitions(src);
        }
        this->documentVariableNames = std::move(names);
    }
    std::string current = cloneToStdString(this->buffer.get());
    const bool typedVariable =
            this->documentVariableNames->count(word) > 0 || current.find(word + " :=") != std::string::npos ||
            current.find(word + ":=") != std::string::npos || current.find(word + " ≔") != std::string::npos ||
            current.find(word + "≔") != std::string::npos;
    const bool highlight = g_utf8_strlen(word.c_str(), -1) >= 2 && !typedVariable;

    // Below the cursor
    auto box = viewPool->front().toWidgetCoordinates(textElement->getTransformation() *
                                                     xoj::util::Rectangle<double>(this->cursorBox));
    this->completion->show(functions, {round_cast<int>(box.x), round_cast<int>(box.y + box.height) + 4}, highlight);
}

void TextEditor::hideCompletion() {
    if (this->completion) {
        this->completion->hide();
    }
}

bool TextEditor::handleCompletionKey(const KeyEvent& event) {
    if (!this->completion || !this->completion->isVisible()) {
        return false;
    }
    constexpr auto MODIFIERS = GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK;
    if ((event.state & MODIFIERS) != 0) {
        return false;
    }
    switch (event.keyval) {
        case GDK_KEY_Escape:
            hideCompletion();
            return true;
        case GDK_KEY_Up:
        case GDK_KEY_KP_Up:
            this->completion->moveHighlight(-1);
            return true;
        case GDK_KEY_Down:
        case GDK_KEY_KP_Down:
            this->completion->moveHighlight(1);
            return true;
        case GDK_KEY_Tab:
        case GDK_KEY_KP_Tab:
        case GDK_KEY_Return:
        case GDK_KEY_ISO_Enter:
        case GDK_KEY_KP_Enter:
            if (const auto* f = this->completion->highlighted()) {
                acceptCompletion(f->name);
                return true;
            }
            hideCompletion();
            return false;
        default:
            return false;
    }
}

void TextEditor::acceptCompletion(const std::string& functionName) {
    hideCompletion();
    GtkTextIter start;
    if (wordBeforeCursor(&start).empty()) {
        return;
    }
    GtkTextIter end = getIteratorAtCursor(this->buffer.get());
    gtk_text_buffer_begin_user_action(this->buffer.get());
    gtk_text_buffer_delete(this->buffer.get(), &start, &end);
    std::string text = functionName + "()";
    gtk_text_buffer_insert_at_cursor(this->buffer.get(), text.c_str(), -1);
    GtkTextIter inside = getIteratorAtCursor(this->buffer.get());
    gtk_text_iter_backward_char(&inside);  // the cursor goes between the parentheses
    gtk_text_buffer_place_cursor(this->buffer.get(), &inside);
    gtk_text_buffer_end_user_action(this->buffer.get());
    this->contentsChanged(true);
    this->repaintEditor();
}

bool TextEditor::handleGridNavigation(const KeyEvent& event) {
    if (this->preeditString && *this->preeditString.get()) {
        return false;
    }
    constexpr auto MODIFIERS = GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SUPER_MASK;
    if ((event.state & MODIFIERS) != 0) {
        return false;
    }
    if ((event.keyval == GDK_KEY_Tab || event.keyval == GDK_KEY_KP_Tab) && tabInFormula()) {
        return true;
    }
    const auto* formula = getFormula();
    if (!this->control->isCalcGrid()) {
        // Without the grid, the right arrow at the end of the text still leaves the box (and shows its results)
        const bool right = event.keyval == GDK_KEY_Right || event.keyval == GDK_KEY_KP_Right;
        GtkTextIter cursor = getIteratorAtCursor(this->buffer.get());
        const bool atEnd = formula ? formula->step(cursorByte(), 1) == cursorByte() : gtk_text_iter_is_end(&cursor);
        if (right && atEnd && !gtk_text_buffer_get_has_selection(this->buffer.get())) {
            this->control->calcLeaveTextBox();
            return true;
        }
        return false;
    }
    const guint key = event.keyval;
    const bool enter = key == GDK_KEY_Return || key == GDK_KEY_ISO_Enter || key == GDK_KEY_KP_Enter;
    const bool tab = key == GDK_KEY_Tab || key == GDK_KEY_KP_Tab;
    const bool left = key == GDK_KEY_Left || key == GDK_KEY_KP_Left;
    const bool right = key == GDK_KEY_Right || key == GDK_KEY_KP_Right;
    const bool up = key == GDK_KEY_Up || key == GDK_KEY_KP_Up;
    const bool down = key == GDK_KEY_Down || key == GDK_KEY_KP_Down;
    if (!(enter || tab || left || right || up || down)) {
        return false;
    }

    GtkTextBuffer* buf = this->buffer.get();
    const bool empty = gtk_text_buffer_get_char_count(buf) == 0;
    GtkTextIter cursor = getIteratorAtCursor(buf);
    const int nLines = gtk_text_buffer_get_line_count(buf);
    const int cursorLine = gtk_text_iter_get_line(&cursor);

    // Arrows inside a box with text move the cursor; they leave the box only from its edges
    if (formula && !empty) {
        const size_t cur = cursorByte();
        if ((left && formula->step(cur, -1) != cur) || (right && formula->step(cur, 1) != cur) ||
            (up && formula->vertical(cur, -1)) || (down && formula->vertical(cur, 1))) {
            return false;
        }
    } else if (!empty && ((left && !gtk_text_iter_is_start(&cursor)) || (right && !gtk_text_iter_is_end(&cursor)) ||
                          (up && cursorLine > 0) || (down && cursorLine < nLines - 1))) {
        return false;
    }

    auto grid =
            xoj::calc::alignmentGrid(this->page->getBackgroundType(), this->page->getWidth(), this->page->getHeight());
    Text* t = this->textElement.get();
    const double lh = t->getLineHeight() > 0 ? t->getLineHeight() : grid.pitchFor(t->naturalLineHeight());
    const auto& origin = t->getOrigin();
    const double firstLine = grid.snapRow(origin.y + t->firstBaseline() + xoj::calc::BASELINE_LIFT);
    double lastLine = firstLine + (nLines - 1) * lh;
    if (formula) {
        // Lines with fractions are taller
        lastLine = grid.snapRow(origin.y + formula->lastBaseline() + xoj::calc::BASELINE_LIFT);
    }
    const double x = grid.snapX(origin.x);
    // Leave one empty square of the grid after the box
    auto ahead = [&]() {
        double maxX = empty ? origin.x : this->previousBoundingBox.maxX;
        return grid.x0 + (std::ceil((maxX - grid.x0) / grid.dx - 0.05) + 1) * grid.dx;
    };

    double nx = x, line = firstLine;
    if (tab) {
        line = lastLine + lh;
        // Below the denominators of the last line
        while (formula && lh > 0 && line - 0.7 * lh < origin.y + formula->bottom()) {
            line += lh;
        }
    } else if (enter || right) {
        // The next box starts one square to the right (past the right edge: on the next line)
        nx = empty ? x + grid.dx : ahead();
        line = empty ? firstLine : lastLine;
    } else if (left) {
        nx = x - grid.dx;
    } else if (up) {  // the arrows move one row of dots
        line = firstLine - grid.dy;
    } else {  // down
        line = lastLine + grid.dy;
        while (formula && grid.dy > 0 && line - 0.7 * lh < origin.y + formula->bottom()) {
            line += grid.dy;
        }
    }
    if ((enter || right) && !empty) {
        // Start after the box as it will be displayed (with the results, for a calculation region)
        this->control->calcGridGoTo(this->page, nx, line, origin.x + 1, firstLine - xoj::calc::BASELINE_LIFT - 2);
    } else {
        this->control->calcGridGoTo(this->page, nx, line);
    }
    return true;
}

auto TextEditor::onKeyReleaseEvent(const KeyEvent& event) -> bool {
    GtkTextIter iter = getIteratorAtCursor(this->buffer.get());

    if (gtk_text_iter_can_insert(&iter, true) &&
        gtk_im_context_filter_keypress(this->imContext.get(), event.sourceEvent)) {
        this->needImReset = true;
        return true;
    }
    return false;
}

void TextEditor::toggleOverwrite() {
    this->cursorOverwrite = !this->cursorOverwrite;
    repaintCursorAfterChange();
}

/**
 * I know it's a bit rough and duplicated
 * Improve that later on...
 */
void TextEditor::decreaseFontSize() {
    XojFont& font = textElement->getFont();
    if (double size = font.getSize(); size > 1) {
        font.setSize(font.getSize() - 1);
        afterFontChange();
    }
}

void TextEditor::increaseFontSize() {
    XojFont& font = textElement->getFont();
    font.setSize(font.getSize() + 1);
    afterFontChange();
}

void TextEditor::toggleBoldFace() {
    // xournalpp-calc: bold for the selection (or what is typed next), as in a word processor
    toggleFormat(xoj::text::FormatKind::BOLD, "1");
}

void TextEditor::selectAtCursor(TextEditor::SelectType ty) {
    GtkTextIter startPos;
    GtkTextIter endPos;
    gtk_text_buffer_get_selection_bounds(this->buffer.get(), &startPos, &endPos);
    const auto searchFlag = GTK_TEXT_SEARCH_TEXT_ONLY;  // To be used to find double newlines

    switch (ty) {
        case TextEditor::SelectType::WORD: {
            auto currentPos = getIteratorAtCursor(this->buffer.get());
            if (!gtk_text_iter_inside_word(&currentPos)) {
                // Do nothing if cursor is over whitespace
                return;
            }

            if (!gtk_text_iter_starts_word(&currentPos)) {
                gtk_text_iter_backward_word_start(&startPos);
            }
            if (!gtk_text_iter_ends_word(&currentPos)) {
                gtk_text_iter_forward_word_end(&endPos);
            }
            break;
        }
        case TextEditor::SelectType::PARAGRAPH:
            // Note that a GTK "paragraph" is a line, so there's no nice one-liner.
            // We define a paragraph as text separated by double newlines.
            while (!gtk_text_iter_is_start(&startPos)) {
                // There's no GTK function to go to line start, so do it manually.
                while (!gtk_text_iter_starts_line(&startPos)) {
                    if (!gtk_text_iter_backward_word_start(&startPos)) {
                        break;
                    }
                }
                // Check for paragraph start
                GtkTextIter searchPos = startPos;
                gtk_text_iter_backward_chars(&searchPos, 2);
                if (gtk_text_iter_backward_search(&startPos, "\n\n", searchFlag, nullptr, nullptr, &searchPos)) {
                    break;
                }
                gtk_text_iter_backward_line(&startPos);
            }
            while (!gtk_text_iter_ends_line(&endPos)) {
                gtk_text_iter_forward_to_line_end(&endPos);
                // Check for paragraph end
                GtkTextIter searchPos = endPos;
                gtk_text_iter_forward_chars(&searchPos, 2);
                if (gtk_text_iter_forward_search(&endPos, "\n\n", searchFlag, nullptr, nullptr, &searchPos)) {
                    break;
                }
                gtk_text_iter_forward_line(&endPos);
            }
            break;
        case TextEditor::SelectType::ALL:
            gtk_text_buffer_get_bounds(this->buffer.get(), &startPos, &endPos);
            break;
    }

    gtk_text_buffer_select_range(this->buffer.get(), &startPos, &endPos);

    control->setCopyCutEnabled(gtk_text_buffer_get_has_selection(this->buffer.get()));

    // Selection highlighting is handled through Pango attributes
    this->layoutStatus = LayoutStatus::NEEDS_ATTRIBUTES_UPDATE;
    this->repaintEditor(false);
}

void TextEditor::moveCursor(GtkMovementStep step, int count, bool extendSelection) {
    hideCompletion();
    resetImContext();

    GtkTextIter insert = getIteratorAtCursor(this->buffer.get());
    GtkTextIter newplace = insert;

    bool updateVirtualCursor = true;

    // xournalpp-calc: in a formula the cursor goes through its boxes and skips the hidden parentheses
    const auto* formula = getFormula();
    if (formula && (step == GTK_MOVEMENT_LOGICAL_POSITIONS || step == GTK_MOVEMENT_VISUAL_POSITIONS ||
                    step == GTK_MOVEMENT_DISPLAY_LINES)) {
        size_t p = cursorByte();
        const int dir = count < 0 ? -1 : 1;
        for (int k = 0; k < std::abs(count); k++) {
            if (step == GTK_MOVEMENT_DISPLAY_LINES) {
                if (auto v = formula->vertical(p, dir)) {
                    p = *v;
                }
            } else {
                p = formula->step(p, dir);
            }
        }
        newplace = getIteratorAtByteOffset(this->buffer.get(), static_cast<int>(p));
        step = GTK_MOVEMENT_BUFFER_ENDS;
        count = 0;  // nothing more below
    }

    switch (step) {
        case GTK_MOVEMENT_LOGICAL_POSITIONS:  // not used!?
            gtk_text_iter_forward_visible_cursor_positions(&newplace, count);
            break;
        case GTK_MOVEMENT_VISUAL_POSITIONS:
            if (count < 0) {
                gtk_text_iter_backward_cursor_position(&newplace);
            } else {
                gtk_text_iter_forward_cursor_position(&newplace);
            }
            break;

        case GTK_MOVEMENT_WORDS:
            if (count < 0) {
                gtk_text_iter_backward_visible_word_starts(&newplace, -count);
            } else if (count > 0) {
                if (!gtk_text_iter_forward_visible_word_ends(&newplace, count)) {
                    gtk_text_iter_forward_to_line_end(&newplace);
                }
            }
            break;

        case GTK_MOVEMENT_DISPLAY_LINES:
            updateVirtualCursor = false;
            jumpALine(&newplace, count);
            break;

        case GTK_MOVEMENT_PARAGRAPHS:
            if (count > 0) {
                if (!gtk_text_iter_ends_line(&newplace)) {
                    gtk_text_iter_forward_to_line_end(&newplace);
                    --count;
                }
                gtk_text_iter_forward_visible_lines(&newplace, count);
                gtk_text_iter_forward_to_line_end(&newplace);
            } else if (count < 0) {
                if (gtk_text_iter_get_line_offset(&newplace) > 0) {
                    gtk_text_iter_set_line_offset(&newplace, 0);
                }
                gtk_text_iter_forward_visible_lines(&newplace, count);
                gtk_text_iter_set_line_offset(&newplace, 0);
            }
            break;

        case GTK_MOVEMENT_DISPLAY_LINE_ENDS:
        case GTK_MOVEMENT_PARAGRAPH_ENDS:
            if (count > 0) {
                if (!gtk_text_iter_ends_line(&newplace)) {
                    gtk_text_iter_forward_to_line_end(&newplace);
                }
            } else if (count < 0) {
                gtk_text_iter_set_line_offset(&newplace, 0);
            }
            break;

        case GTK_MOVEMENT_BUFFER_ENDS:
            if (count > 0) {
                gtk_text_buffer_get_end_iter(this->buffer.get(), &newplace);
            } else if (count < 0) {
                gtk_text_buffer_get_iter_at_offset(this->buffer.get(), &newplace, 0);
            }
            break;

        default:
            break;
    }

    if (formula) {
        newplace = getIteratorAtByteOffset(
                this->buffer.get(),
                static_cast<int>(formula->nearestStop(static_cast<size_t>(getByteOffsetOfIterator(newplace)))));
    }

    // call moveCursorIterator() even if the cursor hasn't moved, since it cancels the selection
    moveCursorIterator(&newplace, extendSelection);

    if (updateVirtualCursor) {
        computeVirtualCursorPosition();
    }

    if (gtk_text_iter_equal(&insert, &newplace)) {
        gtk_widget_error_bell(this->xournalWidget);
    }
}

void TextEditor::findPos(GtkTextIter* iter, double xPos, double yPos) const {
    if (const auto* f = getFormula()) {
        *iter = getIteratorAtByteOffset(this->buffer.get(), static_cast<int>(f->hit(xPos, yPos)));
        return;
    }
    int index = 0;
    int trailing = 0;
    pango_layout_xy_to_index(this->getUpToDateLayout(), round_cast<int>(xPos * PANGO_SCALE),
                             round_cast<int>(yPos * PANGO_SCALE), &index, &trailing);
    /*
     * trailing is non-zero iff the abscissa is past the middle of the grapheme.
     * In this case, it contains the length of the grapheme in utf8 char count.
     * This way, we put the cursor after the grapheme when clicking past the middle of the grapheme.
     */
    *iter = getIteratorAtByteOffset(this->buffer.get(), index);
    gtk_text_iter_forward_chars(iter, trailing);
}

void TextEditor::updateTextElementContent() { this->textElement->setText(cloneToStdString(this->buffer.get())); }

void TextEditor::contentsChanged(bool forceCreateUndoAction) {
    // Todo: Reinstate text edition undo stack
    this->layoutStatus = LayoutStatus::NEEDS_COMPLETE_UPDATE;
    this->computeVirtualCursorPosition();
}

void TextEditor::markPos(double x, double y, bool extendSelection) {
    GtkTextIter newplace = getIteratorAtCursor(this->buffer.get());

    findPos(&newplace, x, y);

    // call moveCursorIterator() even if the cursor hasn't moved, since it cancels the selection
    moveCursorIterator(&newplace, extendSelection);
    computeVirtualCursorPosition();
}

void TextEditor::mousePressed(double x, double y) {
    hideCompletion();
    this->mouseDown = true;
    // Todo select if SHIFT is pressed
    auto p = this->textElement->getTransformation().inverse() * xoj::util::Point<double>(x, y);
    this->markPos(p.x, p.y, false);
}

void TextEditor::mouseMoved(double x, double y) {
    if (this->mouseDown) {
        auto p = this->textElement->getTransformation().inverse() * xoj::util::Point<double>(x, y);
        this->markPos(p.x, p.y, true);
    }
}

void TextEditor::mouseReleased() { this->mouseDown = false; }

bool TextEditor::isEventInEditor(double x, double y) const {
    return this->boxes.effectiveBounds.contains(this->textElement->getTransformation().inverse() *
                                                xoj::util::Point<double>(x, y));
}

void TextEditor::jumpALine(GtkTextIter* textIter, int count) {
    count += this->virtualCursorPosition.pangoLineNumber;
    if (count < 0) {
        return;
    }

    PangoLayoutLine* line = pango_layout_get_line_readonly(this->layout.get(), count);
    if (line == nullptr) {
        return;
    }
    this->virtualCursorPosition.pangoLineNumber = count;

    int index = 0;
    int trailing = 0;
    pango_layout_line_x_to_index(line, this->virtualCursorPosition.abscissa, &index, &trailing);
    /*
     * trailing is non-zero iff the abscissa is past the middle of the grapheme.
     * In this case, it contains the length of the grapheme in utf8 char count.
     */
    *textIter = getIteratorAtByteOffset(this->buffer.get(), index);
    gtk_text_iter_forward_chars(textIter, trailing);
}

void TextEditor::computeVirtualCursorPosition() {
    int offset = getByteOffsetOfCursor(this->buffer.get());

    pango_layout_index_to_line_x(this->getUpToDateLayout(), offset, 0, &this->virtualCursorPosition.pangoLineNumber,
                                 &this->virtualCursorPosition.abscissa);
}

void TextEditor::moveCursorIterator(const GtkTextIter* newLocation, gboolean extendSelection) {
    bool selectionChanged = true;
    if (extendSelection) {
        if (auto oldLoc = getIteratorAtCursor(this->buffer.get()); gtk_text_iter_equal(newLocation, &oldLoc)) {
            // Nothing changed
            return;
        }
        gtk_text_buffer_move_mark_by_name(this->buffer.get(), "insert", newLocation);
        control->setCopyCutEnabled(gtk_text_buffer_get_has_selection(this->buffer.get()));
    } else {
        // if !extendSelection, we clear the selection even if the cursor does not move
        selectionChanged = gtk_text_buffer_get_has_selection(this->buffer.get());
        gtk_text_buffer_place_cursor(this->buffer.get(), newLocation);
        control->setCopyCutEnabled(false);
    }

    if (this->cursorBlink) {
        // Whenever the cursor moves, the blinking cycle restarts from the start (i.e. the cursor is first shown).
        this->cursorVisible = false;  // Will be toggled to true by BlinkTimer::callback before the repaint
        blinkCallback(this);
    }

    if (selectionChanged) {
        // The selection background color is set through Pango attributes
        this->layoutStatus = LayoutStatus::NEEDS_ATTRIBUTES_UPDATE;
        // Repaint the entire box. Computing the exact area that was (un)selected would be better but complicated
        this->repaintEditor(false);
    } else {
        repaintCursorAfterChange();
    }
}

void TextEditor::updateCursorBox() {
    this->cursorBox = computeCursorBox();

    if (!viewPool->empty()) {
        // Inform the IM of the cursor location (for word selection popup's location)
        // We use the first view as the main view, as far as the IM is concerned
        auto box = viewPool->front().toWidgetCoordinates(textElement->getTransformation() *
                                                         xoj::util::Rectangle<double>(this->cursorBox));

        GdkRectangle cursorRect;  // cursor position in window coordinates
        cursorRect.x = static_cast<int>(box.x);
        cursorRect.y = static_cast<int>(box.y);
        cursorRect.height = static_cast<int>(box.height);
        cursorRect.width = static_cast<int>(box.width);
        gtk_im_context_set_cursor_location(this->imContext.get(), &cursorRect);
    }
}

void TextEditor::updateDraggableIcons() const {
    if (!viewPool->empty()) {
        // We use the first view as the main view
        auto moveIconPos = viewPool->front().toWidgetCoordinates(textElement->getTransformation().shift);
        auto extendIconPos = viewPool->front().toWidgetCoordinates(
                textElement->getTransformation() * xoj::util::Point<double>(boxes.theoreticalSize.width, 0));

        moveIcon->setPosition({round_cast<int>(moveIconPos.x), round_cast<int>(moveIconPos.y)});
        extendIcon->setPosition({round_cast<int>(extendIconPos.x), round_cast<int>(extendIconPos.y)});
    }
}

static auto whitespace(gunichar ch, gpointer user_data) -> gboolean { return (ch == ' ' || ch == '\t'); }

static auto not_whitespace(gunichar ch, gpointer user_data) -> gboolean { return !whitespace(ch, user_data); }

static auto find_whitepace_region(const GtkTextIter* center, GtkTextIter* start, GtkTextIter* end) -> gboolean {
    *start = *center;
    *end = *center;

    if (gtk_text_iter_backward_find_char(start, not_whitespace, nullptr, nullptr)) {
        gtk_text_iter_forward_char(start); /* we want the first whitespace... */
    }
    if (whitespace(gtk_text_iter_get_char(end), nullptr)) {
        gtk_text_iter_forward_find_char(end, not_whitespace, nullptr, nullptr);
    }

    return !gtk_text_iter_equal(start, end);
}

void TextEditor::deleteFromCursor(GtkDeleteType type, int count) {
    hideCompletion();

    this->resetImContext();

    if (type == GTK_DELETE_CHARS) {
        // Char delete deletes the selection, if one exists
        if (gtk_text_buffer_delete_selection(this->buffer.get(), true, true)) {
            control->setCopyCutEnabled(false);
            this->contentsChanged(true);
            this->repaintEditor();
            return;
        }
    }

    if (const auto* f = getFormula(); f && type == GTK_DELETE_CHARS && count > 0) {
        // The hidden parentheses of a formula are not deleted: the cursor goes into the next box
        const size_t cur = cursorByte();
        if (auto name = f->functionNameAt(cur, 1)) {
            GtkTextIter ia = getIteratorAtByteOffset(this->buffer.get(), static_cast<int>(name->first));
            GtkTextIter ib = getIteratorAtByteOffset(this->buffer.get(), static_cast<int>(name->second));
            gtk_text_buffer_begin_user_action(this->buffer.get());
            gtk_text_buffer_delete(this->buffer.get(), &ia, &ib);
            gtk_text_buffer_end_user_action(this->buffer.get());
            this->contentsChanged(true);
            this->repaintEditor();
            return;
        }
        if (cur < cloneToStdString(this->buffer.get()).size() && !f->visibleByte(cur) && f->step(cur, 1) != cur) {
            placeCursorAtByte(f->step(cur, 1));
            return;
        }
    }

    GtkTextIter insert = getIteratorAtCursor(this->buffer.get());

    GtkTextIter start = insert;
    GtkTextIter end = insert;

    switch (type) {
        case GTK_DELETE_CHARS:
            gtk_text_iter_forward_cursor_positions(&end, count);
            break;

        case GTK_DELETE_WORD_ENDS:
            if (count > 0) {
                gtk_text_iter_forward_word_ends(&end, count);
            } else if (count < 0) {
                gtk_text_iter_backward_word_starts(&start, 0 - count);
            }
            break;

        case GTK_DELETE_WORDS:
            break;

        case GTK_DELETE_DISPLAY_LINE_ENDS:
            break;

        case GTK_DELETE_DISPLAY_LINES:
            break;

        case GTK_DELETE_PARAGRAPH_ENDS:
            if (count > 0) {
                /* If we're already at a newline, we need to
                 * simply delete that newline, instead of
                 * moving to the next one.
                 */
                if (gtk_text_iter_ends_line(&end)) {
                    gtk_text_iter_forward_line(&end);
                    --count;
                }

                while (count > 0) {
                    if (!gtk_text_iter_forward_to_line_end(&end)) {
                        break;
                    }

                    --count;
                }
            } else if (count < 0) {
                if (gtk_text_iter_starts_line(&start)) {
                    gtk_text_iter_backward_line(&start);
                    if (!gtk_text_iter_ends_line(&end)) {
                        gtk_text_iter_forward_to_line_end(&start);
                    }
                } else {
                    gtk_text_iter_set_line_offset(&start, 0);
                }
                ++count;

                gtk_text_iter_backward_lines(&start, -count);
            }
            break;

        case GTK_DELETE_PARAGRAPHS:
            if (count > 0) {
                gtk_text_iter_set_line_offset(&start, 0);
                gtk_text_iter_forward_to_line_end(&end);

                /* Do the lines beyond the first. */
                while (count > 1) {
                    gtk_text_iter_forward_to_line_end(&end);
                    --count;
                }
            }

            break;

        case GTK_DELETE_WHITESPACE: {
            find_whitepace_region(&insert, &start, &end);
        } break;

        default:
            break;
    }

    if (!gtk_text_iter_equal(&start, &end)) {
        gtk_text_buffer_begin_user_action(this->buffer.get());

        if (!gtk_text_buffer_delete_interactive(this->buffer.get(), &start, &end, true)) {
            gtk_widget_error_bell(this->xournalWidget);
        }

        gtk_text_buffer_end_user_action(this->buffer.get());
    } else {
        gtk_widget_error_bell(this->xournalWidget);
    }

    this->contentsChanged();
    this->repaintEditor();
}

void TextEditor::backspace() {

    resetImContext();

    // Backspace deletes the selection, if one exists
    if (gtk_text_buffer_delete_selection(this->buffer.get(), true, true)) {
        control->setCopyCutEnabled(false);
        this->contentsChanged();
        this->repaintEditor();
        return;
    }

    if (const auto* f = getFormula(); f && f->functionNameAt(cursorByte(), -1)) {
        // xournalpp-calc: the name of a function (shown sen for SENO) is deleted at once
        auto [a, b] = *f->functionNameAt(cursorByte(), -1);
        GtkTextIter ia = getIteratorAtByteOffset(this->buffer.get(), static_cast<int>(a));
        GtkTextIter ib = getIteratorAtByteOffset(this->buffer.get(), static_cast<int>(b));
        gtk_text_buffer_begin_user_action(this->buffer.get());
        gtk_text_buffer_delete(this->buffer.get(), &ia, &ib);
        gtk_text_buffer_end_user_action(this->buffer.get());
        this->contentsChanged(true);
        this->repaintEditor();
        return;
    }
    if (const auto* f = getFormula()) {
        // xournalpp-calc: before the cursor is a hidden parenthesis or operator of the formula. In an empty box
        // (exponent, denominator, root), the structure goes away ("^()", "/()"); otherwise the cursor leaves the box
        const size_t cur = cursorByte();
        size_t prev = cur;
        const std::string text = cloneToStdString(this->buffer.get());
        if (prev > 0) {
            prev--;
            while (prev > 0 && (static_cast<unsigned char>(text[prev]) & 0xC0) == 0x80) {
                prev--;
            }
        }
        if (cur > 0 && !f->visibleByte(prev)) {
            for (const auto& s: f->slots()) {
                if (s.start == cur && s.end == cur && s.delStart != std::string::npos) {
                    GtkTextIter a = getIteratorAtByteOffset(this->buffer.get(), static_cast<int>(s.delStart));
                    GtkTextIter b = getIteratorAtByteOffset(this->buffer.get(), static_cast<int>(s.delEnd));
                    gtk_text_buffer_begin_user_action(this->buffer.get());
                    gtk_text_buffer_delete(this->buffer.get(), &a, &b);
                    gtk_text_buffer_end_user_action(this->buffer.get());
                    this->contentsChanged(true);
                    this->repaintEditor();
                    return;
                }
            }
            if (f->step(cur, -1) != cur) {
                placeCursorAtByte(f->step(cur, -1));
                return;
            }
        }
    }

    GtkTextIter insert = getIteratorAtCursor(this->buffer.get());

    if (gtk_text_buffer_backspace(this->buffer.get(), &insert, true, true)) {
        this->contentsChanged();
        this->repaintEditor();
        this->updateCompletion();
    } else {
        gtk_widget_error_bell(this->xournalWidget);
    }
}

void TextEditor::linebreak() {
    this->resetImContext();
    iMCommitCallback(nullptr, "\n", this);

    control->getCursor()->setInvisible(true);
}

void TextEditor::tabulation() {
    resetImContext();
    Settings* settings = control->getSettings();
    if (!settings->getUseSpacesAsTab()) {
        iMCommitCallback(nullptr, "\t", this);
    } else {
        std::string indent(static_cast<size_t>(settings->getNumberOfSpacesForTab()), ' ');
        iMCommitCallback(nullptr, indent.c_str(), this);
    }

    control->getCursor()->setInvisible(true);
}


void TextEditor::copyToClipboard() const {
    auto* clipboard = gtk_widget_get_clipboard(this->xournalWidget);
    gtk_text_buffer_copy_clipboard(this->buffer.get(), clipboard);
}

void TextEditor::cutToClipboard() {
    auto* clipboard = gtk_widget_get_clipboard(this->xournalWidget);
    gtk_text_buffer_cut_clipboard(this->buffer.get(), clipboard, true);

    this->contentsChanged(true);
    this->repaintEditor();
}

void TextEditor::pasteFromClipboard() {
    auto* clipboard = gtk_widget_get_clipboard(this->xournalWidget);
    gtk_text_buffer_paste_clipboard(this->buffer.get(), clipboard, nullptr, true);
}

void TextEditor::bufferPasteDoneCallback(GtkTextBuffer* buffer, GtkClipboard* clipboard, TextEditor* te) {
    te->contentsChanged(true);
    te->repaintEditor();

    if (te->textElement->getWrap() == Text::NO_WRAP && te->previousBoundingBox.maxX > te->page->getWidth()) {
        te->textElement->setWrap(te->page->getWidth() - te->previousBoundingBox.minX);
        te->currentWrapWidth = te->textElement->getWrap();
        te->layoutStatus = LayoutStatus::NEEDS_PARAMETERS_UPDATE;
        te->repaintEditor(true);
    }
}

void TextEditor::resetImContext() {
    if (this->needImReset) {
        this->needImReset = false;
        gtk_im_context_reset(this->imContext.get());
    }
}

/*
 * Blink!
 */
void TextEditor::blinkCallback(TextEditor* te) {
    te->cursorVisible = !te->cursorVisible;
    auto time = te->cursorVisible ? te->cursorBlinkingTimeOn : te->cursorBlinkingTimeOff;
    te->blinkTimer = g_timeout_add(time, xoj::util::wrap_for_once_v<blinkCallback>, te);

    auto dirtyRange = Range(te->textElement->getTransformation() * xoj::util::Rectangle<double>(te->cursorBox));
    te->viewPool->dispatch(xoj::view::TextEditionView::FLAG_DIRTY_REGION, dirtyRange);
}

void TextEditor::setTextToPangoLayout(PangoLayout* pl) const {
    std::string_view preed(preeditString);

    if (!preed.empty()) {
        // When using an Input Method, we need to insert the preeditString into the text at the cursor location
        std::string txt = cloneWithInsertToStdString(this->buffer.get(), preed);

        int pos = getByteOffsetOfCursor(this->buffer.get());
        xoj::util::PangoAttrListSPtr attrlist(pango_attr_list_new(), xoj::util::adopt);
        xoj::text::addToPango(this->textElement->getFormats(), cloneToStdString(this->buffer.get()), attrlist.get(),
                              this->textElement->getFontSize());
        pango_attr_list_splice(attrlist.get(), this->preeditAttrList.get(), pos, static_cast<int>(preed.length()));

        pango_layout_set_attributes(pl, attrlist.get());

        pango_layout_set_text(pl, txt.c_str(), static_cast<int>(txt.length()));
    } else {
        setSelectionAttributesToPangoLayout(pl);
        pango_layout_set_text(pl, cloneToCString(this->buffer.get()).get(), -1);
    }
    this->textElement->applyLineHeight(pl);
}

Color TextEditor::getSelectionColor() const { return this->control->getSettings()->getSelectionColor(); }

void TextEditor::setSelectionAttributesToPangoLayout(PangoLayout* pl) const {
    xoj::util::PangoAttrListSPtr attrlist(pango_attr_list_new(), xoj::util::adopt);
    if (!this->textElement->getFormats().empty()) {
        xoj::text::addToPango(this->textElement->getFormats(), cloneToStdString(this->buffer.get()), attrlist.get(),
                              this->textElement->getFontSize());
    }

    GtkTextIter start;
    GtkTextIter end;
    bool hasSelection = gtk_text_buffer_get_selection_bounds(this->buffer.get(), &start, &end);

    if (hasSelection) {
        auto selectionColorU16 = Util::argb_to_ColorU16(this->getSelectionColor());
        PangoAttribute* attrib =
                pango_attr_background_new(selectionColorU16.red, selectionColorU16.green, selectionColorU16.blue);
        attrib->start_index = static_cast<unsigned int>(getByteOffsetOfIterator(start));
        attrib->end_index = static_cast<unsigned int>(getByteOffsetOfIterator(end));

        pango_attr_list_insert(attrlist.get(), attrib);  // attrlist takes ownership of attrib
    }

    // xournalpp-calc: misspelled words get a red wavy line (not in calculation regions, nor the word being typed)
    if (this->control->getSettings()->isSpellCheck() && !this->textElement->isCalc()) {
        const std::string text = cloneToStdString(this->buffer.get());
        const int cursor = getByteOffsetOfCursor(this->buffer.get());
        for (auto [s, e]: xoj::spell::misspelled(text)) {
            if (e == cursor && !hasSelection) {
                continue;
            }
            PangoAttribute* line = pango_attr_underline_new(PANGO_UNDERLINE_ERROR);
            line->start_index = static_cast<unsigned int>(s);
            line->end_index = static_cast<unsigned int>(e);
            pango_attr_list_insert(attrlist.get(), line);
            PangoAttribute* color = pango_attr_underline_color_new(0xe000, 0x1800, 0x1800);
            color->start_index = static_cast<unsigned int>(s);
            color->end_index = static_cast<unsigned int>(e);
            pango_attr_list_insert(attrlist.get(), color);
        }
    }

    pango_layout_set_attributes(pl, attrlist.get());
}

auto TextEditor::misspelledWordAt(double x, double y) -> std::string {
    this->spellStart = this->spellEnd = -1;
    if (!this->control->getSettings()->isSpellCheck() || this->textElement->isCalc()) {
        return {};
    }
    auto p = this->textElement->getTransformation().inverse() * xoj::util::Point<double>(x, y);
    GtkTextIter it;
    findPos(&it, p.x, p.y);
    const std::string text = cloneToStdString(this->buffer.get());
    // byte offset of the clicked character
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(this->buffer.get(), &start);
    auto before = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&start, &it));
    const int byte = static_cast<int>(strlen(before.get()));
    for (auto [s, e]: xoj::spell::misspelled(text)) {
        if (byte >= s && byte <= e) {
            this->spellStart = static_cast<int>(g_utf8_strlen(text.c_str(), s));
            this->spellEnd = static_cast<int>(g_utf8_strlen(text.c_str(), e));
            return text.substr(static_cast<size_t>(s), static_cast<size_t>(e - s));
        }
    }
    return {};
}

void TextEditor::replaceMisspelled(const std::string& word) {
    if (this->spellStart < 0 || this->spellEnd <= this->spellStart) {
        return;
    }
    GtkTextIter a, b;
    gtk_text_buffer_get_iter_at_offset(this->buffer.get(), &a, this->spellStart);
    gtk_text_buffer_get_iter_at_offset(this->buffer.get(), &b, this->spellEnd);
    gtk_text_buffer_delete(this->buffer.get(), &a, &b);
    gtk_text_buffer_insert(this->buffer.get(), &a, word.c_str(), -1);
    gtk_text_buffer_place_cursor(this->buffer.get(), &a);
    this->spellStart = this->spellEnd = -1;
    this->contentsChanged(true);
    formatsChanged();
}

void TextEditor::refreshSpelling() { formatsChanged(); }

void TextEditor::updateBoxes() {
    if (const auto* f = getFormula()) {
        this->boxes.theoreticalSize = {f->width(), f->bottom()};
        this->boxes.effectiveBounds = {0, f->top(), f->width(), f->bottom() - f->top()};
        this->previousBoundingBox = Range(this->textElement->getTransformation() * this->boxes.effectiveBounds);
        return;
    }
    /*
     * NB: we cannot rely on Text::calcSize directly, since it would not take the size changes due to the IM
     * preeditString into account.
     */
    this->boxes = Text::computeBoxesForLayout(getUpToDateLayout(), this->currentWrapWidth);
    auto edBounds = boxes.effectiveBounds;
    edBounds.width = std::max(edBounds.width, boxes.theoreticalSize.width);
    this->previousBoundingBox = Range(this->textElement->getTransformation() * edBounds);
}

auto TextEditor::getUpToDateLayout() const -> PangoLayout* {
    switch (layoutStatus) {
        case LayoutStatus::NEEDS_COMPLETE_UPDATE:
            setTextToPangoLayout(this->layout.get());
            break;
        case LayoutStatus::NEEDS_ATTRIBUTES_UPDATE:
            setSelectionAttributesToPangoLayout(this->layout.get());
            break;
        case LayoutStatus::NEEDS_PARAMETERS_UPDATE:
            pango_layout_set_width(this->layout.get(), round_cast<int>(this->currentWrapWidth * PANGO_SCALE));
            pango_layout_set_justify(layout.get(), this->textElement->getJustify());
            pango_layout_set_alignment(layout.get(), this->textElement->getAlign().toPango());
            break;
        case LayoutStatus::UP_TO_DATE:
            break;
    }
    layoutStatus = LayoutStatus::UP_TO_DATE;
    return this->layout.get();
}

auto TextEditor::getCursorBox() const -> const Range& { return this->cursorBox; }

bool TextEditor::isCursorVisible() const { return cursorVisible; }

auto TextEditor::computeCursorBox() const -> Range {
    if (const auto* f = getFormula()) {
        const auto c = f->caretAt(cursorByte());
        Range res(c.x, c.top);
        res.addPoint(c.x, c.bottom);
        return res;
    }
    // Compute the bounding box of the active grapheme (i.e. the one just after the cursor)
    int offset = getByteOffsetOfCursor(this->buffer.get());
    if (this->preeditString && this->preeditCursor != 0) {
        const gchar* preeditText = this->preeditString.get();
        offset += static_cast<int>(g_utf8_offset_to_pointer(preeditText, preeditCursor) - preeditText);
    }
    PangoRectangle rect = {0};
    pango_layout_index_to_pos(getUpToDateLayout(), offset, &rect);
    const double ratio = 1.0 / PANGO_SCALE;

    // Warning: rect.width could be negative (e.g. for languages written from right to left).
    Range res(rect.x * ratio, rect.y * ratio);
    res.addPoint((rect.x + (cursorOverwrite ? rect.width : 0.0)) * ratio, (rect.y + rect.height) * ratio);
    return res;
}

void TextEditor::repaintEditor(bool sizeChanged) {
    Range dirtyRange(this->previousBoundingBox);
    if (sizeChanged) {
        this->updateBoxes();
        dirtyRange = dirtyRange.unite(this->previousBoundingBox);
    }
    this->updateCursorBox();
    this->updateDraggableIcons();
    this->viewPool->dispatch(xoj::view::TextEditionView::FLAG_DIRTY_REGION, dirtyRange);
    this->control->fireTextFormatChanged();
}

void TextEditor::repaintCursorAfterChange() {
    const auto& m = this->textElement->getTransformation();
    auto dirtyRange = Range(m * xoj::util::Rectangle<double>(this->cursorBox));
    this->updateCursorBox();
    dirtyRange = dirtyRange.unite(Range(m * xoj::util::Rectangle<double>(this->cursorBox)));
    this->viewPool->dispatch(xoj::view::TextEditionView::FLAG_DIRTY_REGION, dirtyRange);
    this->control->fireTextFormatChanged();
}

void TextEditor::finalizeEdition() {
    hideCompletion();

    auto* db = this->control->getActionDatabase();
    auto* th = this->control->getToolHandler();
    db->setActionState(Action::FONT, this->control->getSettings()->getFont().asString().c_str());
    db->setActionState(Action::TEXT_ALIGNMENT, th->getTextAlignment());
    db->setActionState(Action::TEXT_JUSTIFY, th->getTextJustify());
    db->setActionState(Action::TOOL_COLOR, th->getColorMaskAlpha());

    auto* doc = this->control->getDocument();
    UndoRedoHandler* undo = this->control->getUndoRedoHandler();

    if (this->bufferEmpty()) {
        // Delete the edited element from layer
        if (originalTextElement) {
            auto eraseDeleteUndoAction = std::make_unique<DeleteUndoAction>(page, true);
            doc->lock();
            Layer* layer = this->page->getSelectedLayer();
            auto [orig, elementIndex] = layer->removeElement(originalTextElement);
            doc->unlock();
            if (elementIndex != Element::InvalidIndex) [[likely]] {
                eraseDeleteUndoAction->addElement(layer, std::move(orig), elementIndex);
                undo->addUndoAction(std::move(eraseDeleteUndoAction));
            }  // A warning has already been issued otherwise
            originalTextElement = nullptr;
        }
        this->viewPool->dispatchAndClear(xoj::view::TextEditionView::FINALIZATION_REQUEST, this->previousBoundingBox);
        return;
    }

    this->updateTextElementContent();
    if (originalTextElement) {
        // Modifying a preexisting element
        this->viewPool->dispatchAndClear(xoj::view::TextEditionView::FINALIZATION_REQUEST, this->previousBoundingBox);

        if (this->textElement->isCalc() && this->textElement->getText() != this->originalTextElement->getText()) {
            // xournalpp-calc: the results of the old text are not shown with the new one (they come back at once,
            // when the regions are recalculated)
            this->textElement->setCalcMarkup({});
        }
        doc->lock();
        Layer* layer = this->page->getSelectedLayer();
        auto [orig, _] = layer->removeElement(this->originalTextElement);
        auto ptr = this->textElement.get();
        layer->addElement(std::move(this->textElement));
        doc->unlock();

        this->page->fireElementChanged(ptr);

        if (orig) [[likely]] {
            xoj_assert(orig.get() == this->originalTextElement);
            this->originalTextElement->setInEditing(false);
            undo->addUndoAction(std::make_unique<TextBoxUndoAction>(this->page, layer, ptr, std::move(orig)));
        } else {
            // A warning has already been issued
            undo->addUndoAction(std::make_unique<InsertUndoAction>(this->page, layer, ptr));
        }
        originalTextElement = nullptr;
    } else {
        // Creating a new element
        auto ptr = this->textElement.get();
        doc->lock();
        Layer* layer = this->page->getSelectedLayer();
        layer->addElement(std::move(this->textElement));
        doc->unlock();
        this->viewPool->dispatchAndClear(xoj::view::TextEditionView::FINALIZATION_REQUEST, this->previousBoundingBox);
        this->page->fireElementChanged(ptr);
        undo->addUndoAction(std::make_unique<InsertUndoAction>(page, layer, ptr));
    }
}

void TextEditor::initializeEditionAt(double x, double y) {
    // Is there already a textfield?
    Text* text = nullptr;
    std::shared_lock lock(*this->control->getDocument());

    // Should we reverse this loop to select the most recent text rather than the oldest?
    const auto exactLine = this->control->getCalcGridExactLine();
    for (auto&& e: this->page->getSelectedLayer()->getElements()) {
        if (e->getType() == ELEMENT_TEXT && e->hasBoundingBoxContaining(x, y)) {
            auto* t = dynamic_cast<Text*>(e.get());
            if (exactLine && t) {
                // Grid navigation: only enter a text that has a line on the requested row of dots
                auto grid = xoj::calc::alignmentGrid(this->page->getBackgroundType(), this->page->getWidth(),
                                                     this->page->getHeight());
                const double first = t->getOrigin().y + t->firstBaseline() + xoj::calc::BASELINE_LIFT;
                const double pitch = t->getLineHeight() > 0 ? t->getLineHeight() : t->naturalLineHeight();
                const auto lines = std::count(t->getText().begin(), t->getText().end(), '\n');
                if (*exactLine < first - grid.dy / 2 ||
                    *exactLine > first + static_cast<double>(lines) * pitch + grid.dy / 2) {
                    continue;
                }
            }
            text = t;
            break;
        }
    }

    if (text == nullptr) {
        lock.unlock();
        ToolHandler* h = this->control->getToolHandler();
        this->textElement = std::make_unique<Text>();
        this->textElement->setColor(h->getColor());
        this->textElement->setFont(control->getSettings()->getFont());
        this->textElement->setTransformation(
                xoj::util::Matrix::TRANSLATION(x, y - this->textElement->getBoundingBox().height / 2));
        this->textElement->setAlignment(h->getTextAlignment());
        this->textElement->setJustify(h->getTextJustify());
        this->textElement->setCalc(this->control->isCalcMode());
        if (this->control->isCalcGrid()) {
            // Mathcad-style alignment: start on a dot of the grid, with the baseline on a row of dots (a ruled line)
            auto grid = xoj::calc::alignmentGrid(this->page->getBackgroundType(), this->page->getWidth(),
                                                 this->page->getHeight());
            double lineH = this->textElement->naturalLineHeight();
            this->textElement->setLineHeight(grid.pitchFor(lineH));
            // The letters sit 1 mm above the line, so that the dots do not cut them
            double baseline = this->control->getCalcGridExactLine().value_or(grid.snapY(y + 0.25 * lineH)) -
                              xoj::calc::BASELINE_LIFT;
            this->textElement->setTransformation(
                    xoj::util::Matrix::TRANSLATION(grid.snapX(x), baseline - this->textElement->firstBaseline()));
        }

#ifdef ENABLE_AUDIO
        if (auto audioController = control->getAudioController(); audioController && audioController->isRecording()) {
            fs::path audioFilename = audioController->getAudioFilename();
            size_t sttime = audioController->getStartTime();
            size_t milliseconds = (as_unsigned(g_get_monotonic_time() / 1000) - sttime);
            this->textElement->setTimestamp(milliseconds);
            this->textElement->setAudioFilename(audioFilename);
        }
#endif
        this->originalTextElement = nullptr;
    } else {
        this->originalTextElement = text;
        this->textElement = text->cloneText();
        // xournalpp-calc: a calculation region is shown with its results (wider than what is written); while it is
        // edited only what is written is shown, so the area of the results must be repainted too
        const auto shownBox = text->getBoundingBox();
        text->setInEditing(true);
        lock.unlock();
        Range shown(shownBox);
        this->page->fireRangeChanged(shown);

        auto* db = this->control->getActionDatabase();
        db->setActionState(Action::FONT, this->textElement->getFont().asString().c_str());
        db->setActionState(Action::TEXT_ALIGNMENT, this->textElement->getAlign());
        db->setActionState(Action::TEXT_JUSTIFY, this->textElement->getJustify());
        Color c = this->textElement->getColor();
        c.alpha = 0xff;
        db->setActionState(Action::TOOL_COLOR, c);

        this->page->fireElementChanged(text);
    }
    this->currentWrapWidth = this->textElement->getWrap();
    this->layout = this->textElement->createPangoLayout();
    this->replaceBufferContent(this->textElement->getText());
    this->updateBoxes();
}

// ---------------------------------------------------------------------------------------------------------------------
// xournalpp-calc: formats of parts of the text

void TextEditor::bufferInsertCallback(GtkTextBuffer*, GtkTextIter* location, gchar* text, gint len, TextEditor* te) {
    if (!te->trackFormats) {
        return;
    }
    const int count = static_cast<int>(g_utf8_strlen(text, len));
    const int pos = gtk_text_iter_get_offset(location) - count;
    auto f = te->textElement->getFormats();
    if (f.empty() && te->pendingFormats.empty()) {
        return;
    }
    xoj::text::onInsert(f, pos, count);
    if (!te->pendingFormats.empty()) {
        if (pos == te->pendingPos) {
            for (const auto& [kind, value]: te->pendingFormats) {
                xoj::text::set(f, kind, pos, pos + count, value);
            }
            te->pendingPos = pos + count;
        } else {
            te->pendingFormats.clear();
        }
    }
    te->textElement->setFormats(std::move(f));
}

void TextEditor::bufferDeleteCallback(GtkTextBuffer*, GtkTextIter* start, GtkTextIter* end, TextEditor* te) {
    if (!te->trackFormats || te->textElement->getFormats().empty()) {
        return;
    }
    auto f = te->textElement->getFormats();
    xoj::text::onDelete(f, gtk_text_iter_get_offset(start), gtk_text_iter_get_offset(end));
    te->textElement->setFormats(std::move(f));
}

bool TextEditor::hasSelection() const { return gtk_text_buffer_get_has_selection(this->buffer.get()); }

std::pair<int, int> TextEditor::selectionOffsets() const {
    GtkTextIter a, b;
    gtk_text_buffer_get_selection_bounds(this->buffer.get(), &a, &b);
    int s = gtk_text_iter_get_offset(&a);
    int e = gtk_text_iter_get_offset(&b);
    return {std::min(s, e), std::max(s, e)};
}

void TextEditor::formatsChanged() {
    this->layoutStatus = LayoutStatus::NEEDS_COMPLETE_UPDATE;
    this->computeVirtualCursorPosition();
    this->repaintEditor(true);
}

auto TextEditor::formatState(xoj::text::FormatKind kind) const -> std::optional<std::string> {
    auto [s, e] = selectionOffsets();
    if (s == e) {
        if (auto it = pendingFormats.find(kind); it != pendingFormats.end() && pendingPos == s) {
            return it->second;
        }
    }
    return xoj::text::valueAt(this->textElement->getFormats(), kind, s, e);
}

void TextEditor::applyFormat(xoj::text::FormatKind kind, const std::optional<std::string>& value) {
    auto [s, e] = selectionOffsets();
    if (s == e) {
        if (pendingPos != s) {
            pendingFormats.clear();
        }
        pendingFormats[kind] = value;
        pendingPos = s;
        this->control->fireTextFormatChanged();
        return;
    }
    auto f = this->textElement->getFormats();
    xoj::text::set(f, kind, s, e, value);
    this->textElement->setFormats(std::move(f));
    formatsChanged();
}

void TextEditor::toggleFormat(xoj::text::FormatKind kind, const std::string& value) {
    auto cur = formatState(kind);
    applyFormat(kind, cur && *cur == value ? std::nullopt : std::optional<std::string>(value));
}

void TextEditor::clearFormatting() {
    auto [s, e] = selectionOffsets();
    if (s == e) {
        s = 0;
        e = gtk_text_buffer_get_char_count(this->buffer.get());
    }
    pendingFormats.clear();
    auto f = this->textElement->getFormats();
    xoj::text::clear(f, s, e);
    this->textElement->setFormats(std::move(f));
    formatsChanged();
}

void TextEditor::changeCase(CaseChange c) {
    GtkTextIter a, b;
    if (!gtk_text_buffer_get_selection_bounds(this->buffer.get(), &a, &b)) {
        // the word at the cursor
        a = getIteratorAtCursor(this->buffer.get());
        b = a;
        if (!gtk_text_iter_starts_word(&a)) {
            gtk_text_iter_backward_word_start(&a);
        }
        if (!gtk_text_iter_ends_word(&b)) {
            gtk_text_iter_forward_word_end(&b);
        }
    }
    const int s = gtk_text_iter_get_offset(&a);
    const int e = gtk_text_iter_get_offset(&b);
    if (e <= s) {
        return;
    }
    auto old = xoj::util::OwnedCString::assumeOwnership(gtk_text_iter_get_text(&a, &b));
    std::string src(old.get());
    std::string out;
    bool startOfSentence = true;
    bool startOfWord = true;
    for (const char* p = src.c_str(); *p; p = g_utf8_next_char(p)) {
        gunichar ch = g_utf8_get_char(p);
        gunichar r = ch;
        switch (c) {
            case CaseChange::LOWER:
                r = g_unichar_tolower(ch);
                break;
            case CaseChange::UPPER:
                r = g_unichar_toupper(ch);
                break;
            case CaseChange::CAPITALIZE:
                r = startOfWord ? g_unichar_toupper(ch) : g_unichar_tolower(ch);
                break;
            case CaseChange::SENTENCE:
                r = startOfSentence && g_unichar_isalpha(ch) ? g_unichar_toupper(ch) : g_unichar_tolower(ch);
                break;
            case CaseChange::TOGGLE:
                r = g_unichar_isupper(ch) ? g_unichar_tolower(ch) : g_unichar_toupper(ch);
                break;
        }
        if (g_unichar_isalpha(ch)) {
            startOfSentence = false;
        } else if (ch == '.' || ch == '!' || ch == '?' || ch == '\n') {
            startOfSentence = true;
        }
        startOfWord = !g_unichar_isalnum(ch);
        char buf[8];
        out.append(buf, static_cast<size_t>(g_unichar_to_utf8(r, buf)));
    }
    if (out == src) {
        return;
    }
    // Same number of characters: the formats stay where they are
    const auto formats = this->textElement->getFormats();
    this->trackFormats = false;
    gtk_text_buffer_delete(this->buffer.get(), &a, &b);
    gtk_text_buffer_insert(this->buffer.get(), &a, out.c_str(), -1);
    this->trackFormats = true;
    GtkTextIter ns, ne;
    gtk_text_buffer_get_iter_at_offset(this->buffer.get(), &ns, s);
    gtk_text_buffer_get_iter_at_offset(this->buffer.get(), &ne, s + static_cast<int>(g_utf8_strlen(out.c_str(), -1)));
    gtk_text_buffer_select_range(this->buffer.get(), &ns, &ne);
    if (g_utf8_strlen(out.c_str(), -1) == e - s) {
        this->textElement->setFormats(formats);
    }
    this->contentsChanged();
    formatsChanged();
}

std::string TextEditor::currentFamily() const {
    if (auto v = formatState(xoj::text::FormatKind::FAMILY); v) {
        return *v;
    }
    PangoFontDescription* desc = pango_font_description_from_string(this->textElement->getFontName().c_str());
    std::string fam = pango_font_description_get_family(desc) ? pango_font_description_get_family(desc) : "Sans";
    pango_font_description_free(desc);
    return fam;
}

double TextEditor::currentSize() const {
    if (auto v = formatState(xoj::text::FormatKind::SIZE); v) {
        try {
            return std::stod(*v);
        } catch (...) {}
    }
    return this->textElement->getFontSize();
}

void TextEditor::setFormatFamily(const std::string& family) {
    if (hasSelection()) {
        applyFormat(xoj::text::FormatKind::FAMILY, family);
        return;
    }
    XojFont font = this->textElement->getFont();
    font.setName(family);
    setFont(font);
    this->control->fireTextFormatChanged();
}

void TextEditor::setFormatSize(double size) {
    if (size <= 0) {
        return;
    }
    if (hasSelection()) {
        char buf[32];
        g_ascii_formatd(buf, sizeof(buf), "%g", size);
        applyFormat(xoj::text::FormatKind::SIZE, std::string(buf));
        return;
    }
    XojFont font = this->textElement->getFont();
    font.setSize(size);
    setFont(font);
    this->control->fireTextFormatChanged();
}

void TextEditor::growFont(bool bigger) {
    static const double SIZES[] = {8, 9, 10, 11, 12, 14, 16, 18, 20, 22, 24, 26, 28, 36, 48, 72};
    double cur = currentSize();
    double next = cur;
    if (bigger) {
        next = cur + 10;
        for (double v: SIZES) {
            if (v > cur + 0.01) {
                next = v;
                break;
            }
        }
    } else {
        next = std::max(1.0, cur - 1);
        for (double v: SIZES) {
            if (v < cur - 0.01) {
                next = v;
            }
        }
        if (cur <= SIZES[0]) {
            next = std::max(1.0, cur - 1);
        }
    }
    setFormatSize(next);
}
