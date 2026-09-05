#include "mt-window-private.h"

gboolean
mt_window_run_editor_command(MtWindow *window,
                             MtPluginEditorCommand command)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter cursor;
    GtkTextIter target;
    gboolean moved;

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return FALSE;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    target = cursor;
    moved = FALSE;

    switch (command)
    {
        case MT_PLUGIN_EDITOR_MOVE_LEFT:
            moved = gtk_text_iter_backward_char(&target);
            break;
        case MT_PLUGIN_EDITOR_MOVE_RIGHT:
            moved = gtk_text_iter_forward_char(&target);
            break;
        case MT_PLUGIN_EDITOR_MOVE_UP:
            moved = gtk_text_iter_backward_line(&target);
            break;
        case MT_PLUGIN_EDITOR_MOVE_DOWN:
            moved = gtk_text_iter_forward_line(&target);
            break;
        case MT_PLUGIN_EDITOR_MOVE_LINE_START:
            gtk_text_iter_set_line_offset(&target, 0);
            moved = TRUE;
            break;
        case MT_PLUGIN_EDITOR_MOVE_LINE_END:
            gtk_text_iter_forward_to_line_end(&target);
            moved = TRUE;
            break;
        case MT_PLUGIN_EDITOR_MOVE_DOCUMENT_START:
            gtk_text_buffer_get_start_iter(buffer, &target);
            moved = TRUE;
            break;
        case MT_PLUGIN_EDITOR_MOVE_DOCUMENT_END:
            gtk_text_buffer_get_end_iter(buffer, &target);
            moved = TRUE;
            break;
        case MT_PLUGIN_EDITOR_MOVE_WORD_FORWARD:
            moved = gtk_text_iter_forward_word_end(&target);
            break;
        case MT_PLUGIN_EDITOR_MOVE_WORD_BACKWARD:
            moved = gtk_text_iter_backward_word_start(&target);
            break;
        case MT_PLUGIN_EDITOR_DELETE_FORWARD_CHAR:
            if (gtk_text_iter_forward_char(&target))
            {
                gtk_text_buffer_delete(buffer, &cursor, &target);
                return TRUE;
            }
            return FALSE;
        case MT_PLUGIN_EDITOR_DELETE_CURRENT_LINE:
            gtk_text_iter_set_line_offset(&cursor, 0);
            target = cursor;
            gtk_text_iter_forward_to_line_end(&target);
            if (!gtk_text_iter_is_end(&target))
            {
                gtk_text_iter_forward_char(&target);
            }
            gtk_text_buffer_delete(buffer, &cursor, &target);
            return TRUE;
        case MT_PLUGIN_EDITOR_YANK_LINE:
        {
            GtkTextIter line_start;
            GtkTextIter line_end;
            gchar *line_text;
            GdkClipboard *clipboard;

            gtk_text_iter_set_line_offset(&cursor, 0);
            line_start = cursor;
            line_end = line_start;
            gtk_text_iter_forward_to_line_end(&line_end);
            line_text = gtk_text_buffer_get_text(buffer, &line_start, &line_end, FALSE);
            clipboard = gdk_display_get_clipboard(window->display);
            gdk_clipboard_set_text(clipboard, line_text);
            g_free(line_text);
            return TRUE;
        }
        case MT_PLUGIN_EDITOR_CHANGE_LINE:
        {
            GtkTextIter line_start;
            GtkTextIter line_end;

            gtk_text_iter_set_line_offset(&cursor, 0);
            line_start = cursor;
            line_end = line_start;
            gtk_text_iter_forward_to_line_end(&line_end);
            gtk_text_buffer_begin_user_action(buffer);
            gtk_text_buffer_delete(buffer, &line_start, &line_end);
            gtk_text_buffer_end_user_action(buffer);
            return TRUE;
        }
        case MT_PLUGIN_EDITOR_PASTE:
        {
            GdkClipboard *clipboard;

            clipboard = gdk_display_get_clipboard(window->display);
            gdk_clipboard_read_text_async(clipboard,
                                          NULL,
                                          mt_window_paste_finished,
                                          window);
            return TRUE;
        }
        case MT_PLUGIN_EDITOR_UNDO:
            if (gtk_text_buffer_get_can_undo(buffer))
            {
                gtk_text_buffer_undo(buffer);
                return TRUE;
            }
            return FALSE;
        case MT_PLUGIN_EDITOR_REDO:
            if (gtk_text_buffer_get_can_redo(buffer))
            {
                gtk_text_buffer_redo(buffer);
                return TRUE;
            }
            return FALSE;
        case MT_PLUGIN_EDITOR_SAVE:
            mt_window_action_save(NULL, NULL, window);
            return TRUE;
        case MT_PLUGIN_EDITOR_SAVE_AND_CLOSE:
            if (mt_document_is_untitled(document))
            {
                mt_window_show_toast(window, _("Save an untitled document with :w before closing"));
                return FALSE;
            }
            if (mt_document_is_saving(document))
            {
                mt_window_show_toast(window, _("Saving is already in progress"));
                return FALSE;
            }
            {
                MtFileRequest *request;

                request = mt_file_request_new(window, document, NULL);
                mt_document_save_async(document,
                                       NULL,
                                       mt_window_save_and_close_finished,
                                       request);
                mt_window_show_toast(window, _("Saving before closing…"));
                return TRUE;
            }
        case MT_PLUGIN_EDITOR_CLOSE:
            mt_window_action_close(NULL, NULL, window);
            return TRUE;
        case MT_PLUGIN_EDITOR_FORCE_CLOSE:
            gtk_text_buffer_set_modified(buffer, FALSE);
            mt_document_remove_snapshot(document);
            if (document->page != NULL)
            {
                adw_tab_view_close_page(window->tab_view, document->page);
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }

    if (moved)
    {
        gtk_text_buffer_place_cursor(buffer, &target);
    }

    return moved;
}

