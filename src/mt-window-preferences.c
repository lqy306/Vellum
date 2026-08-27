/*
 * mt-window-preferences.c
 * 主窗口的编辑器偏好界面与扩展偏好开关。
 */

#include "mt-window-private.h"
#include "mt-plugin-manager.h"

#include <glib/gi18n.h>

static void
mt_window_preference_switch_active_changed(GObject *object,
                                           GParamSpec *pspec,
                                           gpointer user_data)
{
    AdwSwitchRow *row;
    MtWindow *window;
    MtPluginManager *manager;
    guint index;
    const MtPreferenceSwitch *item;

    (void)pspec;

    row = ADW_SWITCH_ROW(object);
    window = user_data;
    manager = window->plugin_manager;
    index = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(row), "vellum-pref-switch-index"));
    item = manager != NULL ? mt_plugin_manager_get_preference_switch(manager, index) : NULL;
    if (item != NULL && item->set_callback != NULL)
    {
        item->set_callback(adw_switch_row_get_active(row), item->user_data);
    }
}

enum
{
    MT_EDITOR_SETTING_LINE_NUMBERS = 1,
    MT_EDITOR_SETTING_CURRENT_LINE,
    MT_EDITOR_SETTING_OVERVIEW,
    MT_EDITOR_SETTING_SPELL_CHECK,
    MT_EDITOR_SETTING_RIGHT_MARGIN,
    MT_EDITOR_SETTING_WORD_WRAP,
    MT_EDITOR_SETTING_AUTO_INDENT,
    MT_EDITOR_SETTING_AUTO_PAIR_BRACKETS,
    MT_EDITOR_SETTING_RESTORE_SESSION,
    MT_EDITOR_SETTING_EXTENSIONS_ENABLED,
    MT_EDITOR_SETTING_AUTO_CHECK_UPDATES
};

static void
mt_window_editor_switch_changed(GObject *object,
                                GParamSpec *pspec,
                                gpointer user_data)
{
    MtWindow *window;
    gint setting;
    gboolean enabled;

    (void)pspec;
    window = user_data;
    setting = GPOINTER_TO_INT(g_object_get_data(object, "vellum-editor-setting"));
    enabled = adw_switch_row_get_active(ADW_SWITCH_ROW(object));

    switch (setting)
    {
        case MT_EDITOR_SETTING_LINE_NUMBERS:
            mt_settings_set_show_line_numbers(window->settings, enabled);
            break;
        case MT_EDITOR_SETTING_CURRENT_LINE:
            mt_settings_set_highlight_current_line(window->settings, enabled);
            break;
        case MT_EDITOR_SETTING_OVERVIEW:
            mt_settings_set_show_overview(window->settings, enabled);
            break;
        case MT_EDITOR_SETTING_SPELL_CHECK:
            mt_settings_set_spell_check(window->settings, enabled);
            break;
        case MT_EDITOR_SETTING_RIGHT_MARGIN:
            mt_settings_set_show_right_margin(window->settings, enabled);
            break;
        case MT_EDITOR_SETTING_WORD_WRAP:
            mt_settings_set_word_wrap(window->settings, enabled);
            break;
        case MT_EDITOR_SETTING_AUTO_INDENT:
            mt_settings_set_auto_indent(window->settings, enabled);
            break;
        case MT_EDITOR_SETTING_AUTO_PAIR_BRACKETS:
            mt_settings_set_auto_pair_brackets(window->settings, enabled);
            break;
        case MT_EDITOR_SETTING_RESTORE_SESSION:
            mt_settings_set_restore_session(window->settings, enabled);
            break;
        case MT_EDITOR_SETTING_EXTENSIONS_ENABLED:
            mt_settings_set_extensions_enabled(window->settings, enabled);
            mt_window_show_toast(window,
                                 _("Extension loading will change the next time Vellum starts"));
            break;
        case MT_EDITOR_SETTING_AUTO_CHECK_UPDATES:
            mt_settings_set_auto_check_updates(window->settings, enabled);
            break;
        default:
            return;
    }

    mt_window_apply_editor_preferences(window);
    mt_settings_save(window->settings);
}

static void
mt_window_indent_width_changed(AdwSpinRow *row, gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    mt_settings_set_indent_width(window->settings,
                                 (gint)gtk_adjustment_get_value(adw_spin_row_get_adjustment(row)));
    mt_window_apply_editor_preferences(window);
    mt_settings_save(window->settings);
}

