#include "mt-window-private.h"

void
mt_window_add_document(MtWindow *window, MtDocument *document)
{
    GtkWidget *scroll;
    GtkWidget *content;
    AdwTabPage *page;

    scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), mt_document_get_view(document));
    content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(content), scroll);
    /* 独立组件在 GtkSourceMap 上绘制半透明可视区域，并把点击/拖拽映射到编辑器滚动位置。 */
    document->overview = mt_overview_map_new(GTK_SOURCE_VIEW(mt_document_get_view(document)));
    gtk_widget_set_overflow(document->overview, GTK_OVERFLOW_VISIBLE);
    gtk_widget_set_size_request(document->overview, MT_OVERVIEW_MAP_WIDTH, -1);
    gtk_widget_add_css_class(document->overview, "vellum-overview-map");
    gtk_widget_set_visible(document->overview,
                           mt_settings_get_show_overview(window->settings));
    gtk_box_append(GTK_BOX(content), document->overview);
    gtk_widget_add_css_class(mt_document_get_view(document), "vellum-editor");
    page = adw_tab_view_append(window->tab_view, content);
    mt_document_set_page(document, page);
    g_object_set_data_full(G_OBJECT(page),
                           MT_DOCUMENT_DATA_KEY,
                           document,
                           (GDestroyNotify)mt_document_free);
    mt_window_update_document_title(document);
    adw_tab_view_set_selected_page(window->tab_view, page);

    g_object_set_data(G_OBJECT(mt_document_get_buffer(document)), "vellum-window", window);
    g_signal_connect(mt_document_get_buffer(document),
                     "modified-changed",
                     G_CALLBACK(mt_window_buffer_modified_changed),
                     document);
    g_signal_connect(mt_document_get_buffer(document),
                     "changed",
                     G_CALLBACK(mt_window_buffer_changed),
                     document);
    g_signal_connect(mt_document_get_buffer(document),
                     "insert-text",
                     G_CALLBACK(mt_window_buffer_inserted),
                     document);
    g_signal_connect(mt_document_get_buffer(document),
                     "delete-range",
                     G_CALLBACK(mt_window_buffer_deleted),
                     document);
    g_signal_connect(mt_document_get_buffer(document),
                     "mark-set",
                     G_CALLBACK(mt_window_cursor_moved),
                     window);
    {
        GtkAdjustment *vadjustment;
        GtkAdjustment *hadjustment;

        /* 幽灵补全的覆盖层锚定在固定坐标：滚动后位置必然错位，
         * 必须立即移除，否则就像残影一样留在原来的行上。 */
        vadjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(mt_document_get_view(document)));
        if (vadjustment != NULL)
        {
            g_signal_connect(vadjustment,
                             "value-changed",
                             G_CALLBACK(mt_window_editor_scrolled),
                             window);
        }
        hadjustment = gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(mt_document_get_view(document)));
        if (hadjustment != NULL)
        {
            g_signal_connect(hadjustment,
                             "value-changed",
                             G_CALLBACK(mt_window_editor_scrolled),
                             window);
        }
    }
    gtk_widget_grab_focus(mt_document_get_view(document));
    mt_window_apply_source_scheme(window);
    mt_window_apply_editor_preferences(window);
    mt_window_update_position(window);
    mt_window_update_language_label(window);
    mt_window_update_header_title(window);
    mt_window_properties_refresh(window);
}

void
mt_window_new_document(MtWindow *window)
{
    MtDocument *document;

    document = mt_document_new();
    mt_window_add_document(window, document);
}

void
mt_window_open_files(MtWindow *window, GListModel *files)
{
    guint index;
    guint count;

    count = g_list_model_get_n_items(files);

    for (index = 0; index < count; index++)
    {
        GFile *file;

        file = g_list_model_get_item(files, index);
        mt_window_open_file(window, file);
        g_object_unref(file);
    }
}

void
mt_window_open_file(MtWindow *window, GFile *file)
{
    MtDocument *document;
    MtFileRequest *request;

    document = mt_document_new();
    mt_window_add_document(window, document);
    request = mt_file_request_new(window, document, NULL);
    mt_document_load_async(document, file, mt_window_document_load_finished, request);
}

