#include "mt-window-private.h"

void
mt_window_find_next(MtWindow *window, gboolean backwards)
{
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter match_start;
    GtkTextIter match_end;
    const gchar *needle;
    gboolean found;
    GtkTextSearchFlags flags;

    needle = gtk_editable_get_text(GTK_EDITABLE(window->find_entry));
    if (needle == NULL || *needle == '\0')
    {
        return;
    }

    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    flags = GTK_TEXT_SEARCH_TEXT_ONLY;
    if (!window->search_case_sensitive)
    {
        flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;
    }
    gtk_text_buffer_get_iter_at_mark(buffer, &start, gtk_text_buffer_get_insert(buffer));

    if (backwards)
    {
        found = gtk_text_iter_backward_search(&start,
                                              needle,
                                              flags,
                                              &match_start,
                                              &match_end,
                                              NULL);
        if (!found)
        {
            gtk_text_buffer_get_end_iter(buffer, &start);
            found = gtk_text_iter_backward_search(&start,
                                                  needle,
                                                  flags,
                                                  &match_start,
                                                  &match_end,
                                                  NULL);
        }
    }
    else
    {
        if (gtk_text_buffer_get_has_selection(buffer))
        {
            gtk_text_buffer_get_selection_bounds(buffer, NULL, &start);
        }

        found = gtk_text_iter_forward_search(&start,
                                             needle,
                                             GTK_TEXT_SEARCH_TEXT_ONLY,
                                             &match_start,
                                             &match_end,
                                             NULL);
        if (!found)
        {
            gtk_text_buffer_get_start_iter(buffer, &start);
            found = gtk_text_iter_forward_search(&start,
                                                 needle,
                                                 flags,
                                                 &match_start,
                                                 &match_end,
                                                 NULL);
        }
    }

    if (found)
    {
        gtk_text_buffer_select_range(buffer, &match_start, &match_end);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(mt_document_get_view(document)),
                                     &match_start,
                                     0.25,
                                     FALSE,
                                     0.0,
                                     0.0);
    }
    else
    {
        mt_window_show_toast(window, _("No matches found"));
    }
}

void
mt_window_find_next_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    mt_window_find_next(user_data, FALSE);
}

void
mt_window_find_previous_clicked(GtkButton *button, gpointer user_data)
{
    (void)button;
    mt_window_find_next(user_data, TRUE);
}

