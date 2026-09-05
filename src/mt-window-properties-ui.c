#include "mt-window-private.h"

void
mt_window_properties_encoding_picked(MtPropertiesPanel *panel,
                                     guint index,
                                     gpointer user_data)
{
    MtDocument *document;
    GtkSourceEncoding *encoding;

    (void)user_data;
    document = mt_window_get_current_document(panel->window);
    if (document == NULL)
    {
        return;
    }

    encoding = index < panel->encoding_items->len ?
               g_ptr_array_index(panel->encoding_items, index) : NULL;
    if (encoding == NULL)
    {
        return;
    }
    panel->encoding_selected = index;

    /* 未保存的修改不允许换编码重载，否则会丢失内容。 */
    if (mt_document_is_modified(document))
    {
        mt_window_properties_refresh_metadata(panel);
        mt_window_show_toast(panel->window,
                             _("Save or discard changes before changing the encoding"));
        return;
    }

    if (document->source_file == NULL || mt_document_get_file(document) == NULL)
    {
        mt_window_properties_refresh_metadata(panel);
        mt_window_show_toast(panel->window,
                             _("Save the file first to change its encoding"));
        return;
    }

    {
        MtFileRequest *request;

        request = mt_file_request_new(panel->window, document, NULL);
        mt_document_reload_with_encoding(document,
                                         encoding,
                                         mt_window_document_load_finished,
                                         request);
        mt_window_show_toast(panel->window, _("Reloading with the selected encoding…"));
    }
}

void
mt_window_properties_encoding_changed(GObject *object,
                                      GParamSpec *pspec,
                                      gpointer user_data)
{
    MtPropertiesPanel *panel;

    (void)pspec;
    panel = user_data;
    if (panel->refreshing_metadata)
    {
        return;
    }
    mt_window_properties_encoding_picked(panel,
                                         adw_combo_row_get_selected(ADW_COMBO_ROW(object)),
                                         NULL);
}

void
mt_window_properties_newline_changed(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    MtPropertiesPanel *panel;
    MtDocument *document;
    GtkSourceNewlineType newline;

    (void)object;
    (void)pspec;
    panel = user_data;
    if (panel->refreshing_metadata)
    {
        return;
    }
    document = mt_window_get_current_document(panel->window);
    if (document == NULL)
    {
        return;
    }

    switch (adw_combo_row_get_selected(panel->newline_row))
    {
        case 1:
            newline = GTK_SOURCE_NEWLINE_TYPE_CR_LF;
            break;
        case 2:
            newline = GTK_SOURCE_NEWLINE_TYPE_CR;
            break;
        default:
            newline = GTK_SOURCE_NEWLINE_TYPE_LF;
            break;
    }

    mt_window_properties_convert_newlines(panel, newline);
}

void
mt_window_properties_auto_indent_changed(GObject *object,
                                         GParamSpec *pspec,
                                         gpointer user_data)
{
    MtPropertiesPanel *panel;

    (void)object;
    (void)pspec;
    panel = user_data;
    mt_settings_set_auto_indent(panel->window->settings,
                                adw_switch_row_get_active(panel->auto_indent_row));
    mt_window_apply_editor_preferences(panel->window);
    mt_settings_save(panel->window->settings);
}

void
mt_window_properties_indent_char_changed(GObject *object,
                                         GParamSpec *pspec,
                                         gpointer user_data)
{
    MtPropertiesPanel *panel;

    (void)object;
    (void)pspec;
    panel = user_data;
    mt_settings_set_insert_spaces(panel->window->settings,
                                  adw_combo_row_get_selected(panel->indent_char_row) == 1);
    mt_window_apply_editor_preferences(panel->window);
    mt_settings_save(panel->window->settings);
}

void
mt_window_properties_tab_width_changed(AdwSpinRow *row, gpointer user_data)
{
    MtPropertiesPanel *panel;

    panel = user_data;
    mt_settings_set_tab_width(panel->window->settings,
                              (gint)gtk_spin_button_get_value(GTK_SPIN_BUTTON(row)));
    mt_window_apply_editor_preferences(panel->window);
    mt_settings_save(panel->window->settings);
}

