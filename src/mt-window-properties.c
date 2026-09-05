/* ---------- 文档属性面板（学习原版 GNOME Text Editor 的 Document Properties） ---------- */

#include "mt-window-private.h"

void
mt_window_properties_open_uri(GtkWindow *parent, const gchar *uri)
{
    GtkUriLauncher *launcher;

    if (parent == NULL || uri == NULL || *uri == '\0')
    {
        return;
    }

    launcher = gtk_uri_launcher_new(uri);
    gtk_uri_launcher_launch(launcher, parent, NULL, NULL, NULL);
    g_object_unref(launcher);
}

void
mt_window_properties_name_activated(AdwActionRow *row, gpointer user_data)
{
    MtPropertiesPanel *panel;
    MtDocument *document;
    GFile *file;
    gchar *uri;

    (void)row;
    panel = user_data;
    document = mt_window_get_current_document(panel->window);
    if (document == NULL || mt_document_is_untitled(document))
    {
        return;
    }
    file = mt_document_get_file(document);
    if (file == NULL)
    {
        return;
    }
    uri = g_file_get_uri(file);
    mt_window_properties_open_uri(GTK_WINDOW(panel->window->window), uri);
    g_free(uri);
}

void
mt_window_properties_location_activated(AdwActionRow *row, gpointer user_data)
{
    MtPropertiesPanel *panel;
    MtDocument *document;
    GFile *file;
    GFile *parent;
    gchar *uri;

    (void)row;
    panel = user_data;
    document = mt_window_get_current_document(panel->window);
    if (document == NULL || mt_document_is_untitled(document))
    {
        return;
    }
    file = mt_document_get_file(document);
    if (file == NULL)
    {
        return;
    }
    parent = g_file_get_parent(file);
    uri = parent != NULL ? g_file_get_uri(parent) : g_file_get_uri(file);
    mt_window_properties_open_uri(GTK_WINDOW(panel->window->window), uri);
    g_free(uri);
    g_clear_object(&parent);
}

static GtkSourceNewlineType
mt_window_properties_detect_newline(GtkTextBuffer *buffer)
{
    GtkTextIter start;
    GtkTextIter end;
    gchar *text;
    const gchar *p;
    GtkSourceNewlineType newline;

    gtk_text_buffer_get_start_iter(buffer, &start);
    end = start;
    gtk_text_iter_forward_chars(&end, 4096);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    newline = GTK_SOURCE_NEWLINE_TYPE_LF;
    for (p = text; *p != '\0'; p = g_utf8_next_char(p))
    {
        if (*p == '\r')
        {
            if (p[1] == '\n')
            {
                newline = GTK_SOURCE_NEWLINE_TYPE_CR_LF;
            }
            else
            {
                newline = GTK_SOURCE_NEWLINE_TYPE_CR;
            }
            break;
        }
        if (*p == '\n')
        {
            newline = GTK_SOURCE_NEWLINE_TYPE_LF;
            break;
        }
    }
    g_free(text);
    return newline;
}

void
mt_window_properties_convert_newlines(MtPropertiesPanel *panel,
                                      GtkSourceNewlineType newline)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *text;
    GString *converted;
    const gchar *target;
    const gchar *p;

    document = mt_window_get_current_document(panel->window);
    if (document == NULL)
    {
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    if (gtk_text_buffer_get_char_count(buffer) > MT_PROPERTIES_STATS_LIMIT)
    {
        mt_window_properties_refresh_metadata(panel);
        mt_window_show_toast(panel->window,
                             _("The document is too large to convert line endings"));
        return;
    }

    if (mt_window_properties_detect_newline(buffer) == newline)
    {
        return;
    }

    switch (newline)
    {
        case GTK_SOURCE_NEWLINE_TYPE_CR:
            target = "\r";
            break;
        case GTK_SOURCE_NEWLINE_TYPE_CR_LF:
            target = "\r\n";
            break;
        default:
            target = "\n";
            break;
    }

    gtk_text_buffer_get_bounds(buffer, &start, &end);
    text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    converted = g_string_new(NULL);
    for (p = text; *p != '\0'; p++)
    {
        if (*p == '\r')
        {
            if (p[1] == '\n')
            {
                p++;
            }
            g_string_append(converted, target);
        }
        else if (*p == '\n')
        {
            g_string_append(converted, target);
        }
        else
        {
            g_string_append_c(converted, *p);
        }
    }
    g_free(text);

    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_delete(buffer, &start, &end);
    gtk_text_buffer_insert(buffer, &start, converted->str, -1);
    gtk_text_buffer_end_user_action(buffer);
    g_string_free(converted, TRUE);

    mt_window_show_toast(panel->window, _("Line endings converted"));
}