guint
mt_window_restore_snapshots(MtWindow *window)
{
    GPtrArray *snapshots;
    guint index;
    guint restored;

    snapshots = mt_document_list_snapshots();
    restored = 0;

    for (index = 0; index < snapshots->len; index++)
    {
        const gchar *path;
        MtDocument *document;
        GError *error;

        path = g_ptr_array_index(snapshots, index);
        document = mt_document_new();
        error = NULL;

        if (mt_document_restore_snapshot(document, path, &error))
        {
            GtkTextBuffer *buffer;
            GtkTextIter start;
            GtkTextIter end;

            buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
            gtk_text_buffer_get_bounds(buffer, &start, &end);
            if (gtk_text_iter_equal(&start, &end))
            {
                /* 空草稿没有恢复价值：删除文件并跳过，避免残留垃圾。 */
                g_remove(path);
                mt_document_free(document);
            }
            else
            {
                g_free(document->draft_path);
                document->draft_path = g_strdup(path);
                mt_window_add_document(window, document);
                restored++;
            }
        }
        else
        {
            g_warning("Unable to restore draft snapshot: %s", error->message);
            g_clear_error(&error);
            mt_document_free(document);
        }
    }

    g_ptr_array_unref(snapshots);

    if (restored > 0)
    {
        gchar *message;

        message = g_strdup_printf(ngettext("Recovered %u draft", "Recovered %u drafts", restored), restored);
        mt_window_show_toast(window, message);
        g_free(message);
    }

    return restored;
}

MtDocument *
mt_window_get_current_document(MtWindow *window)
{
    AdwTabPage *page;

    if (window == NULL || window->disposed || !ADW_IS_TAB_VIEW(window->tab_view))
    {
        return NULL;
    }

    page = adw_tab_view_get_selected_page(window->tab_view);
    if (page == NULL)
    {
        return NULL;
    }

    return g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);
}

gchar **
mt_window_get_open_document_paths(MtWindow *window, gsize *count)
{
    GPtrArray *paths;
    guint n;
    guint i;

    paths = g_ptr_array_new_with_free_func(g_free);
    if (window != NULL && !window->disposed && ADW_IS_TAB_VIEW(window->tab_view))
    {
        n = adw_tab_view_get_n_pages(window->tab_view);
        for (i = 0; i < n; i++)
        {
            AdwTabPage *page = adw_tab_view_get_nth_page(window->tab_view, i);
            MtDocument *document;

            if (page == NULL)
            {
                continue;
            }
            document = g_object_get_data(G_OBJECT(page), MT_DOCUMENT_DATA_KEY);
            if (document != NULL)
            {
                GFile *file = mt_document_get_file(document);

                if (file != NULL)
                {
                    g_ptr_array_add(paths, g_file_get_path(file));
                }
            }
        }
    }
    if (count != NULL)
    {
        *count = paths->len;
    }
    g_ptr_array_add(paths, NULL);
    return (gchar **)g_ptr_array_free(paths, FALSE);
}

void
mt_window_insert_text(MtWindow *window, const gchar *text)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter iter;

    if (window == NULL || window->disposed || text == NULL)
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        mt_window_new_document(window);
        document = mt_window_get_current_document(window);
    }

    /* 所有扩展插入都归为一次撤销；若插入来自其他动作，先清掉可能过期的候选覆盖层。 */
    if (window->inline_completion_document == document)
    {
        mt_window_clear_inline_completion(window);
    mt_window_clear_inline_diff(window);
    }
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_insert(buffer, &iter, text, -1);
    gtk_text_buffer_end_user_action(buffer);
}

void
mt_window_clear_inline_completion(MtWindow *window)
{
    MtDocument *document;

    if (window == NULL || window->disposed)
    {
        return;
    }

    document = window->inline_completion_document;
    window->inline_completion_document = NULL;
    if (document == NULL || document->inline_completion_label == NULL)
    {
        return;
    }

    /* GTK 4.22 中 gtk_text_view_add_overlay 添加的子项没有公开的移除 API：
     * gtk_text_view_remove 只处理 anchored children，直接 unparent 会留下
     * center_child 内部 overlays 队列的悬挂条目，重排时触发 snapshot_child
     * 断言。因此“清除”= 隐藏并复用同一个 label，视图销毁时自动释放。 */
    gtk_widget_set_visible(document->inline_completion_label, FALSE);
}