void
mt_window_replace_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    gchar *selected;
    const gchar *needle;
    const gchar *replacement;

    (void)button;

    window = user_data;
    document = mt_window_get_current_document(window);
    if (document == NULL)
    {
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    needle = gtk_editable_get_text(GTK_EDITABLE(window->find_entry));
    replacement = gtk_editable_get_text(GTK_EDITABLE(window->replace_entry));

    if (gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
    {
        selected = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        if (g_strcmp0(selected, needle) == 0)
        {
            gtk_text_buffer_delete(buffer, &start, &end);
            gtk_text_buffer_insert(buffer, &start, replacement, -1);
        }
        g_free(selected);
    }

    mt_window_find_next(window, FALSE);
}

void
mt_window_replace_all_clicked(GtkButton *button, gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    GtkTextBuffer *buffer;
    GtkTextIter search_from;
    GtkTextIter match_start;
    GtkTextIter match_end;
    const gchar *needle;
    const gchar *replacement;
    GtkTextSearchFlags flags;
    guint count;

    (void)button;

    window = user_data;
    document = mt_window_get_current_document(window);
    needle = gtk_editable_get_text(GTK_EDITABLE(window->find_entry));
    replacement = gtk_editable_get_text(GTK_EDITABLE(window->replace_entry));
    if (document == NULL || needle == NULL || *needle == '\0')
    {
        return;
    }

    buffer = GTK_TEXT_BUFFER(mt_document_get_buffer(document));
    flags = GTK_TEXT_SEARCH_TEXT_ONLY;
    if (!window->search_case_sensitive)
    {
        flags |= GTK_TEXT_SEARCH_CASE_INSENSITIVE;
    }
    count = 0;
    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_get_start_iter(buffer, &search_from);
    while (gtk_text_iter_forward_search(&search_from,
                                        needle,
                                        flags,
                                        &match_start,
                                        &match_end,
                                        NULL))
    {
        gint offset;

        offset = gtk_text_iter_get_offset(&match_start);
        gtk_text_buffer_delete(buffer, &match_start, &match_end);
        gtk_text_buffer_get_iter_at_offset(buffer, &search_from, offset);
        gtk_text_buffer_insert(buffer, &search_from, replacement, -1);
        gtk_text_buffer_get_iter_at_offset(buffer,
                                           &search_from,
                                           offset + g_utf8_strlen(replacement, -1));
        count++;
    }
    gtk_text_buffer_end_user_action(buffer);

    if (count == 0)
    {
        mt_window_show_toast(window, _("No matches found"));
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(ngettext("Replaced %u occurrence",
                                           "Replaced %u occurrences",
                                           count),
                                  count);
        mt_window_show_toast(window, message);
        g_free(message);
    }
}

void
mt_window_set_replace_mode(MtWindow *window, gboolean replace_mode)
{
    if (window == NULL || window->disposed)
    {
        return;
    }

    if (window->replace_entry != NULL)
    {
        gtk_widget_set_visible(GTK_WIDGET(window->replace_entry), replace_mode);
    }
    if (window->replace_button != NULL)
    {
        gtk_widget_set_visible(window->replace_button, replace_mode);
    }
    if (window->replace_all_button != NULL)
    {
        gtk_widget_set_visible(window->replace_all_button, replace_mode);
    }
}

void
mt_window_replace_mode_toggled(GtkToggleButton *button, gpointer user_data)
{
    mt_window_set_replace_mode(user_data, gtk_toggle_button_get_active(button));
}

void
mt_window_search_case_toggled(GtkCheckButton *button, gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    if (window == NULL || window->disposed)
    {
        return;
    }
    window->search_case_sensitive = gtk_check_button_get_active(button);
}

void
mt_window_hide_find_bar(GtkButton *button, gpointer user_data)
{
    MtWindow *window;

    (void)button;

    window = user_data;
    if (window->find_revealer != NULL)
    {
        gtk_revealer_set_reveal_child(GTK_REVEALER(window->find_revealer), FALSE);
    }
    {
        MtDocument *document;

        document = mt_window_get_current_document(window);
        if (document != NULL)
        {
            gtk_widget_grab_focus(mt_document_get_view(document));
        }
    }
}

void
mt_window_show_find_bar(MtWindow *window, gboolean replace_mode)
{
    if (window == NULL || window->disposed)
    {
        return;
    }
    if (window->find_revealer != NULL)
    {
        gtk_revealer_set_reveal_child(GTK_REVEALER(window->find_revealer), TRUE);
    }
    mt_window_set_replace_mode(window, replace_mode);
    if (window->replace_mode_button != NULL)
    {
        gtk_toggle_button_set_active(window->replace_mode_button, replace_mode);
    }
    gtk_widget_grab_focus(GTK_WIDGET(window->find_entry));
}

void
mt_window_apply_font_scale(MtWindow *window)
{
    mt_window_apply_editor_style(window);
}

void
mt_window_system_dark_changed(AdwStyleManager *manager,
                              GParamSpec *pspec,
                              gpointer user_data)
{
    MtWindow *window;

    (void)manager;
    (void)pspec;

    window = user_data;
    if (window == NULL || window->disposed)
    {
        return;
    }

    if (mt_settings_get_appearance(window->settings) == MT_APPEARANCE_SYSTEM)
    {
        mt_window_apply_editor_style(window);
    }
}

void
mt_window_appearance_selected(GObject *object,
                              GParamSpec *pspec,
                              gpointer user_data)
{
    AdwComboRow *row;
    MtWindow *window;

    (void)pspec;

    row = ADW_COMBO_ROW(object);
    window = user_data;
    mt_settings_set_appearance(window->settings, (MtAppearance)adw_combo_row_get_selected(row));
    mt_settings_apply_appearance(window->settings);
    mt_window_apply_editor_style(window);
    mt_window_update_menu_theme_buttons(window);
    mt_settings_save(window->settings);
}

const gchar *
mt_window_theme_label(const gchar *style_scheme)
{
    if (g_strcmp0(style_scheme, "adwaita") == 0)
    {
        return _("Adwaita");
    }
    if (g_strcmp0(style_scheme, "classic") == 0)
    {
        return _("Classic");
    }
    if (g_strcmp0(style_scheme, "cobalt") == 0)
    {
        return _("Cobalt");
    }
    if (g_strcmp0(style_scheme, "kate") == 0)
    {
        return _("Kate");
    }
    if (g_strcmp0(style_scheme, "oblivion") == 0)
    {
        return _("Oblivion");
    }
    if (g_strcmp0(style_scheme, "solarized") == 0)
    {
        return _("Solarized");
    }
    if (g_strcmp0(style_scheme, "tango") == 0)
    {
        return _("Tango");
    }

    return _("Automatic (Adwaita)");
}

void
mt_window_theme_card_clicked(GtkButton *button, gpointer user_data)
{
    MtThemeChooser *chooser;
    const gchar *style_scheme;

    chooser = user_data;
    style_scheme = g_object_get_data(G_OBJECT(button), "vellum-theme-style-scheme");
    if (style_scheme == NULL)
    {
        return;
    }

    {
        MtWindow *window;

        window = chooser->window;
        mt_timing_log("theme-card-clicked: %s", style_scheme);
        mt_settings_set_style_scheme(window->settings, style_scheme);
        mt_settings_save(window->settings);
        adw_action_row_set_subtitle(chooser->row, mt_window_theme_label(style_scheme));
        /* 选中即应用并关闭，避免配色应用较慢时窗口“退不出来”。 */
        gtk_window_close(chooser->dialog);
        mt_window_apply_source_scheme(window);
    }
}

static const gchar *
mt_window_preview_scheme_id(MtWindow *window, const gchar *style_scheme)
{
    gboolean dark;

    dark = mt_window_is_dark(window);
    if (g_strcmp0(style_scheme, "adwaita") == 0 ||
        g_strcmp0(style_scheme, "auto") == 0)
    {
        return dark ? "Adwaita-dark" : "Adwaita";
    }
    if (g_strcmp0(style_scheme, "classic") == 0)
    {
        return dark ? "classic-dark" : "classic";
    }
    if (g_strcmp0(style_scheme, "cobalt") == 0)
    {
        return dark ? "cobalt" : "cobalt-light";
    }
    if (g_strcmp0(style_scheme, "kate") == 0)
    {
        return dark ? "kate-dark" : "kate";
    }
    if (g_strcmp0(style_scheme, "oblivion") == 0)
    {
        return "oblivion";
    }
    if (g_strcmp0(style_scheme, "solarized") == 0)
    {
        return dark ? "solarized-dark" : "solarized-light";
    }
    if (g_strcmp0(style_scheme, "tango") == 0)
    {
        return "tango";
    }

    return dark ? "Adwaita-dark" : "Adwaita";
}

static GtkWidget *
mt_window_create_theme_card(MtThemeChooser *chooser, const gchar *style_scheme)
{
    GtkSourceStyleSchemeManager *scheme_manager;
    GtkSourceStyleScheme *scheme;
    GtkSourceLanguageManager *language_manager;
    GtkSourceLanguage *language;
    GtkSourceBuffer *buffer;
    GtkWidget *button;
    GtkWidget *box;
    GtkWidget *preview_frame;
    GtkWidget *preview;
    GtkWidget *label;

    scheme_manager = gtk_source_style_scheme_manager_get_default();
    scheme = gtk_source_style_scheme_manager_get_scheme(
        scheme_manager,
        mt_window_preview_scheme_id(chooser->window, style_scheme));
    language_manager = gtk_source_language_manager_get_default();
    language = gtk_source_language_manager_get_language(language_manager, "c");
    buffer = gtk_source_buffer_new_with_language(language);
    if (scheme != NULL)
    {
        gtk_source_buffer_set_style_scheme(buffer, scheme);
    }
    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(buffer),
                             "/* Vellum */\nint main(void)\n{\n    return 0;\n}\n",
                             -1);

    preview = gtk_source_view_new_with_buffer(buffer);
    gtk_source_view_set_show_line_numbers(GTK_SOURCE_VIEW(preview), TRUE);
    gtk_source_view_set_highlight_current_line(GTK_SOURCE_VIEW(preview), FALSE);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(preview), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(preview), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(preview), TRUE);
    gtk_widget_set_size_request(preview, 212, 132);
    gtk_widget_add_css_class(preview, "vellum-theme-preview");
    g_object_unref(buffer);

    preview_frame = gtk_frame_new(NULL);
    gtk_frame_set_child(GTK_FRAME(preview_frame), preview);
    gtk_widget_add_css_class(preview_frame, "vellum-theme-preview-frame");

    label = gtk_label_new(mt_window_theme_label(style_scheme));
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_add_css_class(label, "vellum-theme-name");

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_box_append(GTK_BOX(box), preview_frame);
    gtk_box_append(GTK_BOX(box), label);

    button = gtk_button_new();
    gtk_button_set_child(GTK_BUTTON(button), box);
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_tooltip_text(button, mt_window_theme_label(style_scheme));
    gtk_widget_add_css_class(button, "vellum-theme-card");
    g_object_set_data_full(G_OBJECT(button),
                           "vellum-theme-style-scheme",
                           g_strdup(style_scheme),
                           g_free);
    if (g_strcmp0(style_scheme, mt_settings_get_style_scheme(chooser->window->settings)) == 0)
    {
        gtk_widget_add_css_class(button, "vellum-theme-selected");
    }
    g_signal_connect(button,
                     "clicked",
                     G_CALLBACK(mt_window_theme_card_clicked),
                     chooser);

    return button;
}