void
mt_window_properties_refresh_metadata(MtPropertiesPanel *panel)
{
    MtWindow *window;
    MtDocument *document;
    GtkSourceBuffer *buffer;
    GtkSourceLanguage *language;
    GtkSourceFile *source_file;
    const GtkSourceEncoding *encoding;
    GtkSourceNewlineType newline;
    GFile *file;

    window = panel->window;
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        adw_action_row_set_subtitle(panel->name_row, "");
        adw_action_row_set_subtitle(panel->location_row, "");
        g_object_set(G_OBJECT(panel->name_row), "activatable", FALSE, NULL);
        g_object_set(G_OBJECT(panel->location_row), "activatable", FALSE, NULL);
        adw_action_row_set_subtitle(panel->type_row, _("Plain Text"));
        panel->type_selected = 0;
        panel->encoding_selected = 0;
        panel->refreshing_metadata = TRUE;
        adw_combo_row_set_selected(panel->encoding_row, 0);
        adw_combo_row_set_selected(panel->newline_row, 0);
        panel->refreshing_metadata = FALSE;
        return;
    }

    adw_action_row_set_subtitle(panel->name_row, mt_document_get_display_name(document));
    g_object_set(G_OBJECT(panel->name_row), "activatable", !mt_document_is_untitled(document), NULL);

    file = mt_document_get_file(document);
    if (file != NULL && !mt_document_is_untitled(document))
    {
        GFile *parent;
        gchar *path;

        parent = g_file_get_parent(file);
        path = parent != NULL ? g_file_get_path(parent) : NULL;
        adw_action_row_set_subtitle(panel->location_row,
                                    path != NULL ? path : _("Unknown location"));
        g_free(path);
        g_clear_object(&parent);
        g_object_set(G_OBJECT(panel->location_row), "activatable", TRUE, NULL);
    }
    else
    {
        adw_action_row_set_subtitle(panel->location_row, _("Not saved yet"));
        g_object_set(G_OBJECT(panel->location_row), "activatable", FALSE, NULL);
    }

    buffer = mt_document_get_buffer(document);
    language = gtk_source_buffer_get_language(buffer);
    source_file = document->source_file;
    encoding = source_file != NULL ? gtk_source_file_get_encoding(source_file) : NULL;

    if (language != NULL)
    {
        const gchar *id;
        const gchar *name;
        guint index;

        id = gtk_source_language_get_id(language);
        name = gtk_source_language_get_name(language);
        adw_action_row_set_subtitle(panel->type_row, name != NULL ? name : id);
        panel->type_selected = 0;
        for (index = 0; index < panel->type_language_ids->len; index++)
        {
            if (g_strcmp0(g_ptr_array_index(panel->type_language_ids, index), id) == 0)
            {
                panel->type_selected = index + 1;
                break;
            }
        }
    }
    else
    {
        adw_action_row_set_subtitle(panel->type_row, _("Plain Text"));
        panel->type_selected = 0;
    }

    if (encoding != NULL)
    {
        const gchar *name;
        guint index;

        name = gtk_source_encoding_get_name(encoding);
        panel->encoding_selected = 0;
        for (index = 0; index < panel->encoding_items->len; index++)
        {
            GtkSourceEncoding *item;

            item = g_ptr_array_index(panel->encoding_items, index);
            if (g_strcmp0(gtk_source_encoding_get_name(item), name) == 0)
            {
                panel->encoding_selected = index;
                break;
            }
        }
    }
    else
    {
        panel->encoding_selected = 0;
    }

    /* 编码和换行均使用原生选择行；刷新时统一抑制回调，避免误触发重载或转换。 */
    panel->refreshing_metadata = TRUE;
    adw_combo_row_set_selected(panel->encoding_row, panel->encoding_selected);
    {
        guint selected;

        newline = mt_window_properties_detect_newline(GTK_TEXT_BUFFER(buffer));
        switch (newline)
        {
            case GTK_SOURCE_NEWLINE_TYPE_CR:
                selected = 2;
                break;
            case GTK_SOURCE_NEWLINE_TYPE_CR_LF:
                selected = 1;
                break;
            default:
                selected = 0;
                break;
        }
        adw_combo_row_set_selected(panel->newline_row, selected);
    }
    panel->refreshing_metadata = FALSE;
}