void
mt_window_clear_inline_diff(MtWindow *window)
{
    if (window == NULL || window->disposed)
    {
        return;
    }
    g_clear_pointer(&window->inline_diff_old, g_free);
    g_clear_pointer(&window->inline_diff_new, g_free);
    window->inline_diff_offset = 0;
    window->inline_diff_document = NULL;
    if (window->inline_diff_widget != NULL)
    {
        gtk_widget_set_visible(window->inline_diff_widget, FALSE);
    }
}

void
mt_window_show_inline_diff(MtWindow *window,
                           gint offset,
                           const gchar *old_text,
                           const gchar *new_text)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter iter;
    GtkWidget *view;
    GtkWidget *box;
    GtkWidget *lbl_old;
    GtkWidget *lbl_new;
    GdkRectangle rect;
    gint x;
    gint y;
    PangoAttrList *attrs;
    PangoAttribute *attr;

    if (window == NULL || window->disposed || old_text == NULL || new_text == NULL)
    {
        return;
    }
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }
    view = mt_document_get_view(document);
    if (!GTK_IS_TEXT_VIEW(view) || gtk_widget_get_root(view) == NULL)
    {
        return;
    }
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    if (offset < 0 || offset > gtk_text_buffer_get_char_count(buffer))
    {
        return;
    }

    gtk_text_buffer_get_iter_at_offset(buffer, &iter, offset);
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view), &iter, &rect);
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(view),
                                          GTK_TEXT_WINDOW_TEXT,
                                          rect.x, rect.y,
                                          &x, &y);

    mt_window_clear_inline_diff(window);

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    lbl_old = gtk_label_new(old_text);
    lbl_new = gtk_label_new(new_text);
    gtk_label_set_xalign(GTK_LABEL(lbl_old), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(lbl_new), 0.0f);

    attrs = pango_attr_list_new();
    attr = pango_attr_foreground_new(0xe01b, 0x2424, 0x2424);
    pango_attr_list_insert(attrs, attr);
    attr = pango_attr_strikethrough_new(TRUE);
    pango_attr_list_insert(attrs, attr);
    gtk_label_set_attributes(GTK_LABEL(lbl_old), attrs);
    pango_attr_list_unref(attrs);

    attrs = pango_attr_list_new();
    attr = pango_attr_foreground_new(0x2ec2, 0x7e7e, 0x7e7e);
    pango_attr_list_insert(attrs, attr);
    gtk_label_set_attributes(GTK_LABEL(lbl_new), attrs);
    pango_attr_list_unref(attrs);

    gtk_box_append(GTK_BOX(box), lbl_old);
    gtk_box_append(GTK_BOX(box), lbl_new);
    gtk_text_view_add_overlay(GTK_TEXT_VIEW(view), box, x, y);
    gtk_widget_set_opacity(box, 0.92);
    gtk_widget_set_visible(box, TRUE);

    window->inline_diff_widget = box;
    window->inline_diff_document = document;
    window->inline_diff_old = g_strdup(old_text);
    window->inline_diff_new = g_strdup(new_text);
    window->inline_diff_offset = offset;
}

void
mt_window_apply_inline_diff(MtWindow *window)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gint old_len;

    if (window == NULL || window->disposed)
    {
        return;
    }
    if (window->inline_diff_document == NULL || window->inline_diff_old == NULL)
    {
        return;
    }
    document = window->inline_diff_document;
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    old_len = (gint)g_utf8_strlen(window->inline_diff_old, -1);
    gtk_text_buffer_get_iter_at_offset(buffer, &start, window->inline_diff_offset);
    gtk_text_buffer_get_iter_at_offset(buffer, &end, window->inline_diff_offset + old_len);
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_insert(buffer, &start, window->inline_diff_new, -1);
    mt_window_clear_inline_diff(window);
}