void
mt_window_set_plugin_panel(MtWindow *window,
                           const gchar *id,
                           MtPluginPanelLocation location,
                           GtkWidget *panel)
{
    GtkStack *stack;
    GtkWidget *existing;

    if (window == NULL || window->disposed || id == NULL || panel == NULL)
    {
        return;
    }

    stack = mt_window_get_panel_stack(window, location);
    if (!GTK_IS_STACK(stack))
    {
        return;
    }
    existing = gtk_stack_get_child_by_name(stack, id);
    if (existing != NULL && existing != panel)
    {
        gtk_stack_remove(stack, existing);
    }

    if (gtk_widget_get_parent(panel) == NULL)
    {
        gtk_stack_add_named(stack, panel, id);
    }
    gtk_stack_set_visible_child_name(stack, id);
    if (stack == window->auxiliary_stack)
    {
        mt_window_auxiliary_visible(window, TRUE);
    }
    else
    {
        gtk_widget_set_visible(GTK_WIDGET(stack), TRUE);
    }

    if (location == MT_PLUGIN_PANEL_SIDEBAR)
    {
        gtk_paned_set_position(window->content_paned, 240);
    }
}

void
mt_window_hide_plugin_panel(MtWindow *window,
                            const gchar *id,
                            MtPluginPanelLocation location)
{
    GtkStack *stack;
    GtkWidget *panel;

    if (window == NULL || window->disposed || id == NULL)
    {
        return;
    }

    stack = mt_window_get_panel_stack(window, location);
    if (!GTK_IS_STACK(stack))
    {
        return;
    }
    panel = gtk_stack_get_child_by_name(stack, id);
    if (panel != NULL)
    {
        gtk_stack_remove(stack, panel);
    }

    if (g_list_model_get_n_items(G_LIST_MODEL(gtk_stack_get_pages(stack))) == 0)
    {
        if (stack == window->auxiliary_stack)
        {
            mt_window_auxiliary_visible(window, FALSE);
        }
        else
        {
            gtk_widget_set_visible(GTK_WIDGET(stack), FALSE);
        }
    }
}