void
mt_window_properties_refresh_stats(MtPropertiesPanel *panel)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *text;
    const gchar *pointer;
    guint lines;
    guint words;
    guint chars;
    guint printable;
    gchar *label;

    document = mt_window_get_current_document(panel->window);
    if (document == NULL)
    {
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    lines = gtk_text_buffer_get_line_count(buffer);
    gtk_text_buffer_get_start_iter(buffer, &start);
    gtk_text_buffer_get_end_iter(buffer, &end);
    chars = gtk_text_iter_get_offset(&end);
    words = 0;
    printable = 0;

    /* 超大文档避免整篇拷贝统计，防止输入时卡顿。 */
    if (chars <= MT_PROPERTIES_STATS_LIMIT)
    {
        gboolean in_word;

        text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        if (text != NULL)
        {
            in_word = FALSE;
            for (pointer = text; *pointer != '\0'; pointer = g_utf8_next_char(pointer))
            {
                gunichar ch;

                ch = g_utf8_get_char(pointer);
                if (g_unichar_isspace(ch))
                {
                    in_word = FALSE;
                }
                else
                {
                    printable++;
                    if (!in_word)
                    {
                        words++;
                        in_word = TRUE;
                    }
                }
            }
            g_free(text);
        }
    }

    label = g_strdup_printf("%u", lines);
    adw_action_row_set_subtitle(panel->lines_row, label);
    g_free(label);
    label = g_strdup_printf("%u", words);
    adw_action_row_set_subtitle(panel->words_row, label);
    g_free(label);
    label = g_strdup_printf("%u", printable);
    adw_action_row_set_subtitle(panel->chars_row, label);
    g_free(label);
    label = g_strdup_printf("%u", chars);
    adw_action_row_set_subtitle(panel->all_chars_row, label);
    g_free(label);
}

void
mt_window_properties_refresh(MtWindow *window)
{
    MtPropertiesPanel *panel;

    if (window == NULL || window->disposed || window->properties == NULL)
    {
        return;
    }
    panel = window->properties;
    mt_window_properties_refresh_metadata(panel);
    mt_window_properties_refresh_stats(panel);
}

static gboolean
mt_window_properties_refresh_cb(gpointer user_data)
{
    MtWindow *window;
    MtPropertiesPanel *panel;

    window = user_data;
    if (window == NULL || window->disposed || window->properties == NULL)
    {
        return G_SOURCE_REMOVE;
    }
    window->properties_source_id = 0;
    panel = window->properties;
    if (gtk_widget_get_visible(panel->page))
    {
        mt_window_properties_refresh(window);
    }
    return G_SOURCE_REMOVE;
}

void
mt_window_properties_schedule_refresh(MtWindow *window)
{
    if (window == NULL || window->disposed || window->properties == NULL)
    {
        return;
    }
    if (window->properties_source_id != 0)
    {
        g_source_remove(window->properties_source_id);
    }
    window->properties_source_id = g_timeout_add(400,
                                                 mt_window_properties_refresh_cb,
                                                 window);
}

/* 属性面板在停靠时保持紧凑的固定视觉宽度；窄窗口时由 OverlaySplitView 以抽屉呈现。 */
#define MT_PROPERTIES_PANEL_WIDTH 300

/* 右侧检查器由 AdwOverlaySplitView 管理：宽屏停靠且宽度稳定，窄屏以不压缩编辑区的抽屉显示。 */
void
mt_window_auxiliary_visible(MtWindow *window, gboolean visible)
{
    if (window == NULL || window->auxiliary_stack == NULL)
    {
        return;
    }

    if (window->auxiliary_wrap != NULL)
    {
        gtk_widget_set_visible(GTK_WIDGET(window->auxiliary_wrap), TRUE);
    }
    gtk_widget_set_visible(GTK_WIDGET(window->auxiliary_stack), TRUE);
    if (window->auxiliary_split != NULL)
    {
        adw_overlay_split_view_set_show_sidebar(window->auxiliary_split, visible);
    }
}