void
mt_window_theme_chooser_free(MtThemeChooser *chooser)
{
    g_free(chooser);
}

void
mt_window_show_theme_chooser(GtkButton *button, gpointer user_data)
{
    static const gchar * const themes[] = {
        "auto", "adwaita", "classic", "cobalt",
        "kate", "oblivion", "solarized", "tango"
    };
    MtWindow *window;
    MtThemeChooser *chooser;
    GtkWidget *content;
    GtkWidget *heading;
    GtkWidget *description;
    GtkWidget *flow_box;
    GtkWidget *scroller;
    GtkWidget *footer;
    guint index;

    window = user_data;
    chooser = g_new0(MtThemeChooser, 1);
    chooser->window = window;
    chooser->row = ADW_ACTION_ROW(g_object_get_data(G_OBJECT(button), "vellum-theme-row"));
    chooser->dialog = GTK_WINDOW(adw_window_new());
    gtk_window_set_title(chooser->dialog, _("Choose Code Theme"));
    /*
     * 挂在打开它的首选项窗口下而不是主窗口：多个 modal 窗口都挂主窗口时，
     * 关闭后一个会令前一个（首选项）无法再获得输入抓取，表现为窗口“冻住”。
     */
    {
        GtkWidget *dialog_parent;

        dialog_parent = GTK_WIDGET(gtk_widget_get_root(GTK_WIDGET(chooser->row)));
        if (GTK_IS_WINDOW(dialog_parent))
        {
            gtk_window_set_transient_for(chooser->dialog, GTK_WINDOW(dialog_parent));
        }
    }
    gtk_window_set_modal(chooser->dialog, TRUE);
    gtk_window_set_default_size(chooser->dialog, 570, 520);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(content, 20);
    gtk_widget_set_margin_bottom(content, 20);
    gtk_widget_set_margin_start(content, 20);
    gtk_widget_set_margin_end(content, 20);

    heading = gtk_label_new(_("Code Themes"));
    gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
    gtk_widget_add_css_class(heading, "title-2");
    description = gtk_label_new(_("Choose a color scheme for source code. The preview uses the same GtkSourceView scheme as the editor."));
    gtk_label_set_xalign(GTK_LABEL(description), 0.0);
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_widget_add_css_class(description, "dim-label");

    flow_box = gtk_flow_box_new();
    chooser->flow_box = GTK_FLOW_BOX(flow_box);
    gtk_flow_box_set_selection_mode(chooser->flow_box, GTK_SELECTION_NONE);
    gtk_flow_box_set_max_children_per_line(chooser->flow_box, 2);
    gtk_flow_box_set_min_children_per_line(chooser->flow_box, 2);
    gtk_flow_box_set_row_spacing(chooser->flow_box, 12);
    gtk_flow_box_set_column_spacing(chooser->flow_box, 12);

    scroller = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroller), flow_box);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_NEVER,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scroller, TRUE);

    for (index = 0; index < G_N_ELEMENTS(themes); index++)
    {
        gtk_flow_box_insert(chooser->flow_box,
                            mt_window_create_theme_card(chooser, themes[index]),
                            -1);
    }

    footer = gtk_label_new(_("Theme colors are provided by installed GtkSourceView schemes; Vellum does not copy third-party theme assets."));
    gtk_label_set_xalign(GTK_LABEL(footer), 0.0);
    gtk_label_set_wrap(GTK_LABEL(footer), TRUE);
    gtk_widget_add_css_class(footer, "dim-label");
    gtk_widget_add_css_class(footer, "caption");

    gtk_box_append(GTK_BOX(content), heading);
    gtk_box_append(GTK_BOX(content), description);
    gtk_box_append(GTK_BOX(content), scroller);
    gtk_box_append(GTK_BOX(content), footer);
    /* AdwWindow 自身没有标题栏，必须显式提供 AdwHeaderBar，
     * 否则没有关闭按钮，模态窗口会“退不出来”。 */
    {
        GtkWidget *toolbar;
        GtkWidget *header;

        toolbar = adw_toolbar_view_new();
        header = adw_header_bar_new();
        adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);
        adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), content);
        adw_window_set_content(ADW_WINDOW(chooser->dialog), toolbar);
    }
    g_object_set_data_full(G_OBJECT(chooser->dialog),
                           "vellum-theme-chooser",
                           chooser,
                           (GDestroyNotify)mt_window_theme_chooser_free);
    gtk_window_present(chooser->dialog);
}

