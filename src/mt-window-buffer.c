#include "mt-window-private.h"

void
mt_window_apply_editor_preferences(MtWindow *window)
{
    gint count;
    gint index;

    count = adw_tab_view_get_n_pages(window->tab_view);
    for (index = 0; index < count; index++)
    {
        AdwTabPage *page;
        MtDocument *document;

        page = adw_tab_view_get_nth_page(window->tab_view, index);
        document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);
        if (document != NULL)
        {
            mt_document_apply_editor_settings(document, window->settings);
        }
    }
}

/* 与 GNOME Text Editor 状态栏一致：等宽数字、表格数字特性。 */
PangoAttrList *
mt_window_statusbar_attrs(void)
{
    PangoAttrList *attrs;

    attrs = pango_attr_list_new();
    pango_attr_list_insert(attrs, pango_attr_font_features_new("tnum"));
    return attrs;
}

void
mt_window_apply_editor_style(MtWindow *window)
{
    gchar *css;
    const gchar *family;
    gdouble scale;
    gint64 start;
    start = g_get_monotonic_time();
    family = mt_settings_get_custom_font(window->settings) ?
             mt_settings_get_font_family(window->settings) : "Monospace";
    scale = mt_settings_get_font_scale(window->settings);

    /* 编辑器表面与 token 色完全交给 GtkSourceView 当前选择的方案。 */
    css = g_strdup_printf(
        "textview.vellum-editor { font-family: %s; font-size: %.2fpt; } "
        "box.vellum-statusbar { font-family: monospace; min-width: 100px; padding: 3px 12px; border-top: 1px solid @borders; } "
        "button.vellum-theme-card { padding: 0; border-radius: 12px; border: 1px solid alpha(@borders, .70); background: alpha(@theme_bg_color, .45); } "
        "button.vellum-theme-card:hover { border-color: alpha(@accent_bg_color, .72); background: alpha(@accent_bg_color, .08); } "
        "button.vellum-theme-card.vellum-theme-selected { border: 2px solid @accent_bg_color; box-shadow: 0 0 0 2px alpha(@accent_bg_color, .16); } "
        "frame.vellum-theme-preview-frame { border-radius: 7px; border: 1px solid alpha(@borders, .72); } "
        "label.vellum-theme-name { font-weight: 600; } "
        "scrolledwindow.vellum-properties-drawer { border-left: 1px solid alpha(@borders, .82); background: @sidebar_bg_color; } "
        "preferencespage.vellum-properties { background: @sidebar_bg_color; } "
        "widget.vellum-overview-map { min-width: 104px; border-left: 1px solid alpha(@borders, .70); background-color: alpha(@view_bg_color, .88); padding-top: 5px; padding-bottom: 5px; } "
        "widget.vellum-overview-map slider, widget.vellum-overview-map selection { background-color: alpha(@accent_bg_color, .30); border: 1px solid alpha(@accent_bg_color, .72); } "
        "label.vellum-inline-completion { opacity: .58; color: alpha(@theme_fg_color, .82); font-family: monospace; font: inherit; } "
         "box.vellum-header-title { min-width: 0; } "
         "box.vellum-header-title label { min-width: 0; } "
         "label.vellum-header-modified { color: @accent_color; font-weight: 700; } "
         "grid.searchbar button.flat { min-width: 24px; min-height: 24px; margin: 0; padding: 3px; } "
         "grid.searchbar button.flat.circular { border-radius: 9999px; } "
         "box.vellum-external-change { min-height: 68px; background: linear-gradient(to bottom, #9a7114, #76530d); border-top: 1px solid alpha(#f6d32d, .34); border-bottom: 1px solid alpha(@borders, .82); } "
         "label.vellum-external-title { color: #ffffff; font-weight: 700; font-size: 1.08em; } "
         "label.vellum-external-description { color: alpha(#ffffff, .88); } "
         "box.vellum-external-change button.suggested-action { font-weight: 700; } "
         "row.vellum-destructive label.title { color: @error_color; font-weight: 600; } "
         "row.vellum-destructive label.subtitle { color: alpha(@error_color, .82); } "
         "frame.vellum-onboarding-target { border: 2px solid #e01b24; border-radius: 10px; background: alpha(#e01b24, .06); box-shadow: 0 0 0 4px alpha(#e01b24, .12); } "
         "button.vellum-properties-toggle { min-width: 24px; padding-left: 2px; padding-right: 2px; } "
         "preferencespage.vellum-properties > scrolledwindow > viewport > clamp > box { margin: 4px 8px 8px 8px; border-spacing: 10px; } "
         "stack.vellum-auxiliary { min-width: 0; } "
         "popover button.pill.flat { padding: 0; } "
         "box.vellum-menu-theme { margin: 9px; } "
         "checkbutton.theme-selector { padding: 0; min-width: 44px; min-height: 44px; background-clip: content-box; border-radius: 9999px; box-shadow: inset 0 0 0 1px @borders; } "
         "checkbutton.theme-selector:checked { box-shadow: inset 0 0 0 2px @theme_selected_bg_color; } "
         "checkbutton.theme-selector.follow { background-image: linear-gradient(to bottom right, #f6f5f4 49.9%%, #242424 50.1%%); } "
         "checkbutton.theme-selector.light { background-color: #f6f5f4; } "
         "checkbutton.theme-selector.dark { background-color: #242424; } "
         "checkbutton.theme-selector radio { -gtk-icon-source: none; border: none; background: none; box-shadow: none; min-width: 12px; min-height: 12px; padding: 2px; transform: translate(27px, 14px); } "
         "checkbutton.theme-selector radio:checked { -gtk-icon-source: -gtk-icontheme(\"object-select-symbolic\"); background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; } ",
        family,
        11.0 * scale);

    gtk_css_provider_load_from_string(window->css_provider, css);
    g_free(css);
    mt_timing_log_duration("apply-editor-style", start);
    mt_window_apply_source_scheme(window);
}