void
mt_window_action_new(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    mt_window_new_document(user_data);
}

void
mt_window_action_open(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    GtkFileDialog *dialog;
    MtFileRequest *request;

    (void)action;
    (void)parameter;

    window = user_data;
    dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Open Files"));
    request = mt_file_request_new(window, NULL, dialog);
    gtk_file_dialog_open_multiple(dialog,
                                  GTK_WINDOW(window->window),
                                  NULL,
                                  mt_window_open_dialog_finished,
                                  request);
    g_object_unref(dialog);
}

void
mt_window_action_save(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    MtFileRequest *request;

    (void)action;
    (void)parameter;

    window = user_data;
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }

    if (mt_document_is_saving(document))
    {
        mt_window_show_toast(window, _("Saving is already in progress"));
        return;
    }

    if (mt_document_is_untitled(document))
    {
        mt_window_action_save_as(action, parameter, user_data);
        return;
    }

    request = mt_file_request_new(window, document, NULL);
    mt_document_save_async(document, NULL, mt_window_document_save_finished, request);
    mt_window_show_toast(window, _("Saving…"));
}

void
mt_window_action_save_as(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    GtkFileDialog *dialog;
    MtFileRequest *request;

    (void)action;
    (void)parameter;

    window = user_data;
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }

    if (mt_document_is_saving(document))
    {
        mt_window_show_toast(window, _("Saving is already in progress"));
        return;
    }

    dialog = gtk_file_dialog_new();
    gtk_file_dialog_set_title(dialog, _("Save As"));
    gtk_file_dialog_set_initial_name(dialog, mt_document_get_display_name(document));
    request = mt_file_request_new(window, document, dialog);
    gtk_file_dialog_save(dialog,
                         GTK_WINDOW(window->window),
                         NULL,
                         mt_window_save_dialog_finished,
                         request);
    g_object_unref(dialog);
}

void
mt_window_action_close(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    AdwTabPage *page;

    (void)action;
    (void)parameter;

    window = user_data;
    page = adw_tab_view_get_selected_page(window->tab_view);

    if (page != NULL)
    {
        adw_tab_view_close_page(window->tab_view, page);
    }
}

void
mt_window_action_find(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    mt_window_show_find_bar(user_data, FALSE);
}

void
mt_window_action_replace(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    mt_window_show_find_bar(user_data, TRUE);
}

void
mt_window_action_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    gdouble scale;

    (void)action;
    (void)parameter;

    window = user_data;
    scale = mt_settings_get_font_scale(window->settings);
    mt_settings_set_font_scale(window->settings, scale + 0.1);
    mt_window_apply_font_scale(window);
    mt_window_update_menu_zoom_label(window);
    mt_settings_save(window->settings);
    mt_window_show_toast(window, _("Zoomed in"));
}

void
mt_window_action_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    gdouble scale;

    (void)action;
    (void)parameter;

    window = user_data;
    scale = mt_settings_get_font_scale(window->settings);
    mt_settings_set_font_scale(window->settings, scale - 0.1);
    mt_window_apply_font_scale(window);
    mt_window_update_menu_zoom_label(window);
    mt_settings_save(window->settings);
    mt_window_show_toast(window, _("Zoomed out"));
}

void
mt_window_action_zoom_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;

    (void)action;
    (void)parameter;

    window = user_data;
    mt_settings_set_font_scale(window->settings, 1.0);
    mt_window_apply_font_scale(window);
    mt_window_update_menu_zoom_label(window);
    mt_settings_save(window->settings);
    mt_window_show_toast(window, _("Zoom reset to 100%"));
}