void
mt_window_show_inline_completion(MtWindow *window, const gchar *text)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter cursor;
    GtkTextIter line_start;
    GtkWidget *view;
    GdkRectangle rectangle;
    GdkRectangle line_rect;
    gint x;
    gint y;
    gint lx;
    gint line_height;
    gchar **lines;
    gchar **disp;
    gchar *preview;

    if (window == NULL || window->disposed || text == NULL || *text == '\0')
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }

    view = mt_document_get_view(document);
    if (!GTK_IS_TEXT_VIEW(view) || gtk_widget_get_root(view) == NULL)
    {
        return;
    }

    mt_window_clear_inline_completion(window);
    mt_window_clear_inline_diff(window);

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    gtk_text_buffer_get_iter_at_mark(buffer,
                                     &cursor,
                                     gtk_text_buffer_get_insert(buffer));
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view),
                                    &cursor,
                                    &rectangle);
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(view),
                                          GTK_TEXT_WINDOW_TEXT,
                                          rectangle.x,
                                          rectangle.y,
                                          &x,
                                          &y);

    gtk_text_buffer_get_iter_at_line(buffer, &line_start,
                                     gtk_text_iter_get_line(&cursor));
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(view), &line_start, &line_rect);
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(view),
                                          GTK_TEXT_WINDOW_TEXT,
                                          line_rect.x,
                                          line_rect.y,
                                          &lx,
                                          NULL);
    line_height = rectangle.height > 0 ? rectangle.height : 16;

    lines = g_strsplit(text, "\n", -1);
    disp = lines;
    if (lines[0] != NULL && lines[0][0] == '\0')
    {
        /* 建议以换行开头（如函数体紧随 `:`）：跳过空首行，从下一行显示。 */
        disp = lines + 1;
    }
    while (disp[0] != NULL && disp[0][0] == '\0')
    {
        disp++;
    }
    if (disp[0] == NULL)
    {
        g_strfreev(lines);
        return;
    }

    preview = g_strjoinv("\n", disp);

    if (document->inline_completion_label == NULL)
    {
        document->inline_completion_label = gtk_label_new(preview);
        gtk_widget_add_css_class(document->inline_completion_label,
                                 "vellum-inline-completion");
        gtk_widget_add_css_class(document->inline_completion_label, "monospace");
        gtk_label_set_xalign(GTK_LABEL(document->inline_completion_label), 0.0f);
        gtk_widget_set_opacity(document->inline_completion_label, 0.6);
        gtk_text_view_add_overlay(GTK_TEXT_VIEW(view),
                                  document->inline_completion_label,
                                  disp == lines ? x + rectangle.width : lx,
                                  disp == lines ? y : y + line_height);
        gtk_widget_set_visible(document->inline_completion_label, TRUE);
    }
    else
    {
        gtk_label_set_text(GTK_LABEL(document->inline_completion_label), preview);
        gtk_text_view_move_overlay(GTK_TEXT_VIEW(view),
                                   document->inline_completion_label,
                                   disp == lines ? x + rectangle.width : lx,
                                   disp == lines ? y : y + line_height);
        gtk_widget_set_visible(document->inline_completion_label, TRUE);
    }
    g_free(preview);
    g_strfreev(lines);
    window->inline_completion_document = document;
}

gchar *
mt_window_get_text_after_cursor(MtWindow *window)
{
    MtDocument *document;
    GtkTextIter cursor;
    GtkTextIter end;

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return g_strdup("");
    }

    gtk_text_buffer_get_iter_at_mark(GTK_TEXT_BUFFER(mt_document_get_buffer(document)),
                                     &cursor,
                                     gtk_text_buffer_get_insert(GTK_TEXT_BUFFER(mt_document_get_buffer(document))));
    gtk_text_buffer_get_end_iter(GTK_TEXT_BUFFER(mt_document_get_buffer(document)), &end);
    return gtk_text_buffer_get_text(GTK_TEXT_BUFFER(mt_document_get_buffer(document)),
                                    &cursor,
                                    &end,
                                    TRUE);
}

void
mt_window_show_toast(MtWindow *window, const gchar *message)
{
    AdwToast *toast;

    if (window == NULL || window->disposed || message == NULL ||
        !ADW_IS_TOAST_OVERLAY(window->toast_overlay))
    {
        return;
    }

    toast = adw_toast_new(message);
    adw_toast_overlay_add_toast(window->toast_overlay, toast);
}

GtkStack *
mt_window_get_panel_stack(MtWindow *window, MtPluginPanelLocation location)
{
    return location == MT_PLUGIN_PANEL_SIDEBAR ?
           window->sidebar_stack : window->auxiliary_stack;
}

gchar *
mt_window_get_current_file_path(MtWindow *window)
{
    MtDocument *document;
    GFile *file;

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return NULL;
    }

    file = mt_document_get_file(document);
    return file != NULL ? g_file_get_path(file) : NULL;
}