void
mt_window_update_language_label(MtWindow *window)
{
    MtDocument *document;
    GtkSourceLanguage *language;

    if (window == NULL || window->disposed || !GTK_IS_LABEL(window->language_label))
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        gtk_label_set_text(window->language_label, "");
        return;
    }

    language = gtk_source_buffer_get_language(mt_document_get_buffer(document));
    gtk_label_set_text(window->language_label,
                       language != NULL ? gtk_source_language_get_name(language) : _("Plain Text"));
}

gboolean
mt_window_language_update_cb(gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    if (window == NULL || window->disposed)
    {
        return G_SOURCE_REMOVE;
    }
    window->language_source_id = 0;
    mt_window_update_language_label(window);

    return G_SOURCE_REMOVE;
}

void
mt_window_schedule_language_update(MtWindow *window)
{
    if (window->language_source_id != 0)
    {
        g_source_remove(window->language_source_id);
    }

    window->language_source_id = g_timeout_add(520,
                                               mt_window_language_update_cb,
                                               window);
}

void
mt_window_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;

    window = g_object_get_data(G_OBJECT(buffer), "vellum-window");
    document = user_data;

    if (window != NULL && !window->disposed)
    {
        if (document != NULL && document == window->inline_completion_document)
        {
            mt_window_clear_inline_completion(window);
    mt_window_clear_inline_diff(window);
        }
        mt_window_schedule_language_update(window);
        mt_window_properties_schedule_refresh(window);
    }
}

static guint
mt_window_count_changed_lines(const gchar *text, gint length)
{
    guint lines;
    gint index;

    lines = 1;
    for (index = 0; index < length; index++)
    {
        if (text[index] == '\n')
        {
            lines++;
        }
    }

    return lines;
}

void
mt_window_notify_document_change(GtkTextBuffer *buffer,
                                 MtDocument *document,
                                 guint changed_lines)
{
    MtWindow *window;

    window = g_object_get_data(G_OBJECT(buffer), "vellum-window");
    if (window == NULL || window->disposed || window->plugin_manager == NULL ||
        document == NULL || document->loader != NULL || document->saving ||
        document != mt_window_get_current_document(window) ||
        gtk_source_buffer_get_language(document->buffer) == NULL)
    {
        return;
    }

    mt_plugin_manager_notify_document_changed((MtPluginManager *)window->plugin_manager,
                                              MAX(changed_lines, 1));
}