static const gchar *
mt_window_extension_display_name(const MtPluginInfo *info)
{
    if (info == NULL || info->id == NULL)
    {
        return "";
    }
    if (g_str_equal(info->id, "io.github.vellum.ai-completion")) return _("AI Code Assistant");
    if (g_str_equal(info->id, "io.github.vellum.timestamp")) return _("Timestamp");
    if (g_str_equal(info->id, "io.github.vellum.document-statistics")) return _("Document Statistics");
    if (g_str_equal(info->id, "io.github.vellum.link-check")) return _("Link Check");
    if (g_str_equal(info->id, "io.github.vellum.project-sidebar")) return _("Project Sidebar");
    if (g_str_equal(info->id, "io.github.vellum.dev-experience")) return _("Dev Experience");
    if (g_str_equal(info->id, "io.github.vellum.vim-mode")) return _("Vi Mode");
    if (g_str_equal(info->id, "io.github.vellum.screenshot")) return _("Editor Screenshot");
    if (g_str_equal(info->id, "io.github.vellum.welcome")) return _("Welcome Guide");
    return info->name != NULL ? info->name : "";
}

static const gchar *
mt_window_extension_display_description(const MtPluginInfo *info)
{
    if (info == NULL || info->id == NULL)
    {
        return "";
    }
    if (g_str_equal(info->id, "io.github.vellum.ai-completion")) return _("Inline completion and configurable code summaries through your AI service");
    if (g_str_equal(info->id, "io.github.vellum.timestamp")) return _("Insert the current local date and time");
    if (g_str_equal(info->id, "io.github.vellum.document-statistics")) return _("Show character, word and line counts for the current document");
    if (g_str_equal(info->id, "io.github.vellum.link-check")) return _("Check HTTP and HTTPS links in the current document");
    if (g_str_equal(info->id, "io.github.vellum.project-sidebar")) return _("Browse an explicitly selected project directory");
    if (g_str_equal(info->id, "io.github.vellum.dev-experience")) return _("Build, run and debug with compiler error underlines and breakpoints");
    if (g_str_equal(info->id, "io.github.vellum.vim-mode")) return _("Basic modal editing commands for Vi users");
    if (g_str_equal(info->id, "io.github.vellum.screenshot")) return _("Export the current editor content as an image");
    if (g_str_equal(info->id, "io.github.vellum.welcome")) return _("Interactive first-run guide with extension selection");
    return info->description != NULL ? info->description : "";
}

static void
mt_window_set_plain_row_title(AdwPreferencesRow *row, const gchar *text)
{
    gchar *escaped;

    escaped = g_markup_escape_text(text != NULL ? text : "", -1);
    adw_preferences_row_set_title(row, escaped);
    g_free(escaped);
}

static void
mt_window_set_plain_action_subtitle(AdwActionRow *row, const gchar *text)
{
    gchar *escaped;

    escaped = g_markup_escape_text(text != NULL ? text : "", -1);
    adw_action_row_set_subtitle(row, escaped);
    g_free(escaped);
}

static gboolean
mt_window_extension_enabled_state_set(GtkSwitch *toggle,
                                      gboolean enabled,
                                      gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    guint index;
    GError *error;

    window = user_data;
    manager = window->plugin_manager;
    index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(toggle), "vellum-extension-index"));
    error = NULL;
    if (manager != NULL && mt_plugin_manager_set_enabled(manager, index, enabled, &error))
    {
        return FALSE;
    }

    if (error != NULL)
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to change extension state: %s"), error->message);
        mt_window_show_toast(window, message);
        g_free(message);
        g_clear_error(&error);
    }
    return TRUE;
}

/* —— 扩展市场：核心内置模块，浏览多个扩展源并安装/卸载 —— */

static void
mt_window_reopen_extensions(MtWindow *window)
{
    if (window->extensions_window != NULL && GTK_IS_WIDGET(window->extensions_window))
    {
        gtk_window_destroy(GTK_WINDOW(window->extensions_window));
        window->extensions_window = NULL;
    }
    mt_window_action_extensions(NULL, NULL, window);
}