void
mt_window_open_file_path(MtWindow *window, const gchar *path)
{
    GFile *file;

    if (path == NULL || !g_file_test(path, G_FILE_TEST_IS_REGULAR))
    {
        return;
    }

    file = g_file_new_for_path(path);
    mt_window_open_file(window, file);
    g_object_unref(file);
}

void
mt_window_save_and_close_finished(GObject *source,
                                  GAsyncResult *result,
                                  gpointer user_data)
{
    MtFileRequest *request;
    GError *error;

    (void)source;
    request = user_data;
    error = NULL;
    if (mt_document_save_finish(request->document, result, &error))
    {
        mt_window_update_document_title(request->document);
        if (!request->window->disposed && ADW_IS_TAB_VIEW(request->window->tab_view))
        {
            adw_tab_view_close_page(request->window->tab_view, request->page);
        }
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to save file: %s"), error->message);
        mt_window_show_toast(request->window, message);
        g_free(message);
        g_clear_error(&error);
    }
    mt_file_request_free(request);
}

void
mt_window_paste_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtWindow *window;
    gchar *text;
    GError *error;

    window = user_data;
    error = NULL;
    text = gdk_clipboard_read_text_finish(GDK_CLIPBOARD(source), result, &error);
    if (text != NULL && *text != '\0')
    {
        mt_window_insert_text(window, text);
    }
    g_free(text);
    g_clear_error(&error);
}

/* —— 编译器报错的红色波浪下划线 —— */

typedef struct
{
    gint offset;
    gint length;
    gchar *message;
} MtErrorRange;

/* 悬停在红色下划线上时显示报错描述；定义在下方，此处前向声明。 */
static gboolean
mt_window_error_query_tooltip(gpointer widget,
                              gint x,
                              gint y,
                              gboolean keyboard_mode,
                              MtWindow *window);

static void
mt_window_ensure_error_state(MtWindow *window)
{
    if (window->error_ranges == NULL)
    {
        window->error_ranges = g_ptr_array_new_with_free_func((GDestroyNotify)g_free);
    }
    if (window->error_tooltip_handler_id == 0)
    {
        MtDocument *document;

        document = mt_window_get_current_document(window);
        if (document != NULL)
        {
            GtkWidget *view;

            view = mt_document_get_view(document);
            if (GTK_IS_TEXT_VIEW(view))
            {
                gtk_widget_set_has_tooltip(view, TRUE);
                window->error_tooltip_handler_id =
                    g_signal_connect(view,
                                     "query-tooltip",
                                     G_CALLBACK(mt_window_error_query_tooltip),
                                     window);
            }
        }
    }
}

/* 悬停在红色下划线上时显示报错描述。 */
static gboolean
mt_window_error_query_tooltip(gpointer widget,
                              gint x,
                              gint y,
                              gboolean keyboard_mode,
                              MtWindow *window)
{
    GtkTextView *view;
    GtkTextBuffer *buffer;
    GtkTextIter iter;
    guint i;

    (void)keyboard_mode;
    view = GTK_TEXT_VIEW(widget);
    buffer = gtk_text_view_get_buffer(view);
    if (!gtk_text_view_get_iter_at_location(view, &iter, x, y))
    {
        gtk_widget_set_tooltip_text(widget, NULL);
        return FALSE;
    }

    if (window->error_ranges == NULL)
    {
        gtk_widget_set_tooltip_text(widget, NULL);
        return FALSE;
    }

    for (i = 0; i < window->error_ranges->len; i++)
    {
        MtErrorRange *range;

        range = g_ptr_array_index(window->error_ranges, i);
        if (range->message == NULL || range->message[0] == '\0')
        {
            continue;
        }

        {
            GtkTextIter start;
            GtkTextIter end;

            gtk_text_buffer_get_iter_at_offset(buffer, &start, range->offset);
            gtk_text_buffer_get_iter_at_offset(buffer, &end, range->offset + range->length);
            if (gtk_text_iter_compare(&iter, &start) >= 0 &&
                gtk_text_iter_compare(&iter, &end) < 0)
            {
                gtk_widget_set_tooltip_text(widget, range->message);
                return TRUE;
            }
        }
    }

    gtk_widget_set_tooltip_text(widget, NULL);
    return FALSE;
}

