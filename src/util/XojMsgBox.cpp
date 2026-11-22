#include "util/XojMsgBox.h"

#include <utility>  // for move

#include <glib-object.h>  // for g_object_set_property, g_value_init, g_valu...
#include <glib.h>         // for g_free, g_markup_escape_text, g_error_free

#include "util/PopupWindowWrapper.h"
#include "util/Util.h"
#include "util/gtk4_helper.h"
#include "util/i18n.h"  // for _, FS, _F
#include "util/raii/CStringWrapper.h"

#ifdef _WIN32
// Needed for help dialog workaround on Windows; see XojMsgBox::showHelp
#include <windows.h>
// <windows.h> must be included first
#include <shellapi.h>  // for ShellExecute
#endif

GtkWindow* defaultWindow = nullptr;

XojMsgBox::XojMsgBox(GtkDialog* dialog, xoj::util::move_only_function<void(int)> callback, CallbackPolicy pol):
        window(reinterpret_cast<GtkWindow*>(dialog)), callback(std::move(callback)), policy(pol) {
    this->signalId =
            g_signal_connect(dialog, "response", G_CALLBACK(+[](GtkDialog* dialog, int response, gpointer data) {
                                 auto* self = static_cast<XojMsgBox*>(data);

                                 if (self->policy == IMMEDIATE) {
                                     self->callback(response);
                                 } else {  // POSTPONED
                                     // We sometimes need to call gtk_window_close() before invoking the callback,
                                     // because if the callback pops up another dialog, the first one won't close...
                                     // But since gtk_window_close() triggers the destruction of *self, we first move
                                     // the callback
                                     Util::execInUiThread([cb = std::move(self->callback), r = response]() { cb(r); });
                                 }

                                 // Closing the window causes another "response" signal, which we want to ignore
                                 g_signal_handler_disconnect(dialog, self->signalId);
                                 gtk_window_close(self->getWindow());  // Destroys *self. Beware!
                             }),
                             this);
}

namespace {
// Lucide line icons (ISC license), drawn in the colour of each kind of message
constexpr auto SVG_HEAD = R"(<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" )"
                          R"(fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" stroke=")";
constexpr auto ICON_QUESTION =
        R"(<circle cx="12" cy="12" r="10"/><path d="M9.1 9a3 3 0 0 1 5.8 1c0 2-3 3-3 3M12 17h.01"/>)";
constexpr auto ICON_SAVE =
        R"(<path d="M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5)"
        R"(a2 2 0 0 1 2-2z"/><path d="M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7M7 3v4a1 1 0 0 0 1 1h7"/>)";
constexpr auto ICON_ERROR = R"(<circle cx="12" cy="12" r="10"/><path d="M12 8v4M12 16h.01"/>)";
constexpr auto ICON_WARNING =
        R"(<path d="m21.7 18-8-14a2 2 0 0 0-3.5 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.7-3M12 9v4M12 17h.01"/>)";
constexpr auto ICON_INFO = R"(<circle cx="12" cy="12" r="10"/><path d="M12 16v-4M12 8h.01"/>)";

auto iconImage(const char* body, const char* color) -> GtkWidget* {
    std::string svg = std::string(SVG_HEAD) + color + "\">" + body + "</svg>";
    GdkPixbufLoader* loader = gdk_pixbuf_loader_new_with_type("svg", nullptr);
    if (!loader) {
        return nullptr;
    }
    gdk_pixbuf_loader_set_size(loader, 44, 44);  // drawn at twice the size, sharp on scaled screens
    GtkWidget* img = nullptr;
    if (gdk_pixbuf_loader_write(loader, reinterpret_cast<const guchar*>(svg.data()), svg.size(), nullptr) &&
        gdk_pixbuf_loader_close(loader, nullptr)) {
        if (GdkPixbuf* pb = gdk_pixbuf_loader_get_pixbuf(loader)) {
            cairo_surface_t* surf = gdk_cairo_surface_create_from_pixbuf(pb, 2, nullptr);
            img = gtk_image_new_from_surface(surf);
            cairo_surface_destroy(surf);
        }
    } else {
        gdk_pixbuf_loader_close(loader, nullptr);
    }
    g_object_unref(loader);
    return img;
}

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }
}  // namespace