static void
mt_window_market_refresh_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    GError *error;

    (void)source;
    window = user_data;
    manager = window->plugin_manager;
    error = NULL;
    if (mt_plugin_manager_marketplace_refresh_finish(manager, result, &error))
    {
        mt_window_show_toast(window, _("Extension catalog refreshed"));
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to refresh extension catalog: %s"),
                                  error != NULL ? error->message : _("Unknown error"));
        mt_window_show_toast(window, message);
        g_free(message);
        g_clear_error(&error);
    }
    mt_window_reopen_extensions(window);
}

static void
mt_window_market_refresh_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;

    (void)button;
    window = user_data;
    manager = window->plugin_manager;
    mt_plugin_manager_marketplace_refresh_async(manager, NULL,
                                                mt_window_market_refresh_ready,
                                                window);
}

static void
mt_window_market_install_ready(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    GError *error;

    (void)source;
    window = user_data;
    manager = window->plugin_manager;
    error = NULL;
    if (mt_plugin_manager_marketplace_install_finish(manager, result, &error))
    {
        mt_window_show_toast(window, _("Extension installed from the marketplace"));
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to install extension: %s"),
                                  error != NULL ? error->message : _("Unknown error"));
        mt_window_show_toast(window, message);
        g_free(message);
        g_clear_error(&error);
    }
    mt_window_reopen_extensions(window);
}

static void
mt_window_market_install_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    MtMarketplaceEntry *entry;
    gboolean prefer_source;

    window = user_data;
    manager = window->plugin_manager;
    entry = g_object_get_data(G_OBJECT(button), "vellum-market-entry");
    if (entry == NULL)
    {
        return;
    }
    prefer_source = g_object_get_data(G_OBJECT(button), "vellum-market-source") != NULL;
    mt_plugin_manager_marketplace_install_async(manager, entry, prefer_source, NULL,
                                                mt_window_market_install_ready,
                                                window);
}

static void
mt_window_market_uninstall_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    MtMarketplaceEntry *entry;
    GError *error;

    window = user_data;
    manager = window->plugin_manager;
    entry = g_object_get_data(G_OBJECT(button), "vellum-market-entry");
    if (entry == NULL)
    {
        return;
    }
    error = NULL;
    if (mt_plugin_manager_marketplace_uninstall(manager, entry, &error))
    {
        mt_window_show_toast(window, _("Extension uninstalled"));
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to uninstall extension: %s"),
                                  error != NULL ? error->message : _("Unknown error"));
        mt_window_show_toast(window, message);
        g_free(message);
        g_clear_error(&error);
    }
    mt_window_reopen_extensions(window);
}