void
mt_window_show_error_underline(MtWindow *window,
                               gint offset,
                               gint length,
                               const gchar *message)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkWidget *view;
    GtkTextTag *tag;
    MtErrorRange *range;

    if (window == NULL || window->disposed || offset < 0 || length <= 0)
    {
        return;
    }
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }
    view = mt_document_get_view(document);
    if (!GTK_IS_TEXT_VIEW(view) || gtk_widget_get_root(view) == NULL)
    {
        return;
    }
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    if (offset + length > gtk_text_buffer_get_char_count(buffer))
    {
        return;
    }

    mt_window_ensure_error_state(window);

    /* 复用同一个 tag：重名时旧的自动覆盖，避免每次报错都累积 tag。 */
    {
        GtkTextTagTable *table;

        table = gtk_text_buffer_get_tag_table(buffer);
        tag = gtk_text_tag_table_lookup(table, "vellum-error-underline");
        if (tag == NULL)
        {
            tag = gtk_text_buffer_create_tag(buffer, "vellum-error-underline", NULL);
            /* PANGO_UNDERLINE_ERROR 画出红色波浪线，正是 IDE 的“红行描述”。 */
            g_object_set(tag, "underline", PANGO_UNDERLINE_ERROR, NULL);
        }
    }

    {
        GtkTextIter start;
        GtkTextIter end;

        gtk_text_buffer_get_iter_at_offset(buffer, &start, offset);
        gtk_text_buffer_get_iter_at_offset(buffer, &end, offset + length);
        gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
    }

    /* 记录范围供悬停提示使用；同一位置重复报错时只更新描述。 */
    {
        guint i;

        for (i = 0; i < window->error_ranges->len; i++)
        {
            MtErrorRange *old;

            old = g_ptr_array_index(window->error_ranges, i);
            if (old->offset == offset && old->length == length)
            {
                g_free(old->message);
                old->message = g_strdup(message);
                return;
            }
        }
    }

    range = g_new0(MtErrorRange, 1);
    range->offset = offset;
    range->length = length;
    range->message = g_strdup(message);
    g_ptr_array_add(window->error_ranges, range);
}

void
mt_window_clear_error_underlines(MtWindow *window)
{
    MtDocument *document;

    if (window == NULL || window->disposed)
    {
        return;
    }

    if (window->error_ranges != NULL)
    {
        g_ptr_array_set_size(window->error_ranges, 0);
    }

    if (window->error_tooltip_handler_id != 0)
    {
        document = mt_window_get_current_document(window);
        if (document != NULL)
        {
            GtkWidget *view;

            view = mt_document_get_view(document);
            if (GTK_IS_TEXT_VIEW(view))
            {
                g_signal_handler_disconnect(view, window->error_tooltip_handler_id);
            }
        }
        window->error_tooltip_handler_id = 0;

        document = mt_window_get_current_document(window);
        if (document != NULL)
        {
            GtkTextBuffer *buffer;
            GtkTextIter start;
            GtkTextIter end;

            buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
            gtk_text_buffer_get_start_iter(buffer, &start);
            gtk_text_buffer_get_end_iter(buffer, &end);
            gtk_text_buffer_remove_tag_by_name(buffer,
                                               "vellum-error-underline",
                                               &start, &end);
        }
    }
}

/* —— 断点 gutter 标记 —— */

/* 用户在 gutter 点击断点标记时，激活窗口 action "win.toggle-breakpoint"，
 * 由插件连接该 action 的 activate 信号自行维护断点集合并调用宿主同步。 */
static void
mt_window_breakpoint_toggled(MtWindow *window, gint line)
{
    GAction *action;

    if (window == NULL || window->window == NULL)
    {
        return;
    }
    action = g_action_map_lookup_action(G_ACTION_MAP(window->window),
                                       "win.toggle-breakpoint");
    if (action != NULL)
    {
        GVariant *value;

        value = g_variant_new_int32(line);
        g_action_activate(action, value);
    }
}

static void
mt_window_breakpoint_button_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    gint line;

    window = g_object_get_data(G_OBJECT(button), "vellum-window");
    line = GPOINTER_TO_INT(user_data);
    mt_window_breakpoint_toggled(window, line);
}