void
mt_window_buffer_inserted(GtkTextBuffer *buffer,
                          GtkTextIter *location,
                          gchar *text,
                          gint length,
                          gpointer user_data)
{
    (void)location;
    mt_window_notify_document_change(buffer,
                                     user_data,
                                     mt_window_count_changed_lines(text, length));
}

void
mt_window_buffer_deleted(GtkTextBuffer *buffer,
                         GtkTextIter *start,
                         GtkTextIter *end,
                         gpointer user_data)
{
    guint lines;

    lines = (guint)(ABS(gtk_text_iter_get_line(end) - gtk_text_iter_get_line(start)) + 1);
    mt_window_notify_document_change(buffer, user_data, lines);
}

void
mt_window_update_document_title(MtDocument *document)
{
    gchar *title;

    if (document->page == NULL)
    {
        return;
    }

    title = g_strdup_printf("%s%s",
                            mt_document_is_modified(document) ? "• " : "",
                            mt_document_get_display_name(document));
    adw_tab_page_set_title(document->page, title);
    adw_tab_page_set_needs_attention(document->page, mt_document_is_modified(document));
    g_free(title);
}

/* 保持标题栏的文档身份区与所选标签同步。原版 GNOME Text Editor
 * 将文档名、所在目录和未保存状态放在标题栏中央；Vellum 沿用这一
 * 信息层级，但使用自身的数据模型与控件实现。 */
void
mt_window_update_header_title(MtWindow *window)
{
    MtDocument *document;
    GFile *file;
    GFile *parent;
    gchar *subtitle;

    if (window == NULL || window->disposed ||
        !GTK_IS_LABEL(window->header_title_label) ||
        !GTK_IS_LABEL(window->header_subtitle_label))
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        gtk_label_set_text(window->header_title_label, "Vellum");
        gtk_label_set_text(window->header_subtitle_label, "");
        if (window->header_modified_indicator != NULL)
        {
            gtk_widget_set_visible(window->header_modified_indicator, FALSE);
        }
        return;
    }

    gtk_label_set_text(window->header_title_label,
                       mt_document_get_display_name(document));
    if (window->header_modified_indicator != NULL)
    {
        gtk_widget_set_visible(window->header_modified_indicator,
                               mt_document_is_modified(document));
    }
    file = mt_document_get_file(document);
    subtitle = NULL;
    if (file == NULL)
    {
        subtitle = g_strdup(_("Draft"));
    }
    else
    {
        parent = g_file_get_parent(file);
        subtitle = parent != NULL ? g_file_get_parse_name(parent) : g_strdup("");
        g_clear_object(&parent);
    }
    gtk_label_set_text(window->header_subtitle_label, subtitle);
    g_free(subtitle);
}

gboolean
mt_window_snapshot_all(MtWindow *window)
{
    gint count;
    gint index;

    if (window == NULL || window->disposed || !ADW_IS_TAB_VIEW(window->tab_view))
    {
        return FALSE;
    }
    count = adw_tab_view_get_n_pages(window->tab_view);

    for (index = 0; index < count; index++)
    {
        AdwTabPage *page;
        MtDocument *document;

        page = adw_tab_view_get_nth_page(window->tab_view, index);
        document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);

        if (document != NULL)
        {
            mt_document_snapshot(document);
        }
    }

    return TRUE;
}

gboolean
mt_window_snapshot_all_cb(gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    mt_window_snapshot_all(window);

    window->snapshot_source_id = 0;

    return G_SOURCE_REMOVE;
}

void
mt_window_schedule_snapshot(MtWindow *window)
{
    if (window->snapshot_source_id != 0)
    {
        g_source_remove(window->snapshot_source_id);
    }

    window->snapshot_source_id = g_timeout_add_seconds(MT_SNAPSHOT_DELAY_SECONDS,
                                                        mt_window_snapshot_all_cb,
                                                        window);
}