void XojMsgBox::applyStyle(GtkDialog* dialog, GtkMessageType type, Icon icon) {
    GtkWidget* w = GTK_WIDGET(dialog);
    addClass(w, "jm-dialog");
    gtk_widget_set_size_request(w, 420, -1);

    // Title bar: like the main window's, with the logo and the program name
    GtkWidget* hb = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(hb), true);
    gtk_header_bar_set_decoration_layout(GTK_HEADER_BAR(hb), ":close");
    gtk_header_bar_set_title(GTK_HEADER_BAR(hb), "Xournal JackMath");
    addClass(hb, "jm-headerbar");
    GtkWindow* parent = gtk_window_get_transient_for(GTK_WINDOW(dialog));
    if (!parent) {
        parent = defaultWindow;
    }
    if (GdkPixbuf* logo = parent ? gtk_window_get_icon(parent) : nullptr) {
        if (GdkPixbuf* small = gdk_pixbuf_scale_simple(logo, 18, 18, GDK_INTERP_BILINEAR)) {
            GtkWidget* img = gtk_image_new_from_pixbuf(small);
            g_object_unref(small);
            gtk_header_bar_pack_start(GTK_HEADER_BAR(hb), img);
        }
    }
    gtk_widget_show_all(hb);
    gtk_window_set_titlebar(GTK_WINDOW(dialog), hb);

    // Icon at the left of the text
    const char* body = ICON_QUESTION;
    const char* color = "#2f5d8a";
    const char* kind = "jm-dlg-info";
    if (type == GTK_MESSAGE_ERROR) {
        body = ICON_ERROR, color = "#c0392b", kind = "jm-dlg-error";
    } else if (type == GTK_MESSAGE_WARNING || icon == Icon::WARNING) {
        body = ICON_WARNING, color = "#b7791f", kind = "jm-dlg-warning";
    } else if (icon == Icon::SAVE) {
        body = ICON_SAVE;
    } else if (type == GTK_MESSAGE_INFO || type == GTK_MESSAGE_OTHER) {
        body = ICON_INFO;
    }
    if (GTK_IS_MESSAGE_DIALOG(dialog)) {
        GtkWidget* area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
        GtkWidget* row = gtk_widget_get_parent(area);
        if (GTK_IS_BOX(row)) {
            // GTK's own big icon (deprecated) is replaced by ours
            GList* kids = gtk_container_get_children(GTK_CONTAINER(row));
            for (GList* k = kids; k; k = k->next) {
                if (GTK_IS_IMAGE(k->data)) {
                    gtk_widget_set_no_show_all(GTK_WIDGET(k->data), true);
                    gtk_widget_hide(GTK_WIDGET(k->data));
                }
            }
            g_list_free(kids);
            gtk_widget_set_margin_start(row, 24);
            gtk_widget_set_margin_end(row, 24);
            gtk_widget_set_margin_top(row, 20);
            gtk_widget_set_margin_bottom(row, 4);
        }
        GtkWidget* content = gtk_dialog_get_content_area(dialog);
        gtk_container_set_border_width(GTK_CONTAINER(content), 0);
        gtk_box_set_spacing(GTK_BOX(content), 0);
        gtk_window_set_resizable(GTK_WINDOW(dialog), false);
        if (GtkWidget* img = iconImage(body, color); img && GTK_IS_BOX(row)) {
            GtkWidget* tile = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
            addClass(tile, "jm-dlg-icon");
            addClass(tile, kind);
            gtk_widget_set_valign(tile, GTK_ALIGN_START);
            gtk_widget_set_halign(img, GTK_ALIGN_CENTER);
            gtk_box_pack_start(GTK_BOX(tile), img, true, false, 0);
            gtk_widget_show_all(tile);
            gtk_box_pack_start(GTK_BOX(row), tile, false, false, 0);
            gtk_box_reorder_child(GTK_BOX(row), tile, 0);
            gtk_box_set_spacing(GTK_BOX(row), 16);
        }
        // Text aligned to the left, as in the rest of the program
        GList* labels = gtk_container_get_children(GTK_CONTAINER(area));
        bool first = true;
        for (GList* l = labels; l; l = l->next) {
            if (GTK_IS_LABEL(l->data)) {
                gtk_label_set_xalign(GTK_LABEL(l->data), 0.0f);
                gtk_widget_set_halign(GTK_WIDGET(l->data), GTK_ALIGN_FILL);
                gtk_label_set_max_width_chars(GTK_LABEL(l->data), 50);
                addClass(GTK_WIDGET(l->data), first ? "jm-dlg-title" : "jm-dlg-text");
                first = false;
            }
        }
        g_list_free(labels);
    }

    // Buttons: rounded, on the right, not stretched over the whole width
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget* actions = gtk_dialog_get_action_area(dialog);
    G_GNUC_END_IGNORE_DEPRECATIONS
    if (GTK_IS_BUTTON_BOX(actions)) {
        gtk_button_box_set_layout(GTK_BUTTON_BOX(actions), GTK_BUTTONBOX_END);
        gtk_box_set_homogeneous(GTK_BOX(actions), false);
        gtk_box_set_spacing(GTK_BOX(actions), 8);
        addClass(actions, "jm-dlg-actions");
        GList* buttons = gtk_container_get_children(GTK_CONTAINER(actions));
        if (buttons && !buttons->next) {
            addClass(GTK_WIDGET(buttons->data), "suggested-action");  // a lone OK is the main button
        }
        for (GList* b = buttons; b; b = b->next) {
            G_GNUC_BEGIN_IGNORE_DEPRECATIONS
            gtk_button_box_set_child_non_homogeneous(GTK_BUTTON_BOX(actions), GTK_WIDGET(b->data), true);
            G_GNUC_END_IGNORE_DEPRECATIONS
        }
        g_list_free(buttons);
    }
}