void
mt_window_properties_indent_width_changed(AdwSpinRow *row, gpointer user_data)
{
    MtPropertiesPanel *panel;

    panel = user_data;
    mt_settings_set_indent_width(panel->window->settings,
                                 (gint)gtk_spin_button_get_value(GTK_SPIN_BUTTON(row)));
    mt_window_apply_editor_preferences(panel->window);
    mt_settings_save(panel->window->settings);
}

MtPropertiesPanel *
mt_window_properties_create(MtWindow *window)
{
    MtPropertiesPanel *panel;
    AdwPreferencesPage *page;
    AdwPreferencesGroup *group;
    AdwActionRow *row;

    panel = g_new0(MtPropertiesPanel, 1);
    panel->window = window;
    page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    /* 覆盖 AdwPreferencesPage 默认的内缩外边距，内容贴齐侧栏，避免“陷进去”。 */
    gtk_widget_add_css_class(GTK_WIDGET(page), "vellum-properties");
    /* 面板自身持有页面引用：隐藏时 gtk_stack_remove 会解除父引用，
     * 若没有这份引用，页面对像会在首次隐藏时被销毁，再次显示即段错误。 */
    panel->page = GTK_WIDGET(page);
    g_object_ref(panel->page);

    /* 文件元信息 */
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("File Name"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    adw_action_row_set_subtitle_selectable(row, TRUE);
    g_object_set(G_OBJECT(row), "activatable", TRUE, NULL);
    g_signal_connect(row, "activated", G_CALLBACK(mt_window_properties_name_activated), panel);
    panel->name_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));

    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Location"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    adw_action_row_set_subtitle_selectable(row, TRUE);
    g_object_set(G_OBJECT(row), "activatable", TRUE, NULL);
    adw_action_row_set_subtitle(row, _("Not saved yet"));
    g_signal_connect(row, "activated", G_CALLBACK(mt_window_properties_location_activated), panel);
    panel->location_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));

    /* 类型：行 + 弹出子窗口（学习原版 GNOME Text Editor 的语法高亮子菜单方式），
     * 顶部带搜索框，可以直接输入语言名过滤。 */
    panel->type_language_ids = g_ptr_array_new_with_free_func(g_free);
    panel->type_items = g_ptr_array_new_with_free_func(mt_properties_item_free);
    {
        GtkSourceLanguageManager *manager;
        const gchar * const *ids;
        MtPropertiesItem *item;
        guint index;

        item = g_new(MtPropertiesItem, 1);
        item->name = g_strdup(_("Plain Text"));
        item->index = 0;
        g_ptr_array_add(panel->type_items, item);

        manager = gtk_source_language_manager_get_default();
        ids = gtk_source_language_manager_get_language_ids(manager);
        for (index = 0; ids != NULL && ids[index] != NULL; index++)
        {
            GtkSourceLanguage *language;
            const gchar *name;

            g_ptr_array_add(panel->type_language_ids, g_strdup(ids[index]));
            item = g_new(MtPropertiesItem, 1);
            language = gtk_source_language_manager_get_language(manager, ids[index]);
            name = language != NULL ? gtk_source_language_get_name(language) : ids[index];
            item->name = g_strdup(name);
            item->index = index + 1;
            g_ptr_array_add(panel->type_items, item);
        }
    }
    panel->type_selected = 0;
    panel->type_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->type_row), _("Document Type"));
    adw_action_row_set_subtitle(panel->type_row, _("Plain Text"));
    g_object_set(G_OBJECT(panel->type_row), "activatable", TRUE, NULL);
    panel->type_button = gtk_image_new_from_icon_name("go-next-symbolic");
    gtk_widget_set_valign(panel->type_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(panel->type_button, 6);
    adw_action_row_add_suffix(panel->type_row, panel->type_button);
    gtk_widget_set_tooltip_text(GTK_WIDGET(panel->type_row), _("Change Document Type"));
    g_signal_connect(panel->type_row,
                     "activated",
                     G_CALLBACK(mt_window_properties_type_dialog_show),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->type_row));

    /* 编码沿用原版的 AdwComboRow：标准下拉列表，搜索行为由 Libadwaita 统一提供。 */
    /* GtkSourceEncoding 是静态单例，不参与引用计数。 */
    panel->encoding_items = g_ptr_array_new();
    panel->encoding_picker_items = g_ptr_array_new_with_free_func(mt_properties_item_free);
    panel->encoding_model = gtk_string_list_new(NULL);
    {
        GSList *encodings;
        GSList *link;
        guint index;

        encodings = gtk_source_encoding_get_all();
        index = 0;
        for (link = encodings; link != NULL; link = link->next)
        {
            GtkSourceEncoding *encoding;
            MtPropertiesItem *item;

            encoding = link->data;
            g_ptr_array_add(panel->encoding_items, encoding);
            item = g_new(MtPropertiesItem, 1);
            item->name = g_strdup(gtk_source_encoding_get_name(encoding));
            item->index = index;
            gtk_string_list_append(panel->encoding_model, item->name);
            g_ptr_array_add(panel->encoding_picker_items, item);
            index++;
        }
        g_slist_free(encodings);
    }
    panel->encoding_selected = 0;
    panel->encoding_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->encoding_row), _("Encoding"));
    adw_combo_row_set_model(panel->encoding_row, G_LIST_MODEL(panel->encoding_model));
    adw_combo_row_set_enable_search(panel->encoding_row, TRUE);
    adw_combo_row_set_selected(panel->encoding_row, 0);
    g_signal_connect(panel->encoding_row,
                     "notify::selected",
                     G_CALLBACK(mt_window_properties_encoding_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->encoding_row));

    /* 换行符：三种常用行尾。 */
    panel->newline_model = gtk_string_list_new(NULL);
    gtk_string_list_append(panel->newline_model, _("Unix (LF)"));
    gtk_string_list_append(panel->newline_model, _("Windows (CRLF)"));
    gtk_string_list_append(panel->newline_model, _("Classic Mac (CR)"));
    panel->newline_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->newline_row), _("Newline"));
    adw_combo_row_set_model(panel->newline_row, G_LIST_MODEL(panel->newline_model));
    adw_combo_row_set_selected(panel->newline_row, 0);
    g_signal_connect(panel->newline_row,
                     "notify::selected",
                     G_CALLBACK(mt_window_properties_newline_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->newline_row));
    adw_preferences_page_add(page, group);

    /* 缩进配置 */
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("Indentation"));
    panel->auto_indent_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->auto_indent_row),
                                  _("Auto Indentation"));
    adw_switch_row_set_active(panel->auto_indent_row,
                              mt_settings_get_auto_indent(window->settings));
    g_signal_connect(panel->auto_indent_row,
                     "notify::active",
                     G_CALLBACK(mt_window_properties_auto_indent_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->auto_indent_row));

    panel->indent_model = gtk_string_list_new(NULL);
    gtk_string_list_append(panel->indent_model, _("Tabs"));
    gtk_string_list_append(panel->indent_model, _("Spaces"));
    panel->indent_char_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->indent_char_row), _("Character"));
    adw_combo_row_set_model(panel->indent_char_row, G_LIST_MODEL(panel->indent_model));
    adw_combo_row_set_selected(panel->indent_char_row,
                               mt_settings_get_insert_spaces(window->settings) ? 1 : 0);
    g_signal_connect(panel->indent_char_row,
                     "notify::selected",
                     G_CALLBACK(mt_window_properties_indent_char_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->indent_char_row));

    panel->tab_width_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(1.0, 16.0, 1.0));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->tab_width_row),
                                  _("Spaces Per Tab"));
    gtk_adjustment_set_value(adw_spin_row_get_adjustment(panel->tab_width_row),
                             mt_settings_get_tab_width(window->settings));
    adw_spin_row_set_digits(panel->tab_width_row, 0);
    g_signal_connect(panel->tab_width_row,
                     "changed",
                     G_CALLBACK(mt_window_properties_tab_width_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->tab_width_row));

    panel->indent_width_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(1.0, 16.0, 1.0));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(panel->indent_width_row),
                                  _("Spaces Per Indent"));
    gtk_adjustment_set_value(adw_spin_row_get_adjustment(panel->indent_width_row),
                             mt_settings_get_indent_width(window->settings));
    adw_spin_row_set_digits(panel->indent_width_row, 0);
    g_signal_connect(panel->indent_width_row,
                     "changed",
                     G_CALLBACK(mt_window_properties_indent_width_changed),
                     panel);
    adw_preferences_group_add(group, GTK_WIDGET(panel->indent_width_row));
    adw_preferences_page_add(page, group);

    /* 统计 */
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("Statistics"));
    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Lines"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    panel->lines_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));

    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Words"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    panel->words_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));

    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("Characters, No Spaces"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    panel->chars_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));

    row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), _("All Characters"));
    adw_preferences_row_set_use_markup(ADW_PREFERENCES_ROW(row), FALSE);
    panel->all_chars_row = row;
    adw_preferences_group_add(group, GTK_WIDGET(row));
    adw_preferences_page_add(page, group);

    return panel;
}