void
mt_window_buffer_modified_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    MtDocument *document;
    MtWindow *window;

    document = user_data;
    window = g_object_get_data(G_OBJECT(buffer), "vellum-window");

    mt_window_update_document_title(document);

    if (window != NULL && !window->disposed)
    {
        mt_window_update_header_title(window);
    }

    if (window != NULL && !window->disposed && mt_document_is_modified(document))
    {
        mt_window_schedule_snapshot(window);
    }
}

void
mt_window_editor_scrolled(GtkAdjustment *adjustment, gpointer user_data)
{
    MtWindow *window;

    (void)adjustment;
    window = user_data;
    if (window != NULL && !window->disposed &&
        window->inline_completion_document != NULL)
    {
        mt_window_clear_inline_completion(window);
    mt_window_clear_inline_diff(window);
    }
}

void
mt_window_cursor_moved(GtkTextBuffer *buffer,
                       GtkTextIter *new_location,
                       GtkTextMark *mark,
                       gpointer user_data)
{
    MtWindow *window;

    (void)new_location;

    window = user_data;
    if (window == NULL || window->disposed)
    {
        return;
    }
    if (mark == gtk_text_buffer_get_insert(buffer) &&
        window->inline_completion_document != NULL)
    {
        /* 光标移动表示用户主动导航或编辑，幽灵补全不再与当前位置对应，
         * 必须立即移除，否则会像残影一样挂在旧坐标上。 */
        mt_window_clear_inline_completion(window);
    mt_window_clear_inline_diff(window);
    }
    mt_window_update_position(window);
}

void
mt_window_update_position(MtWindow *window)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter iter;
    gchar *position;

    if (window == NULL || window->disposed || !GTK_IS_LABEL(window->position_label))
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        gtk_label_set_text(window->position_label, "");
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
    position = g_strdup_printf(_("Ln %d, Col %d"),
                               gtk_text_iter_get_line(&iter) + 1,
                               gtk_text_iter_get_line_offset(&iter) + 1);
    gtk_label_set_text(window->position_label, position);
    g_free(position);
}

void
mt_window_page_selected(AdwTabView *tab_view,
                        GParamSpec *pspec,
                        gpointer user_data)
{
    MtWindow *window;

    (void)pspec;

    window = user_data;
    if (window == NULL || window->disposed || !ADW_IS_TAB_VIEW(tab_view))
    {
        return;
    }
    if (window->inline_completion_document != mt_window_get_current_document(window))
    {
        mt_window_clear_inline_completion(window);
    mt_window_clear_inline_diff(window);
    }
    mt_window_update_position(window);
    mt_window_update_language_label(window);
    mt_window_update_header_title(window);
    mt_window_properties_refresh(window);
    {
        MtDocument *document;
        GtkSourceBuffer *buffer;

        document = mt_window_get_current_document(window);
        if (document != NULL)
        {
            buffer = mt_document_get_buffer(document);
            if (g_object_get_data(G_OBJECT(buffer), MT_SCHEME_PENDING_KEY) != NULL)
            {
                g_object_set_data(G_OBJECT(buffer), MT_SCHEME_PENDING_KEY, NULL);
                mt_window_apply_source_scheme(window);
            }
        }
    }
}

void
mt_window_hide_external_change(MtWindow *window)
{
    if (window != NULL && !window->disposed && window->external_change_bar != NULL)
    {
        gtk_widget_set_visible(window->external_change_bar, FALSE);
        window->external_change_document = NULL;
    }
}

void
mt_window_file_monitor_changed(GFileMonitor *monitor,
                               GFile *file,
                               GFile *other_file,
                               GFileMonitorEvent event_type,
                               gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;

    (void)file;
    (void)other_file;
    window = user_data;
    document = g_object_get_data(G_OBJECT(monitor), MT_DOCUMENT_DATA_KEY);
    if (window == NULL || window->disposed || document == NULL ||
        g_get_monotonic_time() < document->monitor_suppress_until)
    {
        return;
    }

    if (event_type == G_FILE_MONITOR_EVENT_CHANGED ||
        event_type == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT ||
        event_type == G_FILE_MONITOR_EVENT_MOVED_IN)
    {
        window->external_change_document = document;
        if (window->external_change_bar != NULL)
        {
            gtk_widget_set_visible(window->external_change_bar, TRUE);
        }
    }
}