/**
 * Set window for messages without window
 */
void XojMsgBox::setDefaultWindow(GtkWindow* win) { defaultWindow = win; }

void XojMsgBox::showMarkupMessageToUser(GtkWindow* win, const std::string_view& markupTitle, const std::string& msg,
                                        GtkMessageType type) {
    if (win == nullptr) {
        win = defaultWindow;
    }

    GtkWidget* dialog = gtk_message_dialog_new_with_markup(win, GTK_DIALOG_MODAL, type, GTK_BUTTONS_OK, nullptr);

    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), markupTitle.data());

    if (!msg.empty()) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", msg.c_str());
    }
    applyStyle(GTK_DIALOG(dialog), type);

    xoj::popup::PopupWindowWrapper<XojMsgBox> popup(GTK_DIALOG(dialog));
    popup.show(win);
}

void XojMsgBox::showMessageToUser(GtkWindow* win, const std::string& title, const std::string& msg,
                                  GtkMessageType type) {
    auto escapedTitle = xoj::util::OwnedCString::assumeOwnership(g_markup_escape_text(title.c_str(), -1));
    showMarkupMessageToUser(win, escapedTitle.get(), msg, type);
}

void XojMsgBox::showMessageToUser(GtkWindow* win, const std::string& msg, GtkMessageType type) {
    showMessageToUser(win, msg, std::string(), type);
}

void XojMsgBox::showErrorToUser(GtkWindow* win, const std::string& msg) {
    showMessageToUser(win, msg, GTK_MESSAGE_ERROR);
    g_warning("%s", msg.c_str());
}

namespace {
XojMsgBox::Icon nextQuestionIcon = XojMsgBox::Icon::QUESTION;
}

void XojMsgBox::askQuestion(GtkWindow* win, const std::string& maintext, const std::string& secondarytext,
                            const std::vector<Button>& buttons, xoj::util::move_only_function<void(int)> callback) {

    auto formattedMsg = xoj::util::OwnedCString::assumeOwnership(g_markup_escape_text(maintext.c_str(), -1));
    askQuestionWithMarkup(win, std::string_view(formattedMsg), secondarytext, buttons, std::move(callback));
}

void XojMsgBox::askQuestion(GtkWindow* win, const std::string& maintext, const std::string& secondarytext,
                            const std::vector<Button>& buttons, xoj::util::move_only_function<void(int)> callback,
                            Icon icon) {
    nextQuestionIcon = icon;
    askQuestion(win, maintext, secondarytext, buttons, std::move(callback));
}

void XojMsgBox::askQuestionWithMarkup(GtkWindow* win, std::string_view maintext, const std::string& secondarytext,
                                      const std::vector<Button>& buttons,
                                      xoj::util::move_only_function<void(int)> callback) {
    if (win == nullptr) {
        win = defaultWindow;
    }

    GtkWidget* dialog =
            gtk_message_dialog_new_with_markup(win, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, nullptr);

    for (auto& b: buttons) {
        GtkWidget* btn = gtk_dialog_add_button(GTK_DIALOG(dialog), b.label.c_str(), b.response);
        if (b.style == ButtonStyle::SUGGESTED) {
            addClass(btn, "suggested-action");
            gtk_widget_grab_default(btn);
        } else if (b.style == ButtonStyle::DESTRUCTIVE) {
            addClass(btn, "destructive-action");
        }
    }

    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), maintext.data());

    if (!secondarytext.empty()) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", secondarytext.c_str());
    }
    applyStyle(GTK_DIALOG(dialog), GTK_MESSAGE_QUESTION, nextQuestionIcon);
    nextQuestionIcon = Icon::QUESTION;

    xoj::popup::PopupWindowWrapper<XojMsgBox> popup(GTK_DIALOG(dialog), std::move(callback));
    popup.show(win);
}