void
mt_window_language_selected(GObject *object,
                            GParamSpec *pspec,
                            gpointer user_data)
{
    AdwComboRow *row;
    MtWindow *window;
    const gchar *language;

    (void)pspec;

    row = ADW_COMBO_ROW(object);
    window = user_data;
    language = adw_combo_row_get_selected(row) == 1 ? "en" :
               (adw_combo_row_get_selected(row) == 2 ? "zh_CN" : "system");
    mt_settings_set_language(window->settings, language);
    mt_settings_save(window->settings);
    mt_window_show_toast(window, _("Language will apply after restart"));
}

void
mt_window_tab_width_changed(AdwSpinRow *row, gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    mt_settings_set_tab_width(window->settings,
                              (gint)gtk_adjustment_get_value(adw_spin_row_get_adjustment(row)));
    mt_window_apply_editor_preferences(window);
    mt_settings_save(window->settings);
}

void
mt_window_font_scale_changed(AdwSpinRow *row, gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    mt_settings_set_font_scale(window->settings,
                               gtk_adjustment_get_value(adw_spin_row_get_adjustment(row)));
    mt_window_apply_font_scale(window);
    mt_window_update_menu_zoom_label(window);
    mt_settings_save(window->settings);
}

gboolean
mt_window_key_pressed(GtkEventControllerKey *controller,
                      guint keyval,
                      guint keycode,
                      GdkModifierType state,
                      gpointer user_data)
{
    MtWindow *window;
    MtDocument *document;
    MtPluginManager *manager;

    (void)controller;
    window = user_data;
    manager = window->plugin_manager;
    document = mt_window_get_current_document(window);
    if (manager == NULL || document == NULL ||
        !gtk_widget_has_focus(mt_document_get_view(document)))
    {
        return FALSE;
    }

    return mt_plugin_manager_handle_key(manager, keyval, keycode, (guint)state);
}