void
mt_window_auxiliary_width_changed(GObject *object,
                                  GParamSpec *pspec,
                                  gpointer user_data)
{
    MtWindow *window;
    gboolean collapsed;

    (void)object;
    (void)pspec;
    window = user_data;
    if (window == NULL || window->disposed || window->auxiliary_split == NULL)
    {
        return;
    }

    collapsed = gtk_widget_get_width(GTK_WIDGET(window->auxiliary_split)) <
                MT_AUXILIARY_DRAWER_BREAKPOINT;
    if (adw_overlay_split_view_get_collapsed(window->auxiliary_split) != collapsed)
    {
        adw_overlay_split_view_set_collapsed(window->auxiliary_split, collapsed);
    }
}

void
mt_window_properties_set_visible(MtWindow *window, gboolean visible)
{
    MtPropertiesPanel *panel;

    if (window == NULL || window->disposed || window->properties == NULL)
    {
        return;
    }
    panel = window->properties;

    if (visible)
    {
        if (gtk_widget_get_parent(panel->page) == NULL)
        {
            gtk_stack_add_named(window->auxiliary_stack,
                                panel->page,
                                "vellum-properties");
        }
        gtk_stack_set_visible_child_name(window->auxiliary_stack, "vellum-properties");
        mt_window_auxiliary_visible(window, TRUE);
        mt_window_properties_refresh(window);
    }
    else
    {
        gtk_stack_remove(window->auxiliary_stack, panel->page);
        if (g_list_model_get_n_items(G_LIST_MODEL(gtk_stack_get_pages(window->auxiliary_stack))) == 0)
        {
            mt_window_auxiliary_visible(window, FALSE);
        }
    }
}

void
mt_window_properties_toggled(GtkToggleButton *button, gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    if (window == NULL || window->disposed)
    {
        return;
    }
    mt_window_properties_set_visible(window, gtk_toggle_button_get_active(button));
}

void
mt_window_toggle_properties(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;

    (void)action;
    (void)parameter;
    window = user_data;
    if (window == NULL || window->disposed || window->properties_button == NULL)
    {
        return;
    }
    gtk_toggle_button_set_active(window->properties_button,
                                 !gtk_toggle_button_get_active(window->properties_button));
}

static gboolean
mt_window_properties_name_matches(const gchar *name, const gchar *filter)
{
    gchar *name_folded;
    gchar *filter_folded;
    gboolean matches;

    if (filter == NULL || *filter == '\0')
    {
        return TRUE;
    }

    name_folded = g_utf8_casefold(name != NULL ? name : "", -1);
    filter_folded = g_utf8_casefold(filter, -1);
    matches = strstr(name_folded, filter_folded) != NULL;
    g_free(name_folded);
    g_free(filter_folded);

    return matches;
}

/* 弹窗选择器的一行：左侧名称，右侧对当前项显示勾选标记。 */
static GtkListBoxRow *
mt_window_properties_picker_row(const gchar *name, guint index, gboolean selected)
{
    GtkWidget *row;
    GtkWidget *box;
    GtkWidget *label;
    GtkWidget *check;

    row = gtk_list_box_row_new();
    g_object_set_data(G_OBJECT(row), "vellum-picker-index", GUINT_TO_POINTER(index));
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    label = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    check = gtk_image_new_from_icon_name("object-select-symbolic");
    gtk_widget_set_visible(check, selected);
    gtk_box_append(GTK_BOX(box), label);
    gtk_box_append(GTK_BOX(box), check);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);

    return GTK_LIST_BOX_ROW(row);
}

/* 按搜索词重建弹窗列表；items 里每个条目的 index 指向底层数组。 */
void
mt_window_properties_picker_populate(GtkListBox *list,
                                     GPtrArray *items,
                                     guint selected_index,
                                     const gchar *filter)
{
    guint index;

    while (TRUE)
    {
        GtkWidget *child;

        child = gtk_widget_get_first_child(GTK_WIDGET(list));
        if (child == NULL)
        {
            break;
        }
        gtk_list_box_remove(list, child);
    }

    for (index = 0; index < items->len; index++)
    {
        MtPropertiesItem *item;

        item = g_ptr_array_index(items, index);
        if (!mt_window_properties_name_matches(item->name, filter))
        {
            continue;
        }
        gtk_list_box_append(list,
                            GTK_WIDGET(mt_window_properties_picker_row(item->name,
                                                                       item->index,
                                                                       item->index == selected_index)));
    }
}

