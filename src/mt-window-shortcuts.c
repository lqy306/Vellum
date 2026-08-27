/*
 * mt-window-shortcuts.c
 * 主窗口的快捷键设置界面；与编辑、文件和偏好职责解耦。
 */

#include "mt-window-private.h"

#include <glib/gi18n.h>

typedef struct _MtShortcutRequest
{
    MtWindow *window;
    gchar *detailed_action_name;
    GtkButton *button;
    GtkWindow *dialog;
} MtShortcutRequest;

static void
mt_window_shortcut_request_free(MtShortcutRequest *request)
{
    if (request != NULL)
    {
        g_free(request->detailed_action_name);
        g_free(request);
    }
}

static void
mt_window_shortcut_button_destroyed(gpointer user_data, GClosure *closure)
{
    (void)closure;
    mt_window_shortcut_request_free(user_data);
}

static gboolean
mt_window_shortcut_key_pressed(GtkEventControllerKey *controller,
                               guint keyval,
                               guint keycode,
                               GdkModifierType state,
                               gpointer user_data)
{
    MtShortcutRequest *request;
    GdkModifierType modifiers;
    gchar *accelerator;
    gchar *label;
    const gchar *accelerators[2];
    GtkApplication *application;

    (void)controller;
    (void)keycode;
    request = user_data;
    modifiers = state & gtk_accelerator_get_default_mod_mask();

    if (keyval == GDK_KEY_Escape && modifiers == 0)
    {
        gtk_window_destroy(request->dialog);
        return TRUE;
    }
    if (!gtk_accelerator_valid(keyval, modifiers))
    {
        return TRUE;
    }

    accelerator = gtk_accelerator_name(keyval, modifiers);
    label = gtk_accelerator_get_label(keyval, modifiers);
    mt_settings_set_shortcut(request->window->settings,
                             request->detailed_action_name,
                             accelerator);
    mt_settings_save(request->window->settings);
    accelerators[0] = accelerator;
    accelerators[1] = NULL;
    application = GTK_APPLICATION(gtk_window_get_application(GTK_WINDOW(request->window->window)));
    gtk_application_set_accels_for_action(application,
                                          request->detailed_action_name,
                                          accelerators);
    gtk_button_set_label(request->button, label);
    gtk_widget_set_tooltip_text(GTK_WIDGET(request->button), accelerator);
    g_free(label);
    g_free(accelerator);
    gtk_window_destroy(request->dialog);

    return TRUE;
}

static void
mt_window_shortcut_edit_clicked(GtkButton *button, gpointer user_data)
{
    MtShortcutRequest *source_request;
    MtShortcutRequest *request;
    GtkWidget *content;
    GtkWidget *heading;
    GtkWidget *description;
    GtkEventController *controller;

    source_request = user_data;
    request = g_new0(MtShortcutRequest, 1);
    request->window = source_request->window;
    request->detailed_action_name = g_strdup(source_request->detailed_action_name);
    request->button = GTK_BUTTON(button);
    request->dialog = GTK_WINDOW(adw_window_new());
    gtk_window_set_title(request->dialog, _("Set Keyboard Shortcut"));
    gtk_window_set_transient_for(request->dialog, GTK_WINDOW(request->window->window));
    gtk_window_set_modal(request->dialog, TRUE);
    gtk_window_set_default_size(request->dialog, 380, 150);
    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(content, 24);
    gtk_widget_set_margin_bottom(content, 24);
    gtk_widget_set_margin_start(content, 24);
    gtk_widget_set_margin_end(content, 24);
    heading = gtk_label_new(_("Press a new shortcut"));
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
    gtk_widget_add_css_class(heading, "title-3");
    description = gtk_label_new(_("Press Escape to cancel. The change applies immediately and is saved for the next start."));
    gtk_label_set_xalign(GTK_LABEL(description), 0.0);
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_widget_add_css_class(description, "dim-label");
    gtk_box_append(GTK_BOX(content), heading);
    gtk_box_append(GTK_BOX(content), description);
    adw_window_set_content(ADW_WINDOW(request->dialog), content);
    controller = gtk_event_controller_key_new();
    g_signal_connect(controller,
                     "key-pressed",
                     G_CALLBACK(mt_window_shortcut_key_pressed),
                     request);
    gtk_widget_add_controller(GTK_WIDGET(request->dialog), controller);
    g_object_set_data_full(G_OBJECT(request->dialog),
                           "vellum-shortcut-request",
                           request,
                           (GDestroyNotify)mt_window_shortcut_request_free);
    gtk_window_present(request->dialog);
}