/* ---------- 主菜单（学习原版 GNOME Text Editor 的分组汉堡菜单） ---------- */

/* ---------- 主菜单顶部：主题切换与缩放控件（学习原版 GNOME Text Editor） ---------- */

void
mt_window_menu_theme_toggled(GtkCheckButton *button, gpointer user_data)
{
    MtWindow *window;
    const gchar *mode;
    MtAppearance appearance;

    window = user_data;
    if (window == NULL || window->disposed || !gtk_check_button_get_active(button))
    {
        return;
    }

    mode = g_object_get_data(G_OBJECT(button), "vellum-theme-mode");
    if (g_strcmp0(mode, "light") == 0)
    {
        appearance = MT_APPEARANCE_LIGHT;
    }
    else if (g_strcmp0(mode, "dark") == 0)
    {
        appearance = MT_APPEARANCE_DARK;
    }
    else
    {
        appearance = MT_APPEARANCE_SYSTEM;
    }

    /* 与首选项里的 Appearance 下拉框共用同一状态机，避免重复应用。 */
    if (mt_settings_get_appearance(window->settings) == appearance)
    {
        return;
    }

    mt_timing_log("menu-theme-toggled: %s", mode);
    mt_settings_set_appearance(window->settings, appearance);
    mt_settings_apply_appearance(window->settings);
    mt_window_apply_editor_style(window);
    mt_settings_save(window->settings);
}