void
mt_window_properties_type_dialog_search_changed(GtkSearchEntry *entry, gpointer user_data)
{
    AdwDialog *dialog;
    MtPropertiesPanel *panel;
    GtkWidget *list;

    dialog = ADW_DIALOG(user_data);
    panel = g_object_get_data(G_OBJECT(dialog), "vellum-type-panel");
    list = g_object_get_data(G_OBJECT(dialog), "vellum-type-list");
    if (panel != NULL && list != NULL)
    {
        mt_window_properties_picker_populate(GTK_LIST_BOX(list),
                                             panel->type_items,
                                             panel->type_selected,
                                             gtk_editable_get_text(GTK_EDITABLE(entry)));
    }
}

void
mt_window_properties_type_dialog_row_activated(GtkListBox *list,
                                               GtkListBoxRow *row,
                                               gpointer user_data)
{
    AdwDialog *dialog;
    MtPropertiesPanel *panel;
    guint index;

    (void)list;
    dialog = ADW_DIALOG(user_data);
    panel = g_object_get_data(G_OBJECT(dialog), "vellum-type-panel");
    index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "vellum-picker-index"));
    if (panel != NULL)
    {
        mt_window_properties_type_picked(panel, index, NULL);
    }
    adw_dialog_close(dialog);
}

/* 原版把文档类型作为独立、可搜索的自适应选择器；窄屏时由 AdwDialog 自动表现为抽屉。 */
void
mt_window_properties_type_dialog_show(AdwActionRow *row, gpointer user_data)
{
    MtPropertiesPanel *panel;
    AdwDialog *dialog;
    GtkWidget *toolbar;
    GtkWidget *header;
    GtkWidget *box;
    GtkWidget *search;
    GtkWidget *scroller;
    GtkWidget *list;

    (void)row;
    panel = user_data;
    dialog = adw_dialog_new();
    adw_dialog_set_title(dialog, _("Document Type"));
    adw_dialog_set_content_width(dialog, 360);
    adw_dialog_set_content_height(dialog, 520);
    adw_dialog_set_presentation_mode(dialog, ADW_DIALOG_AUTO);

    toolbar = adw_toolbar_view_new();
    header = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 12);
    search = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(search), _("Search"));
    scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);
    list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), list);
    gtk_box_append(GTK_BOX(box), search);
    gtk_box_append(GTK_BOX(box), scroller);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), box);
    adw_dialog_set_child(dialog, toolbar);

    g_object_set_data(G_OBJECT(dialog), "vellum-type-panel", panel);
    g_object_set_data(G_OBJECT(dialog), "vellum-type-list", list);
    g_signal_connect(search,
                     "search-changed",
                     G_CALLBACK(mt_window_properties_type_dialog_search_changed),
                     dialog);
    g_signal_connect(list,
                     "row-activated",
                     G_CALLBACK(mt_window_properties_type_dialog_row_activated),
                     dialog);
    mt_window_properties_picker_populate(GTK_LIST_BOX(list),
                                         panel->type_items,
                                         panel->type_selected,
                                         "");
    adw_dialog_present(dialog, GTK_WIDGET(panel->window->window));
    adw_dialog_set_focus(dialog, search);
}

void
mt_window_properties_type_picked(MtPropertiesPanel *panel,
                                 guint index,
                                 gpointer user_data)
{
    MtDocument *document;
    GtkSourceLanguageManager *manager;
    GtkSourceLanguage *language;
    const gchar *id;

    (void)user_data;
    document = mt_window_get_current_document(panel->window);
    if (document == NULL)
    {
        return;
    }

    id = NULL;
    if (index > 0 && index - 1 < panel->type_language_ids->len)
    {
        id = g_ptr_array_index(panel->type_language_ids, index - 1);
    }
    manager = gtk_source_language_manager_get_default();
    language = id != NULL ? gtk_source_language_manager_get_language(manager, id) : NULL;
    gtk_source_buffer_set_language(mt_document_get_buffer(document), language);

    panel->type_selected = index;
    mt_window_update_language_label(panel->window);
    mt_window_properties_refresh_metadata(panel);
}
