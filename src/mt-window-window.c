/* ---------- 首选项：自定义字体开关 ---------- */

#include "mt-window-private.h"

void
mt_window_custom_font_toggled(GObject *object, GParamSpec *pspec, gpointer user_data)
{
    MtWindow *window;
    AdwSwitchRow *row;
    AdwActionRow *font_row;
    gboolean active;

    (void)pspec;
    window = user_data;
    row = ADW_SWITCH_ROW(object);
    active = adw_switch_row_get_active(row);
    mt_settings_set_custom_font(window->settings, active);

    font_row = g_object_get_data(G_OBJECT(row), "vellum-font-row");
    if (font_row != NULL)
    {
        gtk_widget_set_sensitive(GTK_WIDGET(font_row), active);
    }
    if (!active && g_strcmp0(mt_settings_get_font_family(window->settings), "Monospace") != 0)
    {
        mt_settings_set_font_family(window->settings, "Monospace");
        if (font_row != NULL)
        {
            adw_action_row_set_subtitle(font_row, "Monospace");
        }
    }
    mt_window_apply_editor_style(window);
    mt_settings_save(window->settings);
}

MtWindow *
mt_window_new(AdwApplication *application, MtSettings *settings)
{

    MtWindow *window;
    GtkWidget *toolbar_view;
    GtkWidget *header_bar;
    GtkWidget *open_button;
    GtkWidget *new_button;
    GtkWidget *menu_button;
    GMenu *menu;
    GtkWidget *content;
    GtkWidget *editor_content;
    GtkWidget *status_bar;
    GtkWidget *find_previous_button;
    GtkWidget *find_next_button;
    GtkWidget *find_close_button;
    GtkWidget *external_text_box;
    GtkWidget *external_title;
    GtkWidget *external_description;
    GtkWidget *external_reload_button;
    GtkWidget *external_close_button;
    GtkWidget *header_title_box;
    GtkWidget *header_title_row;
    GtkWidget *header_subtitle_row;
    GtkWidget *header_title_center;
    GtkWidget *find_navigation_box;
    GtkWidget *replace_mode_button;
    GtkWidget *find_options_button;
    GtkWidget *find_options_popover;
    GtkWidget *search_case_button;
    GtkWidget *replace_all_button;
    GtkEventController *key_controller;
    static const GActionEntry entries[] = {
        { "new", mt_window_action_new, NULL, NULL, NULL, { 0, 0, 0 } },
        { "open", mt_window_action_open, NULL, NULL, NULL, { 0, 0, 0 } },
        { "save", mt_window_action_save, NULL, NULL, NULL, { 0, 0, 0 } },
        { "save-as", mt_window_action_save_as, NULL, NULL, NULL, { 0, 0, 0 } },
        { "close", mt_window_action_close, NULL, NULL, NULL, { 0, 0, 0 } },
        { "find", mt_window_action_find, NULL, NULL, NULL, { 0, 0, 0 } },
        { "replace", mt_window_action_replace, NULL, NULL, NULL, { 0, 0, 0 } },
        { "zoom-in", mt_window_action_zoom_in, NULL, NULL, NULL, { 0, 0, 0 } },
        { "zoom-out", mt_window_action_zoom_out, NULL, NULL, NULL, { 0, 0, 0 } },
        { "zoom-reset", mt_window_action_zoom_reset, NULL, NULL, NULL, { 0, 0, 0 } },
        { "preferences", mt_window_action_preferences, NULL, NULL, NULL, { 0, 0, 0 } },
        { "shortcuts", mt_window_action_shortcuts, NULL, NULL, NULL, { 0, 0, 0 } },
        { "extensions", mt_window_action_extensions, NULL, NULL, NULL, { 0, 0, 0 } },
        { "about", mt_window_action_about, NULL, NULL, NULL, { 0, 0, 0 } },
        { "open-releases", mt_window_action_open_releases, NULL, NULL, NULL, { 0, 0, 0 } },
        { "toggle-properties", mt_window_toggle_properties, NULL, NULL, NULL, { 0, 0, 0 } },
        /* 断点 gutter 标记点击时触发；插件连接此动作的 activate 信号切换断点。 */
        { "toggle-breakpoint", mt_window_action_toggle_breakpoint, "i", NULL, NULL, { 0, 0, 0 } }
    };

    window = g_new0(MtWindow, 1);
    window->settings = settings;
    window->window = ADW_APPLICATION_WINDOW(adw_application_window_new(GTK_APPLICATION(application)));
    mt_window_register_source_icon_path(window->window);
    gtk_window_set_icon_name(GTK_WINDOW(window->window), "io.github.vellum.Vellum");
    window->tab_view = ADW_TAB_VIEW(adw_tab_view_new());
    window->tab_bar = ADW_TAB_BAR(adw_tab_bar_new());
    window->toast_overlay = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    window->css_provider = gtk_css_provider_new();
    window->display = g_object_ref(gtk_widget_get_display(GTK_WIDGET(window->window)));

    gtk_window_set_default_size(GTK_WINDOW(window->window), 980, 680);
    gtk_widget_set_size_request(GTK_WIDGET(window->window),
                                MT_MIN_WINDOW_WIDTH,
                                MT_MIN_WINDOW_HEIGHT);
    gtk_window_set_title(GTK_WINDOW(window->window), "Vellum");
    adw_tab_bar_set_view(window->tab_bar, window->tab_view);
    adw_tab_view_set_menu_model(window->tab_view, NULL);

    g_action_map_add_action_entries(G_ACTION_MAP(window->window),
                                    entries,
                                    G_N_ELEMENTS(entries),
                                    window);
    key_controller = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(key_controller, GTK_PHASE_CAPTURE);
    g_signal_connect(key_controller,
                     "key-pressed",
                     G_CALLBACK(mt_window_key_pressed),
                     window);
    gtk_widget_add_controller(GTK_WIDGET(window->window), key_controller);

    toolbar_view = adw_toolbar_view_new();
    header_bar = adw_header_bar_new();

    /* 左侧与 GNOME Text Editor 保持同一信息顺序：打开入口在前，
     * 新建标签在后；使用 Vellum 自己的动作和图标资源。 */
    open_button = gtk_button_new();
    {
        GtkWidget *open_content;
        GtkWidget *open_label;
        GtkWidget *open_arrow;

        open_content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        open_label = gtk_label_new_with_mnemonic(_("_Open"));
        open_arrow = gtk_image_new_from_icon_name("pan-down-symbolic");
        gtk_box_append(GTK_BOX(open_content), open_label);
        gtk_box_append(GTK_BOX(open_content), open_arrow);
        gtk_button_set_child(GTK_BUTTON(open_button), open_content);
    }
    new_button = gtk_button_new_from_icon_name("tab-new-symbolic");
    gtk_widget_set_tooltip_text(new_button, _("New Tab"));
    gtk_widget_set_tooltip_text(open_button, _("Open File"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(new_button), "win.new");
    gtk_actionable_set_action_name(GTK_ACTIONABLE(open_button), "win.open");
    adw_header_bar_pack_start(ADW_HEADER_BAR(header_bar), open_button);
    adw_header_bar_pack_start(ADW_HEADER_BAR(header_bar), new_button);

    /* 标题栏中部使用标题、目录和独立的未保存圆点，避免修改文档时
     * 文字横向跳动，同时保持原生 GNOME 的视觉层级。 */
    header_title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    header_title_row = gtk_center_box_new();
    header_subtitle_row = gtk_center_box_new();
    header_title_center = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    window->header_modified_indicator = gtk_label_new("•");
    window->header_title_label = GTK_LABEL(gtk_label_new("Vellum"));
    window->header_subtitle_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(header_title_box, "vellum-header-title");
    gtk_widget_add_css_class(GTK_WIDGET(window->header_title_label), "title");
    gtk_widget_add_css_class(GTK_WIDGET(window->header_subtitle_label), "subtitle");
    gtk_widget_add_css_class(window->header_modified_indicator, "vellum-header-modified");
    gtk_label_set_ellipsize(window->header_title_label, PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_ellipsize(window->header_subtitle_label, PANGO_ELLIPSIZE_MIDDLE);
    gtk_widget_set_visible(window->header_modified_indicator, FALSE);
    gtk_box_append(GTK_BOX(header_title_center), window->header_modified_indicator);
    gtk_box_append(GTK_BOX(header_title_center), GTK_WIDGET(window->header_title_label));
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(header_title_row), header_title_center);
    gtk_center_box_set_center_widget(GTK_CENTER_BOX(header_subtitle_row),
                                     GTK_WIDGET(window->header_subtitle_label));
    gtk_box_append(GTK_BOX(header_title_box), header_title_row);
    gtk_box_append(GTK_BOX(header_title_box), header_subtitle_row);
    gtk_widget_set_hexpand(header_title_box, TRUE);
    gtk_widget_set_valign(header_title_box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(header_title_box, 12);
    gtk_widget_set_margin_end(header_title_box, 12);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header_bar), header_title_box);

    menu = mt_window_build_primary_menu(window);
    window->primary_menu = menu;
    menu_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(menu_button), "open-menu-symbolic");
    gtk_widget_set_tooltip_text(menu_button, _("Main Menu"));
    {
        GtkPopoverMenu *popover;

        popover = GTK_POPOVER_MENU(gtk_popover_menu_new_from_model(G_MENU_MODEL(menu)));
        gtk_popover_menu_add_child(popover,
                                   mt_window_build_theme_selector(window),
                                   "theme");
        gtk_popover_menu_add_child(popover,
                                   mt_window_build_zoom_box(window),
                                   "zoom");
        gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button), GTK_WIDGET(popover));
    }
    mt_window_update_menu_theme_buttons(window);
    mt_window_update_menu_zoom_label(window);
    window->properties_button = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
    gtk_button_set_icon_name(GTK_BUTTON(window->properties_button), "info-outline-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(window->properties_button),
                                _("Document Properties"));
    gtk_widget_add_css_class(GTK_WIDGET(window->properties_button), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(window->properties_button), "vellum-properties-toggle");
    g_signal_connect(window->properties_button,
                     "toggled",
                     G_CALLBACK(mt_window_properties_toggled),
                     window);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar), menu_button);
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar),
                            GTK_WIDGET(window->properties_button));

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    window->external_change_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    external_text_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    external_title = gtk_label_new(_("File changed on disk"));
    external_description = gtk_label_new(_("The file was changed by another program."));
    external_reload_button = gtk_button_new_with_label(_("Discard Changes and Reload"));
    external_close_button = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_add_css_class(window->external_change_bar, "vellum-external-change");
    gtk_widget_add_css_class(external_title, "vellum-external-title");
    gtk_widget_add_css_class(external_description, "vellum-external-description");
    gtk_widget_add_css_class(external_reload_button, "suggested-action");
    gtk_label_set_xalign(GTK_LABEL(external_title), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(external_description), 0.0f);
    gtk_widget_set_hexpand(external_text_box, TRUE);
    gtk_widget_set_margin_start(window->external_change_bar, 12);
    gtk_widget_set_margin_end(window->external_change_bar, 8);
    gtk_widget_set_margin_top(window->external_change_bar, 8);
    gtk_widget_set_margin_bottom(window->external_change_bar, 8);
    gtk_widget_set_valign(external_reload_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(external_reload_button, 12);
    gtk_widget_set_margin_end(external_reload_button, 4);
    gtk_widget_set_tooltip_text(external_close_button, _("Dismiss external file change notification"));
    gtk_box_append(GTK_BOX(external_text_box), external_title);
    gtk_box_append(GTK_BOX(external_text_box), external_description);
    gtk_box_append(GTK_BOX(window->external_change_bar), external_text_box);
    gtk_box_append(GTK_BOX(window->external_change_bar), external_reload_button);
    gtk_box_append(GTK_BOX(window->external_change_bar), external_close_button);
    gtk_widget_set_visible(window->external_change_bar, FALSE);
    g_signal_connect(external_reload_button,
                     "clicked",
                     G_CALLBACK(mt_window_external_reload_clicked),
                     window);
    g_signal_connect(external_close_button,
                     "clicked",
                     G_CALLBACK(mt_window_external_change_close_clicked),
                     window);
    gtk_box_append(GTK_BOX(content), window->external_change_bar);
    window->content_paned = GTK_PANED(gtk_paned_new(GTK_ORIENTATION_HORIZONTAL));
    window->sidebar_stack = GTK_STACK(gtk_stack_new());
    window->auxiliary_stack = GTK_STACK(gtk_stack_new());
    window->auxiliary_wrap = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window->auxiliary_wrap),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(window->auxiliary_wrap),
                                              MT_PROPERTIES_PANEL_WIDTH);
    gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(window->auxiliary_wrap),
                                              MT_PROPERTIES_PANEL_WIDTH);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(window->auxiliary_wrap),
                                  GTK_WIDGET(window->auxiliary_stack));
    gtk_widget_add_css_class(GTK_WIDGET(window->auxiliary_wrap), "vellum-properties-drawer");

    /* 使用 GNOME 原生 OverlaySplitView：大窗口内固定宽度停靠，小窗口改为右侧覆盖式抽屉。 */
    window->auxiliary_split = ADW_OVERLAY_SPLIT_VIEW(adw_overlay_split_view_new());
    adw_overlay_split_view_set_sidebar_position(window->auxiliary_split, GTK_PACK_END);
    adw_overlay_split_view_set_min_sidebar_width(window->auxiliary_split,
                                                  MT_PROPERTIES_PANEL_WIDTH);
    adw_overlay_split_view_set_max_sidebar_width(window->auxiliary_split,
                                                  MT_PROPERTIES_PANEL_WIDTH);
    adw_overlay_split_view_set_sidebar_width_fraction(window->auxiliary_split, 0.30);
    adw_overlay_split_view_set_pin_sidebar(window->auxiliary_split, TRUE);
    adw_overlay_split_view_set_enable_show_gesture(window->auxiliary_split, TRUE);
    adw_overlay_split_view_set_enable_hide_gesture(window->auxiliary_split, TRUE);
    g_signal_connect(window->auxiliary_split,
                     "notify::width",
                     G_CALLBACK(mt_window_auxiliary_width_changed),
                     window);

    editor_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);


    gtk_stack_set_transition_type(window->sidebar_stack,
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_stack_set_transition_type(window->auxiliary_stack,
                                  GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_widget_set_size_request(GTK_WIDGET(window->sidebar_stack), 220, -1);
    window->properties = mt_window_properties_create(window);
    gtk_widget_set_visible(GTK_WIDGET(window->sidebar_stack), FALSE);
    mt_window_auxiliary_visible(window, FALSE);
    gtk_widget_set_vexpand(GTK_WIDGET(window->content_paned), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(window->content_paned), TRUE);

    gtk_box_append(GTK_BOX(editor_content), GTK_WIDGET(window->tab_bar));
    gtk_box_append(GTK_BOX(editor_content), GTK_WIDGET(window->tab_view));
    gtk_paned_set_start_child(window->content_paned, GTK_WIDGET(window->sidebar_stack));
    gtk_paned_set_end_child(window->content_paned, GTK_WIDGET(window->auxiliary_split));
    gtk_widget_set_hexpand(editor_content, TRUE);
    adw_overlay_split_view_set_content(window->auxiliary_split, editor_content);
    adw_overlay_split_view_set_sidebar(window->auxiliary_split,
                                       GTK_WIDGET(window->auxiliary_wrap));
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(window->content_paned));

    /* 搜索栏采用与 GNOME Text Editor 相同的两行网格结构：查找、
     * 导航、模式/选项和关闭控件位于第一行；替换控件按需展开到第二行。 */
    window->find_bar = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(window->find_bar), 3);
    gtk_grid_set_row_spacing(GTK_GRID(window->find_bar), 3);
    gtk_widget_add_css_class(window->find_bar, "searchbar");
    gtk_widget_add_css_class(window->find_bar, "toolbar");
    gtk_widget_set_margin_start(window->find_bar, 12);
    gtk_widget_set_margin_end(window->find_bar, 12);
    gtk_widget_set_margin_top(window->find_bar, 6);
    gtk_widget_set_margin_bottom(window->find_bar, 6);
    window->find_revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(window->find_revealer),
                                     GTK_REVEALER_TRANSITION_TYPE_SLIDE_UP);
    gtk_revealer_set_transition_duration(GTK_REVEALER(window->find_revealer), 180);
    gtk_revealer_set_reveal_child(GTK_REVEALER(window->find_revealer), FALSE);
    window->find_entry = GTK_SEARCH_ENTRY(gtk_search_entry_new());
    window->replace_entry = GTK_ENTRY(gtk_entry_new());
    gtk_search_entry_set_placeholder_text(window->find_entry, _("Find"));
    gtk_entry_set_placeholder_text(window->replace_entry, _("Replace"));
    gtk_entry_set_icon_from_icon_name(window->replace_entry,
                                      GTK_ENTRY_ICON_PRIMARY,
                                      "edit-find-replace-symbolic");
    gtk_widget_set_hexpand(GTK_WIDGET(window->find_entry), TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(window->replace_entry), TRUE);
    g_object_set(window->replace_entry, "max-width-chars", 20, NULL);

    find_navigation_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
    gtk_box_set_homogeneous(GTK_BOX(find_navigation_box), TRUE);
    find_previous_button = gtk_button_new_from_icon_name("go-up-symbolic");
    find_next_button = gtk_button_new_from_icon_name("go-down-symbolic");
    gtk_widget_set_tooltip_text(find_previous_button, _("Previous Match"));
    gtk_widget_set_tooltip_text(find_next_button, _("Next Match"));
    gtk_widget_add_css_class(find_previous_button, "flat");
    gtk_widget_add_css_class(find_next_button, "flat");
    gtk_widget_set_focus_on_click(find_previous_button, FALSE);
    gtk_widget_set_focus_on_click(find_next_button, FALSE);
    gtk_box_append(GTK_BOX(find_navigation_box), find_previous_button);
    gtk_box_append(GTK_BOX(find_navigation_box), find_next_button);

    replace_mode_button = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(replace_mode_button), "edit-find-replace-symbolic");
    window->replace_mode_button = GTK_TOGGLE_BUTTON(replace_mode_button);
    gtk_widget_set_tooltip_text(replace_mode_button, _("Search and Replace"));
    gtk_widget_add_css_class(replace_mode_button, "flat");
    gtk_widget_set_focus_on_click(replace_mode_button, FALSE);

    find_options_button = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(find_options_button), "emblem-system-symbolic");
    gtk_widget_set_tooltip_text(find_options_button, _("Search Options"));
    gtk_widget_add_css_class(find_options_button, "flat");
    find_options_popover = gtk_popover_new();
    search_case_button = gtk_check_button_new_with_mnemonic(_("_Case Sensitive"));
    gtk_widget_set_margin_start(search_case_button, 12);
    gtk_widget_set_margin_end(search_case_button, 12);
    gtk_widget_set_margin_top(search_case_button, 9);
    gtk_widget_set_margin_bottom(search_case_button, 9);
    gtk_popover_set_child(GTK_POPOVER(find_options_popover), search_case_button);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(find_options_button), find_options_popover);

    window->replace_button = gtk_button_new_with_mnemonic(_("_Replace"));
    replace_all_button = gtk_button_new_with_mnemonic(_("Replace _All"));
    window->replace_all_button = replace_all_button;
    gtk_widget_set_focus_on_click(window->replace_button, FALSE);
    gtk_widget_set_focus_on_click(window->replace_all_button, FALSE);
    mt_window_set_replace_mode(window, FALSE);

    find_close_button = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_widget_set_tooltip_text(find_close_button, _("Close Search"));
    gtk_widget_add_css_class(find_close_button, "flat");
    gtk_widget_add_css_class(find_close_button, "circular");
    gtk_widget_set_focus_on_click(find_close_button, FALSE);

    gtk_grid_attach(GTK_GRID(window->find_bar), GTK_WIDGET(window->find_entry), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), find_navigation_box, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), replace_mode_button, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), find_options_button, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), find_close_button, 4, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), GTK_WIDGET(window->replace_entry), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), window->replace_button, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(window->find_bar), window->replace_all_button, 2, 1, 3, 1);
    gtk_revealer_set_child(GTK_REVEALER(window->find_revealer), window->find_bar);
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(window->find_revealer));

    status_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(status_bar, "vellum-statusbar");
    window->language_label = GTK_LABEL(gtk_label_new(_("Plain Text")));
    gtk_widget_set_halign(GTK_WIDGET(window->language_label), GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(window->language_label));
    window->position_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_set_halign(GTK_WIDGET(window->position_label), GTK_ALIGN_END);
    gtk_widget_set_hexpand(GTK_WIDGET(window->position_label), TRUE);
    gtk_label_set_attributes(window->position_label, mt_window_statusbar_attrs());
    gtk_box_append(GTK_BOX(status_bar), GTK_WIDGET(window->position_label));
    gtk_box_append(GTK_BOX(content), status_bar);

    adw_toast_overlay_set_child(window->toast_overlay, content);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), GTK_WIDGET(window->toast_overlay));
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header_bar);
    adw_application_window_set_content(window->window, toolbar_view);

    gtk_widget_add_css_class(GTK_WIDGET(window->tab_view), "vellum-tab-view");
    gtk_style_context_add_provider_for_display(gtk_widget_get_display(GTK_WIDGET(window->window)),
                                               GTK_STYLE_PROVIDER(window->css_provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    mt_window_apply_font_scale(window);

    window->style_manager_handler_id = g_signal_connect(adw_style_manager_get_default(),
                                                        "notify::dark",
                                                        G_CALLBACK(mt_window_system_dark_changed),
                                                        window);
    g_signal_connect(window->window,
                     "destroy",
                     G_CALLBACK(mt_window_destroyed),
                     window);
    g_signal_connect(window->tab_view,
                     "close-page",
                     G_CALLBACK(mt_window_close_page_requested),
                     window);
    g_signal_connect(window->tab_view,
                     "notify::selected-page",
                     G_CALLBACK(mt_window_page_selected),
                     window);
    g_signal_connect(find_previous_button,
                     "clicked",
                     G_CALLBACK(mt_window_find_previous_clicked),
                     window);
    g_signal_connect(find_next_button,
                     "clicked",
                     G_CALLBACK(mt_window_find_next_clicked),
                     window);
    g_signal_connect(window->replace_button,
                     "clicked",
                     G_CALLBACK(mt_window_replace_clicked),
                     window);
    g_signal_connect(window->replace_all_button,
                     "clicked",
                     G_CALLBACK(mt_window_replace_all_clicked),
                     window);
    g_signal_connect(window->replace_mode_button,
                     "toggled",
                     G_CALLBACK(mt_window_replace_mode_toggled),
                     window);
    g_signal_connect(search_case_button,
                     "toggled",
                     G_CALLBACK(mt_window_search_case_toggled),
                     window);
    g_signal_connect(find_close_button,
                     "clicked",
                     G_CALLBACK(mt_window_hide_find_bar),
                     window);
    g_signal_connect(window->find_entry,
                     "activate",
                     G_CALLBACK(mt_window_find_next_clicked),
                     window);

    if (mt_settings_get_auto_check_updates(window->settings))
    {
        window->auto_update_source_id = g_timeout_add_seconds(5,
                                                              mt_window_auto_update_check_idle,
                                                              window);
    }

    return window;
}

void
mt_window_destroyed(GtkWidget *widget, gpointer user_data)
{
    MtWindow *window;

    (void)widget;
    window = user_data;
    if (window == NULL || window->disposed)
    {
        return;
    }

    window->disposed = TRUE;
    if (window->snapshot_source_id != 0)
    {
        g_source_remove(window->snapshot_source_id);
        window->snapshot_source_id = 0;
    }
    /* 退出前把每个未保存文档的最新内容写入草稿，保证“加载上次的”。 */
    mt_window_snapshot_all(window);
    if (window->language_source_id != 0)
    {
        g_source_remove(window->language_source_id);
        window->language_source_id = 0;
    }
    if (window->style_manager_handler_id != 0)
    {
        g_signal_handler_disconnect(adw_style_manager_get_default(),
                                    window->style_manager_handler_id);
        window->style_manager_handler_id = 0;
    }
    if (window->auto_update_source_id != 0)
    {
        g_source_remove(window->auto_update_source_id);
        window->auto_update_source_id = 0;
    }
    /* 进行中的自动检查在回调里自行释放；此处仅取消请求避免界面已销毁后弹窗。 */
    if (window->auto_update_check != NULL)
    {
        MtAutoUpdateCheck *check;

        check = window->auto_update_check;
        if (check->cancellable != NULL)
        {
            g_cancellable_cancel(check->cancellable);
        }
    }

    /* 清除编译器报错下划线与断点标记，避免释放后悬挂引用。 */
    mt_window_clear_error_underlines(window);
    mt_window_clear_all_breakpoints(window);

    window->tab_view = NULL;
    window->tab_bar = NULL;
    window->toast_overlay = NULL;
    window->find_entry = NULL;
    window->replace_entry = NULL;
    window->find_bar = NULL;
    window->find_revealer = NULL;
    window->sidebar_stack = NULL;
    window->auxiliary_stack = NULL;
    window->auxiliary_wrap = NULL;
    window->content_paned = NULL;
    window->auxiliary_split = NULL;
    window->language_label = NULL;
    window->position_label = NULL;
    window->header_title_label = NULL;
    window->header_subtitle_label = NULL;
    window->header_modified_indicator = NULL;
    window->replace_mode_button = NULL;
    window->replace_button = NULL;
    window->replace_all_button = NULL;
    window->inline_completion_document = NULL;
}

void
mt_window_free(MtWindow *window)
{
    if (window == NULL)
    {
        return;
    }

    mt_scheme_apply_cancel(window);
    if (window->snapshot_source_id != 0)
    {
        g_source_remove(window->snapshot_source_id);
    }

    if (window->language_source_id != 0)
    {
        g_source_remove(window->language_source_id);
    }

    if (window->properties_source_id != 0)
    {
        g_source_remove(window->properties_source_id);
        window->properties_source_id = 0;
    }

    /* 主循环已退出，不会再派发回调，这里兜底释放未完成的自动检查。 */
    if (window->auto_update_check != NULL)
    {
        mt_auto_update_check_free(window->auto_update_check);
        window->auto_update_check = NULL;
    }

    if (window->properties != NULL)
    {
        MtPropertiesPanel *panel;

        panel = window->properties;
        if (panel->indent_model != NULL)
        {
            g_object_unref(panel->indent_model);
        }
        if (panel->newline_model != NULL)
        {
            g_object_unref(panel->newline_model);
        }
        if (panel->type_language_ids != NULL)
        {
            g_ptr_array_unref(panel->type_language_ids);
        }
        if (panel->type_items != NULL)
        {
            g_ptr_array_unref(panel->type_items);
        }
        if (panel->encoding_items != NULL)
        {
            g_ptr_array_unref(panel->encoding_items);
        }
        if (panel->encoding_picker_items != NULL)
        {
            g_ptr_array_unref(panel->encoding_picker_items);
        }
        g_clear_object(&panel->encoding_model);
        g_clear_object(&panel->page);
        g_free(panel);
        window->properties = NULL;
    }

    if (window->style_manager_handler_id != 0)
    {
        g_signal_handler_disconnect(adw_style_manager_get_default(),
                                    window->style_manager_handler_id);
    }

    if (window->css_provider != NULL && window->display != NULL)
    {
        gtk_style_context_remove_provider_for_display(window->display,
                                                      GTK_STYLE_PROVIDER(window->css_provider));
    }

    g_clear_object(&window->css_provider);
    g_clear_object(&window->display);
    g_clear_object(&window->welcome_menu_section);
    g_clear_object(&window->primary_menu);
    /* 编译器报错下划线与断点标记的残留数据。 */
    mt_window_clear_error_underlines(window);
    mt_window_clear_all_breakpoints(window);
    g_free(window);
}

GtkWindow *
mt_window_get_gtk_window(MtWindow *window)
{
    if (window == NULL || window->disposed || !GTK_IS_WINDOW(window->window))
    {
        return NULL;
    }

    return GTK_WINDOW(window->window);
}

void
mt_window_set_plugin_manager(MtWindow *window, gpointer plugin_manager)
{
    window->plugin_manager = plugin_manager;
    mt_window_sync_plugin_menu(window);
}

/* 断点 gutter 标记点击时触发的 action；宿主侧不直接处理，由插件连接
 * "activate" 信号自行维护断点集合并同步 gutter 标记。 */
void
mt_window_action_toggle_breakpoint(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    (void)action;
    (void)parameter;
    (void)user_data;
}