void
mt_window_monitor_document(MtWindow *window, MtDocument *document)
{
    GFile *file;
    GError *error;

    if (window == NULL || window->disposed || document == NULL)
    {
        return;
    }

    file = mt_document_get_file(document);
    if (file == NULL)
    {
        return;
    }

    if (document->file_monitor != NULL)
    {
        g_file_monitor_cancel(document->file_monitor);
        g_clear_object(&document->file_monitor);
    }
    error = NULL;
    document->file_monitor = g_file_monitor_file(file, G_FILE_MONITOR_NONE, NULL, &error);
    if (document->file_monitor == NULL)
    {
        g_clear_error(&error);
        return;
    }
    g_object_set_data(G_OBJECT(document->file_monitor), MT_DOCUMENT_DATA_KEY, document);
    g_signal_connect(document->file_monitor,
                     "changed",
                     G_CALLBACK(mt_window_file_monitor_changed),
                     window);
}

void
mt_window_external_reload_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    GFile *file;
    MtFileRequest *request;

    (void)button;
    window = user_data;
    document = window != NULL ? window->external_change_document : NULL;
    if (document == NULL)
    {
        return;
    }
    file = mt_document_get_file(document);
    if (file == NULL)
    {
        mt_window_hide_external_change(window);
        return;
    }

    document->monitor_suppress_until = g_get_monotonic_time() + 1000000;
    request = mt_file_request_new(window, document, NULL);
    mt_document_load_async(document, file, mt_window_document_load_finished, request);
    mt_window_hide_external_change(window);
    mt_window_show_toast(window, _("Reloading file from disk…"));
}

void
mt_window_external_change_close_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    mt_window_hide_external_change(user_data);
}

void
mt_window_document_load_finished(GObject *source,
                                 GAsyncResult *result,
                                 gpointer user_data)
{
    MtFileRequest *request;
    GError *error;

    (void)source;

    request = user_data;
    error = NULL;

    if (!mt_document_load_finish(request->document, result, &error))
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to open file: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
        g_clear_error(&error);
    }
    else
    {
        mt_window_update_document_title(request->document);
        mt_window_update_header_title(request->window);
        mt_window_monitor_document(request->window, request->document);
        mt_window_apply_source_scheme(request->window);
        mt_window_update_language_label(request->window);
        mt_window_properties_refresh(request->window);
        mt_window_show_toast(request->window, _("File opened"));
    }

    mt_file_request_free(request);
}

void
mt_window_document_save_finished(GObject *source,
                                 GAsyncResult *result,
                                 gpointer user_data)
{
    MtFileRequest *request;
    GError *error;

    (void)source;

    request = user_data;
    error = NULL;

    if (!mt_document_save_finish(request->document, result, &error))
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to save file: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
        g_clear_error(&error);
    }
    else
    {
        request->document->monitor_suppress_until = g_get_monotonic_time() + 1000000;
        mt_window_monitor_document(request->window, request->document);
        mt_window_update_document_title(request->document);
        mt_window_update_header_title(request->window);
        mt_window_properties_refresh(request->window);
        mt_window_show_toast(request->window, _("Saved"));
    }

    mt_file_request_free(request);
}

void
mt_window_open_dialog_finished(GObject *source,
                               GAsyncResult *result,
                               gpointer user_data)
{
    MtFileRequest *request;
    GListModel *files;
    GError *error;

    request = user_data;
    error = NULL;
    files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(source), result, &error);

    if (files != NULL)
    {
        mt_window_open_files(request->window, files);
        g_object_unref(files);
    }
    else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to select files: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
    }

    g_clear_error(&error);
    mt_file_request_free(request);
}