void
mt_window_action_extensions(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    MtPluginManager *manager;
    AdwPreferencesWindow *extensions_window;
    AdwPreferencesPage *page;
    AdwPreferencesGroup *group;
    GtkWidget *import_button;
    guint count;
    guint index;

    (void)action;
    (void)parameter;

    window = user_data;
    manager = window->plugin_manager;
    if (window->extensions_window != NULL && GTK_IS_WIDGET(window->extensions_window))
    {
        gtk_window_destroy(GTK_WINDOW(window->extensions_window));
        window->extensions_window = NULL;
    }
    extensions_window = ADW_PREFERENCES_WINDOW(adw_preferences_window_new());
    window->extensions_window = GTK_WIDGET(extensions_window);
    gtk_window_set_transient_for(GTK_WINDOW(extensions_window), GTK_WINDOW(window->window));
    gtk_window_set_modal(GTK_WINDOW(extensions_window), TRUE);
    gtk_window_set_title(GTK_WINDOW(extensions_window), _("Extensions"));
    page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("Loaded Extensions"));
    if (manager == NULL)
    {
        adw_preferences_group_set_description(group,
                                              _("Extensions are disabled. Enable them in Preferences and restart Vellum to load or manage extensions."));
    }
    else
    {
        adw_preferences_group_set_description(group,
                                              _("Extensions can be disabled without deleting them. Any extension can be removed: user extensions are deleted from disk, built-in extensions are hidden until vellum is reinstalled. Native modules and source packages must come from trusted sources."));
    }
    import_button = gtk_button_new_with_label(_("Import"));
    gtk_widget_set_valign(import_button, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(import_button, manager != NULL);
    adw_preferences_group_set_header_suffix(group, import_button);
    g_signal_connect(import_button,
                     "clicked",
                     G_CALLBACK(mt_window_extension_import_clicked),
                     window);

    count = manager != NULL ? mt_plugin_manager_get_count(manager) : 0;
    if (count == 0)
    {
        AdwActionRow *row;

        row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                      manager == NULL ?
                                      _("Extension loading is disabled for this session") :
                                      _("No extensions are currently loaded"));
        adw_preferences_group_add(group, GTK_WIDGET(row));
    }

    for (index = 0; index < count; index++)
    {
        const MtPluginInfo *info;
        AdwActionRow *row;
        GtkWidget *enabled_switch;
        GtkWidget *export_button;
        GtkWidget *configure_button;
        GtkWidget *delete_button;
        GtkWidget *action_box;
        gchar *subtitle;

        info = mt_plugin_manager_get_info(manager, index);
        if (info == NULL)
        {
            continue;
        }

        row = ADW_ACTION_ROW(adw_action_row_new());
        mt_window_set_plain_row_title(ADW_PREFERENCES_ROW(row),
                                      mt_window_extension_display_name(info));
        subtitle = g_strdup_printf("%s · %s",
                                   mt_window_extension_display_description(info),
                                   info->version != NULL ? info->version : "");
        mt_window_set_plain_action_subtitle(row, subtitle);
        action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_widget_set_valign(action_box, GTK_ALIGN_CENTER);
        enabled_switch = gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(enabled_switch),
                              mt_plugin_manager_is_enabled(manager, index));
        g_object_set_data(G_OBJECT(enabled_switch),
                          "vellum-extension-index",
                          GUINT_TO_POINTER(index));
        g_signal_connect(enabled_switch,
                         "state-set",
                         G_CALLBACK(mt_window_extension_enabled_state_set),
                         window);
        gtk_widget_set_valign(enabled_switch, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(enabled_switch, _("Enable extension"));
        gtk_box_append(GTK_BOX(action_box), enabled_switch);
        export_button = gtk_button_new_from_icon_name("document-save-symbolic");
        gtk_widget_add_css_class(export_button, "flat");
        gtk_widget_set_valign(export_button, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(export_button, _("Export extension"));
        g_object_set_data(G_OBJECT(export_button),
                          "vellum-extension-index",
                          GUINT_TO_POINTER(index));
        g_signal_connect(export_button,
                         "clicked",
                         G_CALLBACK(mt_window_extension_export_clicked),
                         window);
        gtk_box_append(GTK_BOX(action_box), export_button);

        if (mt_plugin_manager_has_configure(manager, index))
        {
            configure_button = gtk_button_new_from_icon_name("emblem-system-symbolic");
            gtk_widget_add_css_class(configure_button, "flat");
            gtk_widget_set_valign(configure_button, GTK_ALIGN_CENTER);
            gtk_widget_set_tooltip_text(configure_button, _("Configure extension"));
            gtk_widget_set_sensitive(configure_button,
                                     mt_plugin_manager_is_enabled(manager, index));
            g_object_set_data(G_OBJECT(configure_button),
                              "vellum-extension-index",
                              GUINT_TO_POINTER(index));
            g_signal_connect(configure_button,
                             "clicked",
                             G_CALLBACK(mt_window_extension_configure_clicked),
                             window);
            gtk_box_append(GTK_BOX(action_box), configure_button);
        }

        delete_button = gtk_button_new_from_icon_name("user-trash-symbolic");
        gtk_widget_add_css_class(delete_button, "flat");
        gtk_widget_add_css_class(delete_button, "destructive-action");
        gtk_widget_set_valign(delete_button, GTK_ALIGN_CENTER);
        gtk_widget_set_tooltip_text(delete_button, _("Delete extension"));
        g_object_set_data(G_OBJECT(delete_button),
                          "vellum-extension-index",
                          GUINT_TO_POINTER(index));
        g_signal_connect(delete_button,
                         "clicked",
                         G_CALLBACK(mt_window_extension_delete_clicked),
                         window);
        gtk_box_append(GTK_BOX(action_box), delete_button);

        adw_action_row_add_suffix(row, action_box);
        adw_preferences_group_add(group, GTK_WIDGET(row));
        g_free(subtitle);
    }

    adw_preferences_page_add(page, group);

    {
        /* 扩展市场：核心内置模块，不可卸载；列出多个扩展源的目录。 */
        AdwPreferencesGroup *market_group;
        GPtrArray *marketplace;
        GtkWidget *refresh_button;
        guint market_index;

        market_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
        adw_preferences_group_set_title(market_group, _("Extension Marketplace"));
        adw_preferences_group_set_description(market_group,
                                              _("Browse extensions from the configured sources (GitHub release by default). Binary and source packages are both offered; source packages need make, cc, pkg-config and the GTK/GLib development packages listed in the package."));
        refresh_button = gtk_button_new_with_label(_("Refresh"));
        gtk_widget_set_valign(refresh_button, GTK_ALIGN_CENTER);
        adw_preferences_group_set_header_suffix(market_group, refresh_button);
        g_signal_connect(refresh_button,
                         "clicked",
                         G_CALLBACK(mt_window_market_refresh_clicked),
                         window);

        marketplace = mt_plugin_manager_get_marketplace(manager);
        if (marketplace == NULL || marketplace->len == 0)
        {
            AdwActionRow *row;

            row = ADW_ACTION_ROW(adw_action_row_new());
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row),
                                          _("Extension catalog not loaded yet"));
            adw_action_row_set_subtitle(ADW_ACTION_ROW(row),
                                        _("Press Refresh to download the extension catalog from the configured sources."));
            adw_preferences_group_add(market_group, GTK_WIDGET(row));
        }
        for (market_index = 0;
             marketplace != NULL && market_index < marketplace->len;
             market_index++)
        {
            MtMarketplaceEntry *entry;
            AdwActionRow *row;
            GtkWidget *button;
            GtkWidget *source_button;
            GtkWidget *action_box;
            gchar *subtitle;
            gboolean installed;

            entry = g_ptr_array_index(marketplace, market_index);
            installed = mt_plugin_manager_has_plugin(manager, entry->id);
            row = ADW_ACTION_ROW(adw_action_row_new());
            mt_window_set_plain_row_title(ADW_PREFERENCES_ROW(row),
                                          entry->name != NULL ? entry->name : entry->id);
            subtitle = g_strdup_printf("%s · %s%s",
                                       entry->description != NULL ? entry->description : "",
                                       entry->version != NULL ? entry->version : "",
                                       (entry->source != NULL && *entry->source != '\0') ?
                                       _(" · source available") : "");
            mt_window_set_plain_action_subtitle(row, subtitle);
            g_free(subtitle);

            action_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
            gtk_widget_set_valign(action_box, GTK_ALIGN_CENTER);
            button = gtk_button_new_with_label(installed ? _("Uninstall") : _("Install"));
            gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
            gtk_widget_add_css_class(button, installed ? "destructive-action" : "suggested-action");
            g_object_set_data(G_OBJECT(button), "vellum-market-entry", entry);
            if (installed)
            {
                g_signal_connect(button,
                                 "clicked",
                                 G_CALLBACK(mt_window_market_uninstall_clicked),
                                 window);
            }
            else
            {
                g_signal_connect(button,
                                 "clicked",
                                 G_CALLBACK(mt_window_market_install_clicked),
                                 window);
            }
            gtk_box_append(GTK_BOX(action_box), button);

            if (!installed && entry->source != NULL && *entry->source != '\0')
            {
                source_button = gtk_button_new_with_label(_("Source"));
                gtk_widget_set_valign(source_button, GTK_ALIGN_CENTER);
                gtk_widget_set_tooltip_text(source_button,
                                            _("Install from source (requires make, cc, pkg-config and the listed development packages)"));
                g_object_set_data(G_OBJECT(source_button), "vellum-market-entry", entry);
                g_object_set_data(G_OBJECT(source_button), "vellum-market-source", GINT_TO_POINTER(1));
                g_signal_connect(source_button,
                                 "clicked",
                                 G_CALLBACK(mt_window_market_install_clicked),
                                 window);
                gtk_box_append(GTK_BOX(action_box), source_button);
            }

            adw_action_row_add_suffix(row, action_box);
            adw_preferences_group_add(market_group, GTK_WIDGET(row));
        }
        adw_preferences_page_add(page, market_group);

        /* 源码安装环境说明：与欢迎引导同款，按发行版列出依赖 */
        {
            AdwPreferencesGroup *source_group;
            GtkWidget *label;

            source_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
            adw_preferences_group_set_title(source_group, _("Source Build Environment"));
            adw_preferences_group_set_description(source_group,
                                                  _("Source packages are built locally with make, cc and pkg-config. Install the required development packages first:"));
            label = gtk_label_new(NULL);
            gtk_label_set_selectable(GTK_LABEL(label), TRUE);
            gtk_label_set_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_xalign(GTK_LABEL(label), 0.0);
            gtk_widget_add_css_class(label, "monospace");
            gtk_label_set_text(GTK_LABEL(label),
                               "Debian/Ubuntu:\n"
                               "  sudo apt install make gcc pkg-config libgtk-4-dev libadwaita-1-dev libgtksourceview-5-dev libsoup-3.0-dev libjson-glib-dev\n"
                               "Fedora/RHEL (Red Hat):\n"
                               "  sudo dnf install make gcc pkgconf-pkg-config gtk4-devel libadwaita-devel gtksourceview5-devel libsoup-devel json-glib-devel\n"
                               "Arch Linux:\n"
                               "  sudo pacman -S make gcc pkgconf gtk4 libadwaita gtksourceview5 libsoup json-glib\n"
                               "openSUSE:\n"
                               "  sudo zypper install make gcc pkgconf gtk4-devel libadwaita-devel gtksourceview5-devel libsoup-devel json-glib-devel");
            adw_preferences_group_add(source_group, label);
            adw_preferences_page_add(page, source_group);
        }

        {
            AdwPreferencesGroup *sources_group;
            GtkWidget *add_button;

            sources_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
            adw_preferences_group_set_title(sources_group, _("Extension Sources"));
            adw_preferences_group_set_description(sources_group,
                                                  _("Manage the catalogs Vellum checks for extensions. "
                                                    "The official source can be turned off; additional sources are checked in order."));
            add_button = gtk_button_new_with_label(_("Add Source…"));
            gtk_widget_set_valign(add_button, GTK_ALIGN_CENTER);
            g_object_set_data(G_OBJECT(add_button), "vellum-sources-group", sources_group);
            g_signal_connect(add_button,
                             "clicked",
                             G_CALLBACK(mt_window_source_add_clicked),
                             window);
            adw_preferences_group_set_header_suffix(sources_group, add_button);
            adw_preferences_page_add(page, sources_group);
            mt_window_sources_group_rebuild(window, sources_group);
        }
    }

    adw_preferences_window_add(extensions_window, page);
    gtk_window_present(GTK_WINDOW(extensions_window));
}