void
mt_window_update_menu_theme_buttons(MtWindow *window)
{
    MtAppearance appearance;

    if (window == NULL || window->menu_theme_follow == NULL)
    {
        return;
    }

    appearance = mt_settings_get_appearance(window->settings);
    gtk_check_button_set_active(window->menu_theme_follow, appearance == MT_APPEARANCE_SYSTEM);
    gtk_check_button_set_active(window->menu_theme_light, appearance == MT_APPEARANCE_LIGHT);
    gtk_check_button_set_active(window->menu_theme_dark, appearance == MT_APPEARANCE_DARK);
}

void
mt_window_update_menu_zoom_label(MtWindow *window)
{
    gchar *text;

    if (window == NULL || window->menu_zoom_label == NULL)
    {
        return;
    }

    text = g_strdup_printf("%d%%",
                           (gint)(mt_settings_get_font_scale(window->settings) * 100.0 + 0.5));
    gtk_button_set_label(window->menu_zoom_label, text);
    g_free(text);
}

GtkWidget *
mt_window_build_theme_selector(MtWindow *window)
{
    GtkWidget *box;
    GtkCheckButton *follow;
    GtkCheckButton *light;
    GtkCheckButton *dark;

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_add_css_class(box, "vellum-menu-theme");

    follow = GTK_CHECK_BUTTON(gtk_check_button_new());
    gtk_widget_add_css_class(GTK_WIDGET(follow), "theme-selector");
    gtk_widget_add_css_class(GTK_WIDGET(follow), "follow");
    gtk_widget_set_hexpand(GTK_WIDGET(follow), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(follow), GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(GTK_WIDGET(follow), _("Follow System Style"));
    g_object_set_data(G_OBJECT(follow), "vellum-theme-mode", (gpointer)"follow");
    g_signal_connect(follow, "toggled", G_CALLBACK(mt_window_menu_theme_toggled), window);

    light = GTK_CHECK_BUTTON(gtk_check_button_new());
    gtk_widget_add_css_class(GTK_WIDGET(light), "theme-selector");
    gtk_widget_add_css_class(GTK_WIDGET(light), "light");
    gtk_widget_set_hexpand(GTK_WIDGET(light), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(light), GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(GTK_WIDGET(light), _("Light Style"));
    g_object_set_data(G_OBJECT(light), "vellum-theme-mode", (gpointer)"light");
    g_signal_connect(light, "toggled", G_CALLBACK(mt_window_menu_theme_toggled), window);

    dark = GTK_CHECK_BUTTON(gtk_check_button_new());
    gtk_widget_add_css_class(GTK_WIDGET(dark), "theme-selector");
    gtk_widget_add_css_class(GTK_WIDGET(dark), "dark");
    gtk_widget_set_hexpand(GTK_WIDGET(dark), TRUE);
    gtk_widget_set_halign(GTK_WIDGET(dark), GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(GTK_WIDGET(dark), _("Dark Style"));
    g_object_set_data(G_OBJECT(dark), "vellum-theme-mode", (gpointer)"dark");
    g_signal_connect(dark, "toggled", G_CALLBACK(mt_window_menu_theme_toggled), window);

    /* 三个按钮组成单选组；跟随系统为锚点。 */
    gtk_check_button_set_group(light, follow);
    gtk_check_button_set_group(dark, follow);

    window->menu_theme_follow = follow;
    window->menu_theme_light = light;
    window->menu_theme_dark = dark;

    gtk_box_append(GTK_BOX(box), GTK_WIDGET(follow));
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(light));
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(dark));

    return box;
}

GtkWidget *
mt_window_build_zoom_box(MtWindow *window)
{
    GtkWidget *box;
    GtkWidget *zoom_out;
    GtkWidget *zoom_in;

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(box, TRUE);
    gtk_widget_set_margin_start(box, 18);
    gtk_widget_set_margin_end(box, 18);

    zoom_out = gtk_button_new_from_icon_name("zoom-out-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(zoom_out), "win.zoom-out");
    gtk_widget_add_css_class(zoom_out, "circular");
    gtk_widget_add_css_class(zoom_out, "flat");
    gtk_widget_set_tooltip_text(zoom_out, _("Zoom Out"));

    window->menu_zoom_label = GTK_BUTTON(gtk_button_new_with_label("100%"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(window->menu_zoom_label), "win.zoom-reset");
    gtk_widget_add_css_class(GTK_WIDGET(window->menu_zoom_label), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(window->menu_zoom_label), "pill");
    gtk_widget_set_hexpand(GTK_WIDGET(window->menu_zoom_label), TRUE);
    gtk_widget_set_tooltip_text(GTK_WIDGET(window->menu_zoom_label), _("Reset Zoom"));

    zoom_in = gtk_button_new_from_icon_name("zoom-in-symbolic");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(zoom_in), "win.zoom-in");
    gtk_widget_add_css_class(zoom_in, "circular");
    gtk_widget_add_css_class(zoom_in, "flat");
    gtk_widget_set_tooltip_text(zoom_in, _("Zoom In"));

    gtk_box_append(GTK_BOX(box), zoom_out);
    gtk_box_append(GTK_BOX(box), GTK_WIDGET(window->menu_zoom_label));
    gtk_box_append(GTK_BOX(box), zoom_in);

    return box;
}

GMenu *
mt_window_build_primary_menu(MtWindow *window)
{
    GMenu *menu;
    GMenu *section;
    GMenuItem *item;

    menu = g_menu_new();

    /* 顶部：自定义主题切换器与缩放控件（由 GtkPopoverMenu 的 custom 子项提供）。 */
    section = g_menu_new();
    item = g_menu_item_new(NULL, NULL);
    g_menu_item_set_attribute(item, "custom", "s", "theme");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(NULL, NULL);
    g_menu_item_set_attribute(item, "custom", "s", "zoom");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    item = g_menu_item_new(_("_New"), "win.new");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>n");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    item = g_menu_item_new(_("_Save"), "win.save");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>s");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("Save _As…"), "win.save-as");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary><Shift>s");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("_Close"), "win.close");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>w");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    item = g_menu_item_new(_("_Find…"), "win.find");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>f");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("_Replace…"), "win.replace");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>h");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    item = g_menu_item_new(_("_Document Properties"), "win.toggle-properties");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary><Shift>i");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    section = g_menu_new();
    item = g_menu_item_new(_("_Preferences"), "win.preferences");
    g_menu_item_set_attribute(item, "accel", "s", "<Primary>comma");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("_Keyboard Shortcuts"), "win.shortcuts");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("_Extensions"), "win.extensions");
    g_menu_append_item(section, item);
    g_object_unref(item);
    item = g_menu_item_new(_("_About Vellum"), "win.about");
    g_menu_append_item(section, item);
    g_object_unref(item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    g_object_unref(section);

    /* 新手引导入口：welcome 插件加载后动态追加，删除后自动移除。 */
    section = g_menu_new();
    g_menu_append_section(menu, NULL, G_MENU_MODEL(section));
    window->welcome_menu_section = section;

    return menu;
}