void XojMsgBox::showErrorAndQuit(std::string& msg, int exitCode) {
    GtkWidget* dialog = gtk_message_dialog_new_with_markup(defaultWindow, GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR,
                                                           GTK_BUTTONS_OK, nullptr);

    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), msg.c_str());
    applyStyle(GTK_DIALOG(dialog), GTK_MESSAGE_ERROR);

    // We use a hack to ensure the app does not exit until the user has read the error message and closed the popup
    bool done = false;

    xoj::popup::PopupWindowWrapper<XojMsgBox> popup(GTK_DIALOG(dialog), [&done](int) { done = true; });

    popup.show(defaultWindow);

    while (!done) {
        // Let the main loop run so the popup pops up and can be interacted with
        g_main_context_iteration(g_main_context_default(), true);
    }

    exit(exitCode);
}

void XojMsgBox::showPluginMessage(const std::string& pluginName, const std::string& msg, bool error) {
    auto header = std::string("Xournal JackMath Plugin «") + pluginName + "»";
    auto escapedHeader = xoj::util::OwnedCString::assumeOwnership(g_markup_escape_text(header.c_str(), -1));
    header = (error ? std::string("<b>Error in </b>") : "") + escapedHeader.get();

    showMarkupMessageToUser(nullptr, header, msg, error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_INFO);
}

auto XojMsgBox::askPluginQuestion(const std::string& pluginName, const std::string& msg,
                                  const std::vector<Button>& buttons, bool error) -> int {
    /*
     * Todo(gtk4): Remove this function entirely.
     */
    std::string header = "<i>Warning: The plugin interface function msgbox() is deprecated and will soon be removed. "
                         "Please adapt your plugin to use the function openDialog() instead</i>\n\n";
    header += (error ? std::string("<b>Error in </b>") : "") + std::string("Xournal JackMath Plugin «") + pluginName +
              "»";

    GtkWidget* dialog = gtk_message_dialog_new_with_markup(defaultWindow, GTK_DIALOG_MODAL,
                                                           error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_QUESTION,
                                                           GTK_BUTTONS_NONE, nullptr);

    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dialog), header.c_str());

    if (defaultWindow != nullptr) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), defaultWindow);
    }

    GValue val = G_VALUE_INIT;
    g_value_init(&val, G_TYPE_STRING);
    g_value_set_string(&val, msg.c_str());
    g_object_set_property(G_OBJECT(dialog), "secondary-text", &val);
    g_value_unset(&val);

    for (auto& b: buttons) {
        gtk_dialog_add_button(GTK_DIALOG(dialog), b.label.c_str(), b.response);
    }

    int res = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return res;
}

void XojMsgBox::replaceFileQuestion(GtkWindow* win, fs::path file,
                                    xoj::util::move_only_function<void(const fs::path&)> writeTofile) {
    if (!fs::exists(file)) {
        writeTofile(file);
        return;
    }

    GtkWidget* dialog = gtk_message_dialog_new(
            win, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s",
            FS(_F("The file {1} already exists! Do you want to replace it?") % file.filename().u8string()).c_str());
    if (win != nullptr) {
        gtk_window_set_transient_for(GTK_WINDOW(dialog), win);
    }
    gtk_dialog_add_button(GTK_DIALOG(dialog), _("Select another name"), GTK_RESPONSE_CANCEL);
    addClass(gtk_dialog_add_button(GTK_DIALOG(dialog), _("Replace"), GTK_RESPONSE_OK), "destructive-action");
    applyStyle(GTK_DIALOG(dialog), GTK_MESSAGE_QUESTION, Icon::WARNING);

    xoj::popup::PopupWindowWrapper<XojMsgBox> popup(
            GTK_DIALOG(dialog), [overwrite = std::move(writeTofile), file = std::move(file)](int response) mutable {
                if (response == GTK_RESPONSE_OK) {
                    overwrite(file);
                }
            });
    popup.show(win);
}

constexpr auto* XOJ_HELP = "https://xournalpp.github.io/community/help/";

void XojMsgBox::showHelp(GtkWindow* win) { openURL(win, XOJ_HELP); }

void XojMsgBox::openURL(GtkWindow* win, const char* url) {
#ifdef _WIN32
    // gvfs is not in MSYS repositories, so we can't use gtk_show_uri_on_window.
    // Instead, we use the native API.
    ShellExecute(nullptr, "open", url, nullptr, nullptr, SW_SHOW);
#else
    GError* error = nullptr;
    gtk_show_uri_on_window(win, url, gtk_get_current_event_time(), &error);

    if (error) {
        std::string msg = FS(_F("Unable to open the link: {1}\n{2}") % url % error->message);
        XojMsgBox::showErrorToUser(win, msg);

        g_error_free(error);
    }
#endif
}