void
mt_window_set_breakpoint(MtWindow *window, gint line)
{
    MtDocument *document;
    GtkTextView *view;
    GtkTextBuffer *buffer;
    GtkTextIter iter;
    GdkRectangle rect;
    gint x;
    gint y;
    GtkWidget *button;
    GtkWidget *dot;

    if (window == NULL || window->disposed || line <= 0)
    {
        return;
    }
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }
    view = GTK_TEXT_VIEW(mt_document_get_view(document));
    if (!GTK_IS_TEXT_VIEW(view) || gtk_widget_get_root(GTK_WIDGET(view)) == NULL)
    {
        return;
    }
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    if (line - 1 >= gtk_text_buffer_get_line_count(buffer))
    {
        return;
    }

    if (window->breakpoint_lines == NULL)
    {
        window->breakpoint_lines = g_ptr_array_new_with_free_func(NULL);
        window->breakpoint_widgets = g_hash_table_new_full(g_int_hash,
                                                           g_int_equal,
                                                           g_free,
                                                           (GDestroyNotify)gtk_widget_unparent);
    }

    /* 已存在则忽略。 */
    {
        guint i;

        for (i = 0; i < window->breakpoint_lines->len; i++)
        {
            gint *existing;

            existing = g_ptr_array_index(window->breakpoint_lines, i);
            if (existing != NULL && *existing == line)
            {
                return;
            }
        }
    }

    gtk_text_buffer_get_iter_at_line(buffer, &iter, line - 1);
    gtk_text_view_get_iter_location(view, &iter, &rect);
    gtk_text_view_buffer_to_window_coords(view,
                                          GTK_TEXT_WINDOW_TEXT,
                                          rect.x, rect.y,
                                          &x, &y);

    button = gtk_button_new();
    gtk_widget_set_name(button, "vellum-breakpoint");
    dot = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(button, 14, 14);
    gtk_button_set_child(GTK_BUTTON(button), dot);
    /* 把标记放在左侧留白（gutter 与文本之间），不遮挡代码。 */
    gtk_text_view_add_overlay(view, button, 2, y);
    gtk_widget_set_visible(button, TRUE);
    /* 按钮随视图销毁自动释放；此处借用指针通知点击动作所属窗口。 */
    g_object_set_data(G_OBJECT(button), "vellum-window", window);
    g_signal_connect(button, "clicked", G_CALLBACK(mt_window_breakpoint_button_clicked),
                     GINT_TO_POINTER(line));

    g_ptr_array_add(window->breakpoint_lines, GINT_TO_POINTER(line));
    g_hash_table_insert(window->breakpoint_widgets,
                        g_memdup(&line, sizeof(gint)),
                        button);
}

void
mt_window_clear_breakpoint(MtWindow *window, gint line)
{
    if (window == NULL || window->disposed || line <= 0)
    {
        return;
    }

    if (window->breakpoint_lines != NULL)
    {
        guint i;

        for (i = 0; i < window->breakpoint_lines->len; i++)
        {
            gint *existing;

            existing = g_ptr_array_index(window->breakpoint_lines, i);
            if (existing != NULL && *existing == line)
            {
                g_ptr_array_remove_index_fast(window->breakpoint_lines, i);
                break;
            }
        }
    }

    if (window->breakpoint_widgets != NULL)
    {
        g_hash_table_remove(window->breakpoint_widgets, &line);
    }
}

void
mt_window_clear_all_breakpoints(MtWindow *window)
{
    if (window == NULL || window->disposed)
    {
        return;
    }

    if (window->breakpoint_widgets != NULL)
    {
        GHashTable *widgets;

        widgets = window->breakpoint_widgets;
        window->breakpoint_widgets = NULL;
        g_hash_table_destroy(widgets);
    }

    g_clear_pointer(&window->breakpoint_lines, g_ptr_array_unref);
}

/* —— 滚动到指定行 —— */

void
mt_window_scroll_to_line(MtWindow *window, gint line)
{
    MtDocument *document;
    GtkTextView *view;
    GtkTextBuffer *buffer;
    GtkTextIter iter;
    GtkAdjustment *vadj;

    if (window == NULL || window->disposed || line <= 0)
    {
        return;
    }
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }
    view = GTK_TEXT_VIEW(mt_document_get_view(document));
    if (!GTK_IS_TEXT_VIEW(view))
    {
        return;
    }
    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    if (line - 1 >= gtk_text_buffer_get_line_count(buffer))
    {
        return;
    }

    gtk_text_buffer_get_iter_at_line(buffer, &iter, line - 1);
    gtk_text_view_scroll_to_iter(view, &iter, 0.0, TRUE, 0.0, 0.35);
}