void
mt_window_sync_plugin_menu(MtWindow *window)
{
    MtPluginManager *manager;
    gboolean loaded;

    if (window == NULL || window->welcome_menu_section == NULL)
    {
        return;
    }

    manager = window->plugin_manager;
    loaded = manager != NULL &&
             mt_plugin_manager_has_plugin(manager, "io.github.vellum.welcome");

    if (loaded && !window->welcome_menu_present)
    {
        GMenuItem *item;

        item = g_menu_item_new(_("Welcome Guide"), "app.show-welcome");
        g_menu_append_item(window->welcome_menu_section, item);
        g_object_unref(item);
        window->welcome_menu_present = TRUE;
    }
    else if (!loaded && window->welcome_menu_present)
    {
        gint n_items;

        n_items = g_menu_model_get_n_items(G_MENU_MODEL(window->welcome_menu_section));
        if (n_items > 0)
        {
            g_menu_remove(window->welcome_menu_section, n_items - 1);
        }
        window->welcome_menu_present = FALSE;
    }
}

/* ---------- 清除历史（行为组里的红色危险操作） ---------- */

static gboolean
mt_window_draft_is_open(MtWindow *window, const gchar *path)
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
        if (document != NULL && document->draft_path != NULL &&
            g_strcmp0(document->draft_path, path) == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

void
mt_window_clear_history_response(AdwAlertDialog *dialog,
                                 const gchar *response,
                                 gpointer user_data)
{
    MtWindow *window;
    GPtrArray *snapshots;
    guint removed;
    guint index;

    (void)dialog;
    window = user_data;
    if (g_strcmp0(response, "clear") != 0)
    {
        return;
    }

    removed = 0;
    snapshots = mt_document_list_snapshots();
    for (index = 0; index < snapshots->len; index++)
    {
        const gchar *path;

        path = g_ptr_array_index(snapshots, index);
        if (!mt_window_draft_is_open(window, path) && g_remove(path) == 0)
        {
            removed++;
        }
    }
    g_ptr_array_unref(snapshots);

    if (removed > 0)
    {
        gchar *message;

        message = g_strdup_printf(ngettext("Cleared %u draft", "Cleared %u drafts", removed),
                                  removed);
        mt_window_show_toast(window, message);
        g_free(message);
    }
    else
    {
        mt_window_show_toast(window, _("No history to clear"));
    }
}

void
mt_window_clear_history_clicked(AdwActionRow *row, gpointer user_data)
{
    MtWindow *window;
    AdwAlertDialog *dialog;
    GtkWidget *content;
    GtkWidget *label;

    (void)row;
    window = user_data;

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    label = gtk_label_new(_("All recoverable drafts will be deleted permanently. "
                            "Open documents are not affected."));
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_halign(label, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(content), label);
    gtk_widget_set_visible(content, TRUE);

    dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new(_("Clear History?"), NULL));
    adw_alert_dialog_set_extra_child(dialog, content);
    adw_alert_dialog_add_responses(dialog,
                                   "cancel", _("Cancel"),
                                   "clear", _("Clear"),
                                   NULL);
    adw_alert_dialog_set_response_appearance(dialog, "clear", ADW_RESPONSE_DESTRUCTIVE);
    adw_alert_dialog_set_default_response(dialog, "cancel");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    g_signal_connect(dialog, "response", G_CALLBACK(mt_window_clear_history_response), window);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(window->window));
}
