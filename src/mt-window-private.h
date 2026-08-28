/*
 * mt-window-private.h
 * 仅供主窗口实现及其职责拆分模块使用的内部状态与协作函数。
 */

#ifndef MT_WINDOW_PRIVATE_H
#define MT_WINDOW_PRIVATE_H

#include "mt-window.h"

struct _MtWindow
{
    AdwApplicationWindow *window;
    AdwTabView *tab_view;
    AdwTabBar *tab_bar;
    AdwToastOverlay *toast_overlay;
    GtkSearchEntry *find_entry;
    GtkEntry *replace_entry;
    GtkWidget *find_bar;
    GtkWidget *find_revealer;
    GtkStack *sidebar_stack;
    GtkStack *auxiliary_stack;
    GtkScrolledWindow *auxiliary_wrap;
    GtkPaned *content_paned;
    AdwOverlaySplitView *auxiliary_split;
    GtkLabel *language_label;
    GtkLabel *position_label;
    GtkLabel *header_title_label;
    GtkLabel *header_subtitle_label;
    GtkWidget *header_modified_indicator;
    GtkToggleButton *replace_mode_button;
    GtkWidget *replace_button;
    GtkWidget *replace_all_button;
    gboolean search_case_sensitive;
    gboolean search_whole_word;
    GtkWidget *external_change_bar;
    MtDocument *external_change_document;
    MtDocument *inline_completion_document;
    GtkCssProvider *css_provider;
    GdkDisplay *display;
    MtSettings *settings;
    gpointer plugin_manager;
    GtkWidget *extensions_window;
    GtkToggleButton *properties_button;
    gpointer properties;
    guint properties_source_id;
    GtkCheckButton *menu_theme_follow;
    GtkCheckButton *menu_theme_light;
    GtkCheckButton *menu_theme_dark;
    GtkButton *menu_zoom_label;
    GMenu *primary_menu;
    GMenu *welcome_menu_section;
    gboolean welcome_menu_present;
    gpointer scheme_apply;
    guint snapshot_source_id;
    guint language_source_id;
    guint auto_update_source_id;
    gpointer auto_update_check;
    gulong style_manager_handler_id;
    gboolean disposed;
};

/* 下面的函数是拆分模块之间的内部协作接口，不属于稳定插件 ABI。 */
void mt_window_apply_editor_preferences(MtWindow *window);
void mt_window_apply_font_scale(MtWindow *window);
void mt_window_update_menu_zoom_label(MtWindow *window);
void mt_window_show_theme_chooser(GtkButton *button, gpointer user_data);
void mt_window_appearance_selected(GObject *object, GParamSpec *pspec, gpointer user_data);
void mt_window_language_selected(GObject *object, GParamSpec *pspec, gpointer user_data);
void mt_window_custom_font_toggled(GObject *object, GParamSpec *pspec, gpointer user_data);
void mt_window_choose_font_clicked(GtkButton *button, gpointer user_data);
void mt_window_font_scale_changed(AdwSpinRow *row, gpointer user_data);
void mt_window_tab_width_changed(AdwSpinRow *row, gpointer user_data);
void mt_window_clear_history_clicked(AdwActionRow *row, gpointer user_data);
const gchar *mt_window_theme_label(const gchar *style_scheme);

#endif