static void
mt_window_add_shortcut_row(AdwPreferencesGroup *group,
                           const gchar *title,
                           const gchar *detailed_action_name,
                           const gchar *fallback,
                           MtWindow *window)
{
    AdwActionRow *row;
    GtkWidget *button;
    MtShortcutRequest *request;
    const gchar *custom;
    const gchar *label;

    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    custom = mt_settings_get_shortcut(window->settings, detailed_action_name);
    label = custom != NULL ? custom : fallback;
    button = gtk_button_new_with_label(label);
    gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(button, _("Change shortcut"));
    request = g_new0(MtShortcutRequest, 1);
    request->window = window;
    request->detailed_action_name = g_strdup(detailed_action_name);
    request->button = GTK_BUTTON(button);
    g_signal_connect_data(button,
                          "clicked",
                          G_CALLBACK(mt_window_shortcut_edit_clicked),
                          request,
                          mt_window_shortcut_button_destroyed,
                          0);
    adw_action_row_add_suffix(row, button);
    adw_preferences_group_add(group, GTK_WIDGET(row));
}

void
mt_window_action_shortcuts(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    AdwPreferencesWindow *shortcuts;
    AdwPreferencesPage *page;
    AdwPreferencesGroup *document_group;
    AdwPreferencesGroup *editing_group;
    AdwPreferencesGroup *extension_group;

    (void)action;
    (void)parameter;
    window = user_data;
    shortcuts = ADW_PREFERENCES_WINDOW(adw_preferences_window_new());
    gtk_window_set_transient_for(GTK_WINDOW(shortcuts), GTK_WINDOW(window->window));
    gtk_window_set_modal(GTK_WINDOW(shortcuts), TRUE);
    gtk_window_set_title(GTK_WINDOW(shortcuts), _("Keyboard Shortcuts"));
    page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());

    document_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(document_group, _("Document"));
    mt_window_add_shortcut_row(document_group, _("New document"), "win.new", "Ctrl+N", window);
    mt_window_add_shortcut_row(document_group, _("Open files"), "win.open", "Ctrl+O", window);
    mt_window_add_shortcut_row(document_group, _("Save"), "win.save", "Ctrl+S", window);
    mt_window_add_shortcut_row(document_group, _("Save As"), "win.save-as", "Ctrl+Shift+S", window);
    mt_window_add_shortcut_row(document_group, _("Close document"), "win.close", "Ctrl+W", window);

    editing_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(editing_group, _("Editing"));
    mt_window_add_shortcut_row(editing_group, _("Find"), "win.find", "Ctrl+F", window);
    mt_window_add_shortcut_row(editing_group, _("Find and replace"), "win.replace", "Ctrl+H", window);
    mt_window_add_shortcut_row(editing_group, _("Zoom in"), "win.zoom-in", "Ctrl++", window);
    mt_window_add_shortcut_row(editing_group, _("Zoom out"), "win.zoom-out", "Ctrl+-", window);
    mt_window_add_shortcut_row(editing_group, _("Reset zoom"), "win.zoom-reset", "Ctrl+0", window);
    mt_window_add_shortcut_row(editing_group, _("AI completion"), "app.ai-complete", "Ctrl+Shift+Space", window);
    mt_window_add_shortcut_row(editing_group, _("Summarize code with AI"), "app.ai-summarize", "Ctrl+Alt+S", window);

    extension_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(extension_group, _("Optional Extensions"));
    mt_window_add_shortcut_row(extension_group, _("Insert timestamp"), "app.timestamp", "Ctrl+Shift+T", window);
    mt_window_add_shortcut_row(extension_group, _("Document statistics"), "app.document-statistics", "Ctrl+Shift+W", window);
    mt_window_add_shortcut_row(extension_group, _("Test links"), "app.link-check", "Ctrl+Shift+L", window);
    mt_window_add_shortcut_row(extension_group, _("Project sidebar"), "app.project-sidebar", "Ctrl+Shift+P", window);
    mt_window_add_shortcut_row(extension_group, _("Build"), "app.build", "F9", window);
    mt_window_add_shortcut_row(extension_group, _("Run"), "app.run", "F10", window);
    mt_window_add_shortcut_row(extension_group, _("Build and run"), "app.build-and-run", "F11", window);

    adw_preferences_page_add(page, document_group);
    adw_preferences_page_add(page, editing_group);
    adw_preferences_page_add(page, extension_group);
    adw_preferences_window_add(shortcuts, page);
    gtk_window_present(GTK_WINDOW(shortcuts));
}