static void
mt_window_right_margin_changed(AdwSpinRow *row, gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    mt_settings_set_right_margin_position(window->settings,
                                          (gint)gtk_adjustment_get_value(adw_spin_row_get_adjustment(row)));
    mt_window_apply_editor_preferences(window);
    mt_settings_save(window->settings);
}

static AdwSwitchRow *
mt_window_add_editor_switch(AdwPreferencesGroup *group,
                            const gchar *title,
                            const gchar *subtitle,
                            gint setting,
                            gboolean active,
                            MtWindow *window)
{
    AdwSwitchRow *row;

    row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    if (subtitle != NULL)
    {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), subtitle);
    }
    adw_switch_row_set_active(row, active);
    g_object_set_data(G_OBJECT(row), "vellum-editor-setting", GINT_TO_POINTER(setting));
    g_signal_connect(row,
                     "notify::active",
                     G_CALLBACK(mt_window_editor_switch_changed),
                     window);
    adw_preferences_group_add(group, GTK_WIDGET(row));

    return row;
}

void
mt_window_action_preferences(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    AdwPreferencesWindow *preferences;
    AdwPreferencesPage *page;
    AdwPreferencesGroup *group;
    AdwComboRow *appearance_row;
    AdwActionRow *style_scheme_row;
    AdwComboRow *language_row;
    AdwSpinRow *font_scale_row;
    AdwSpinRow *tab_width_row;
    AdwSpinRow *indent_width_row;
    AdwSpinRow *right_margin_row;
    AdwActionRow *font_row;
    AdwPreferencesGroup *display_group;
    AdwPreferencesGroup *wrap_group;
    AdwPreferencesGroup *behavior_group;
    AdwSwitchRow *font_switch;
    GtkWidget *choose_font_button;
    GtkWidget *choose_theme_button;
    GtkStringList *appearance_model;
    GtkStringList *language_model;

    (void)action;
    (void)parameter;

    window = user_data;
    preferences = ADW_PREFERENCES_WINDOW(adw_preferences_window_new());
    gtk_window_set_transient_for(GTK_WINDOW(preferences), GTK_WINDOW(window->window));
    gtk_window_set_modal(GTK_WINDOW(preferences), TRUE);
    gtk_window_set_title(GTK_WINDOW(preferences), _("Preferences"));

    page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("Interface"));

    appearance_model = gtk_string_list_new(NULL);
    gtk_string_list_append(appearance_model, _("Follow system"));
    gtk_string_list_append(appearance_model, _("Light"));
    gtk_string_list_append(appearance_model, _("Dark"));
    appearance_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(appearance_row), _("Appearance"));
    adw_combo_row_set_model(appearance_row, G_LIST_MODEL(appearance_model));
    adw_combo_row_set_selected(appearance_row, mt_settings_get_appearance(window->settings));
    adw_preferences_group_add(group, GTK_WIDGET(appearance_row));

    style_scheme_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(style_scheme_row), _("Code Theme"));
    adw_action_row_set_subtitle(style_scheme_row,
                                mt_window_theme_label(mt_settings_get_style_scheme(window->settings)));
    choose_theme_button = gtk_button_new_with_label(_("Choose"));
    gtk_widget_set_valign(choose_theme_button, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(style_scheme_row, choose_theme_button);
    adw_preferences_group_add(group, GTK_WIDGET(style_scheme_row));
    g_object_set_data(G_OBJECT(choose_theme_button), "vellum-theme-row", style_scheme_row);
    g_signal_connect(choose_theme_button,
                     "clicked",
                     G_CALLBACK(mt_window_show_theme_chooser),
                     window);

    language_model = gtk_string_list_new(NULL);
    gtk_string_list_append(language_model, _("System language"));
    gtk_string_list_append(language_model, _("English"));
    gtk_string_list_append(language_model, _("Simplified Chinese"));
    language_row = ADW_COMBO_ROW(adw_combo_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(language_row), _("Language"));
    adw_combo_row_set_model(language_row, G_LIST_MODEL(language_model));
    adw_combo_row_set_selected(language_row,
                               g_strcmp0(mt_settings_get_language(window->settings), "en") == 0 ? 1 :
                               (g_strcmp0(mt_settings_get_language(window->settings), "zh_CN") == 0 ? 2 : 0));
    adw_preferences_group_add(group, GTK_WIDGET(language_row));

    font_switch = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(font_switch), _("Use custom font"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(font_switch),
                                _("When disabled, the system monospace font is used"));
    adw_switch_row_set_active(font_switch, mt_settings_get_custom_font(window->settings));
    adw_preferences_group_add(group, GTK_WIDGET(font_switch));
    g_signal_connect(font_switch,
                     "notify::active",
                     G_CALLBACK(mt_window_custom_font_toggled),
                     window);

    font_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(font_row), _("Editor Font"));
    adw_action_row_set_subtitle(font_row, mt_settings_get_font_family(window->settings));
    choose_font_button = gtk_button_new_with_label(_("Choose"));
    gtk_widget_set_valign(choose_font_button, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(font_row, choose_font_button);
    gtk_widget_set_sensitive(GTK_WIDGET(font_row),
                             mt_settings_get_custom_font(window->settings));
    g_object_set_data(G_OBJECT(font_switch), "vellum-font-row", font_row);
    adw_preferences_group_add(group, GTK_WIDGET(font_row));
    g_object_set_data(G_OBJECT(choose_font_button), "vellum-font-row", font_row);
    g_signal_connect(choose_font_button,
                     "clicked",
                     G_CALLBACK(mt_window_choose_font_clicked),
                     window);

    font_scale_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(0.75, 2.0, 0.05));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(font_scale_row), _("Editor font scale"));
    gtk_adjustment_set_value(adw_spin_row_get_adjustment(font_scale_row),
                             mt_settings_get_font_scale(window->settings));
    adw_spin_row_set_digits(font_scale_row, 2);
    adw_preferences_group_add(group, GTK_WIDGET(font_scale_row));

    tab_width_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(1.0, 16.0, 1.0));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(tab_width_row), _("Tab display width"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(tab_width_row),
                                 _("Controls tab columns only; Tab inserts a real tab character"));
    gtk_adjustment_set_value(adw_spin_row_get_adjustment(tab_width_row),
                             mt_settings_get_tab_width(window->settings));
    adw_spin_row_set_digits(tab_width_row, 0);
    adw_preferences_group_add(group, GTK_WIDGET(tab_width_row));

    display_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(display_group, _("Display"));
    mt_window_add_editor_switch(display_group,
                                _("Show line numbers"),
                                NULL,
                                MT_EDITOR_SETTING_LINE_NUMBERS,
                                mt_settings_get_show_line_numbers(window->settings),
                                window);
    mt_window_add_editor_switch(display_group,
                                _("Highlight current line"),
                                NULL,
                                MT_EDITOR_SETTING_CURRENT_LINE,
                                mt_settings_get_highlight_current_line(window->settings),
                                window);
    mt_window_add_editor_switch(display_group,
                                _("Show overview map"),
                                _("A compact map is displayed beside each document"),
                                MT_EDITOR_SETTING_OVERVIEW,
                                mt_settings_get_show_overview(window->settings),
                                window);
    mt_window_add_editor_switch(display_group,
                                _("Check spelling"),
                                _("Underlines misspelled words"),
                                MT_EDITOR_SETTING_SPELL_CHECK,
                                mt_settings_get_spell_check(window->settings),
                                window);

    wrap_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(wrap_group, _("Wrapping and Indentation"));
    mt_window_add_editor_switch(wrap_group,
                                _("Show right margin"),
                                NULL,
                                MT_EDITOR_SETTING_RIGHT_MARGIN,
                                mt_settings_get_show_right_margin(window->settings),
                                window);
    right_margin_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(40.0, 200.0, 1.0));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(right_margin_row), _("Right margin column"));
    gtk_adjustment_set_value(adw_spin_row_get_adjustment(right_margin_row),
                             mt_settings_get_right_margin_position(window->settings));
    adw_spin_row_set_digits(right_margin_row, 0);
    adw_preferences_group_add(wrap_group, GTK_WIDGET(right_margin_row));
    mt_window_add_editor_switch(wrap_group,
                                _("Automatic word wrap"),
                                NULL,
                                MT_EDITOR_SETTING_WORD_WRAP,
                                mt_settings_get_word_wrap(window->settings),
                                window);
    mt_window_add_editor_switch(wrap_group,
                                _("Automatic indentation"),
                                NULL,
                                MT_EDITOR_SETTING_AUTO_INDENT,
                                mt_settings_get_auto_indent(window->settings),
                                window);
    mt_window_add_editor_switch(wrap_group,
                                _("Auto-pair brackets"),
                                _("Typing an opening bracket, quote or angle bracket in code documents inserts its closing pair and keeps the cursor inside"),
                                MT_EDITOR_SETTING_AUTO_PAIR_BRACKETS,
                                mt_settings_get_auto_pair_brackets(window->settings),
                                window);
    indent_width_row = ADW_SPIN_ROW(adw_spin_row_new_with_range(1.0, 16.0, 1.0));
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(indent_width_row), _("Indent width"));
    gtk_adjustment_set_value(adw_spin_row_get_adjustment(indent_width_row),
                             mt_settings_get_indent_width(window->settings));
    adw_spin_row_set_digits(indent_width_row, 0);
    adw_preferences_group_add(wrap_group, GTK_WIDGET(indent_width_row));

    behavior_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(behavior_group, _("Behavior"));
    mt_window_add_editor_switch(behavior_group,
                                _("Restore previous session"),
                                _("Restore recoverable drafts when Vellum starts"),
                                MT_EDITOR_SETTING_RESTORE_SESSION,
                                mt_settings_get_restore_session(window->settings),
                                window);
    mt_window_add_editor_switch(behavior_group,
                                _("Enable extensions"),
                                _("When disabled, Vellum starts as a compact core editor; restart to apply"),
                                MT_EDITOR_SETTING_EXTENSIONS_ENABLED,
                                mt_settings_get_extensions_enabled(window->settings),
                                window);
    mt_window_add_editor_switch(behavior_group,
                                _("Check for updates automatically"),
                                _("Checks the GitHub release page when Vellum starts"),
                                MT_EDITOR_SETTING_AUTO_CHECK_UPDATES,
                                mt_settings_get_auto_check_updates(window->settings),
                                window);
    {
        AdwActionRow *clear_row;

        clear_row = ADW_ACTION_ROW(adw_action_row_new());
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(clear_row), _("Clear History"));
        adw_action_row_set_subtitle(clear_row,
                                    _("Delete all recoverable drafts"));
        g_object_set(G_OBJECT(clear_row), "activatable", TRUE, NULL);
        gtk_widget_add_css_class(GTK_WIDGET(clear_row), "vellum-destructive");
        g_signal_connect(clear_row,
                         "activated",
                         G_CALLBACK(mt_window_clear_history_clicked),
                         window);
        adw_preferences_group_add(behavior_group, GTK_WIDGET(clear_row));
    }

    adw_preferences_page_add(page, group);
    adw_preferences_page_add(page, display_group);
    adw_preferences_page_add(page, wrap_group);
    adw_preferences_page_add(page, behavior_group);

    if (window->plugin_manager != NULL &&
        mt_plugin_manager_get_preference_switch_count(window->plugin_manager) > 0)
    {
        AdwPreferencesGroup *plugin_group;
        guint switch_count;
        guint index;

        plugin_group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
        switch_count = mt_plugin_manager_get_preference_switch_count(window->plugin_manager);
        for (index = 0; index < switch_count; index++)
        {
            const MtPreferenceSwitch *item;
            AdwSwitchRow *row;

            item = mt_plugin_manager_get_preference_switch(window->plugin_manager, index);
            if (item == NULL)
            {
                continue;
            }
            if (index == 0)
            {
                adw_preferences_group_set_title(plugin_group, item->group);
            }
            row = ADW_SWITCH_ROW(adw_switch_row_new());
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), item->title);
            if (item->subtitle != NULL)
            {
                adw_action_row_set_subtitle(ADW_ACTION_ROW(row), item->subtitle);
            }
            adw_switch_row_set_active(row,
                                      item->get_callback != NULL ?
                                      item->get_callback(item->user_data) : FALSE);
            g_object_set_data(G_OBJECT(row),
                              "vellum-pref-switch-index",
                              GUINT_TO_POINTER(index));
            g_signal_connect(row,
                             "notify::active",
                             G_CALLBACK(mt_window_preference_switch_active_changed),
                             window);
            adw_preferences_group_add(plugin_group, GTK_WIDGET(row));
        }
        adw_preferences_page_add(page, plugin_group);
    }

    adw_preferences_window_add(preferences, page);
    g_signal_connect(appearance_row,
                     "notify::selected",
                     G_CALLBACK(mt_window_appearance_selected),
                     window);
    g_signal_connect(language_row,
                     "notify::selected",
                     G_CALLBACK(mt_window_language_selected),
                     window);
    g_signal_connect(font_scale_row,
                     "changed",
                     G_CALLBACK(mt_window_font_scale_changed),
                     window);
    g_signal_connect(tab_width_row,
                     "changed",
                     G_CALLBACK(mt_window_tab_width_changed),
                     window);
    g_signal_connect(indent_width_row,
                     "changed",
                     G_CALLBACK(mt_window_indent_width_changed),
                     window);
    g_signal_connect(right_margin_row,
                     "changed",
                     G_CALLBACK(mt_window_right_margin_changed),
                     window);
    gtk_window_present(GTK_WINDOW(preferences));
    g_object_unref(appearance_model);
    g_object_unref(language_model);
}