void
mt_window_save_dialog_finished(GObject *source,
                               GAsyncResult *result,
                               gpointer user_data)
{
    MtFileRequest *request;
    GFile *file;
    GError *error;

    request = user_data;
    error = NULL;
    file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);

    if (file != NULL)
    {
        if (mt_document_is_saving(request->document))
        {
            mt_window_show_toast(request->window, _("Saving is already in progress"));
        }
        else
        {
            MtFileRequest *save_request;

            save_request = mt_file_request_new(request->window, request->document, request->dialog);
            mt_document_save_async(request->document,
                                   file,
                                   mt_window_document_save_finished,
                                   save_request);
            mt_window_show_toast(request->window, _("Saving…"));
        }
        g_object_unref(file);
    }
    else if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to choose save location: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
    }

    g_clear_error(&error);
    mt_file_request_free(request);
}

void
mt_window_close_saved_finished(GObject *source,
                               GAsyncResult *result,
                               gpointer user_data)
{
    MtCloseRequest *request;
    GError *error;
    gboolean saved;

    (void)source;

    request = user_data;
    error = NULL;
    saved = mt_document_save_finish(request->document, result, &error);

    if (saved)
    {
        adw_tab_view_close_page_finish(request->window->tab_view, request->page, TRUE);
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to save file: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
        adw_tab_view_close_page_finish(request->window->tab_view, request->page, FALSE);
        g_clear_error(&error);
    }

    mt_close_request_free(request);
}

void
mt_window_close_dialog_response(AdwMessageDialog *dialog,
                                const gchar *response,
                                gpointer user_data)
{
    MtCloseRequest *request;

    request = user_data;

    if (g_strcmp0(response, "discard") == 0)
    {
        /* 丢弃未保存内容时同步删除草稿，避免同一篇草稿每次启动都被恢复。 */
        mt_document_remove_snapshot(request->document);
        adw_tab_view_close_page_finish(request->window->tab_view, request->page, TRUE);
        mt_close_request_free(request);
    }
    else if (g_strcmp0(response, "save") == 0)
    {
        mt_document_save_async(request->document,
                               NULL,
                               mt_window_close_saved_finished,
                               request);
    }
    else
    {
        adw_tab_view_close_page_finish(request->window->tab_view, request->page, FALSE);
        mt_close_request_free(request);
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
}

gboolean
mt_window_close_page_requested(AdwTabView *tab_view,
                               AdwTabPage *page,
                               gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;

    window = user_data;
    document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);

    if (window != NULL && document == window->inline_completion_document)
    {
        mt_window_clear_inline_completion(window);
    mt_window_clear_inline_diff(window);
    }

    if (document == NULL || !mt_document_is_modified(document))
    {
        adw_tab_view_close_page_finish(tab_view, page, TRUE);
        return TRUE;
    }

    {
        AdwMessageDialog *dialog;
        MtCloseRequest *request;

        dialog = ADW_MESSAGE_DIALOG(adw_message_dialog_new(GTK_WINDOW(window->window),
                                                            _("Save changes?"),
                                                            _("This document has unsaved changes.")));
        adw_message_dialog_add_response(dialog, "cancel", _("Cancel"));
        adw_message_dialog_add_response(dialog, "discard", _("Discard"));
        adw_message_dialog_set_response_appearance(dialog,
                                                    "discard",
                                                    ADW_RESPONSE_DESTRUCTIVE);

        if (!mt_document_is_untitled(document))
        {
            adw_message_dialog_add_response(dialog, "save", _("Save"));
            adw_message_dialog_set_response_appearance(dialog,
                                                        "save",
                                                        ADW_RESPONSE_SUGGESTED);
            adw_message_dialog_set_default_response(dialog, "save");
        }

        adw_message_dialog_set_close_response(dialog, "cancel");
        request = mt_close_request_new(window, document, page);
        g_signal_connect(dialog,
                         "response",
                         G_CALLBACK(mt_window_close_dialog_response),
                         request);
        gtk_window_present(GTK_WINDOW(dialog));
    }

    return TRUE;
}
