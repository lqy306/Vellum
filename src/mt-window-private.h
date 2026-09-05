/*
 * mt-window-private.h
 * 仅供主窗口实现及其职责拆分模块使用的内部状态与协作函数。
 */

#ifndef MT_WINDOW_PRIVATE_H
#define MT_WINDOW_PRIVATE_H

#include "mt-window.h"
#include "mt-overview-map.h"
#include "mt-vut-package.h"
#include "mt-plugin-manager.h"

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <pango/pango.h>
#include <stdarg.h>
#include <string.h>

#ifndef VELLUM_VERSION
#define VELLUM_VERSION "0"
#endif

#define MT_DOCUMENT_DATA_KEY "vellum-document"
#define MT_SNAPSHOT_DELAY_SECONDS 8
#define MT_MIN_WINDOW_WIDTH 800
#define MT_MIN_WINDOW_HEIGHT 480
#define MT_SCHEME_PENDING_KEY "vellum-scheme-pending"
#define MT_SCHEME_LAZY_LIMIT 300000
#define MT_MAP_RENDER_LIMIT 300000
#define MT_OVERVIEW_MAP_WIDTH 104
#define MT_AUXILIARY_DRAWER_BREAKPOINT 900
#define MT_PROPERTIES_STATS_LIMIT 500000
#define MT_PROPERTIES_PANEL_WIDTH 300

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
    MtDocument *inline_diff_document;
    gchar *inline_diff_old;
    gchar *inline_diff_new;
    gint inline_diff_offset;
    GtkWidget *inline_diff_widget;
    /* 编译器报错的红色波浪下划线：{offset,length,message} 列表 + 悬停处理器。 */
    GPtrArray *error_ranges;
    gulong error_tooltip_handler_id;
    /* 断点：已设置断点的行号集合（1-based）+ gutter 叠加控件。 */
    GPtrArray *breakpoint_lines;
    GHashTable *breakpoint_widgets;
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
/* 扩展安装源管理（“扩展”页内）。 */
void mt_window_sources_group_rebuild(MtWindow *window, AdwPreferencesGroup *group);
void mt_window_source_remove_clicked(GtkButton *button, gpointer user_data);
void mt_window_source_add_response(GtkDialog *dialog, gint response, gpointer user_data);
void mt_window_source_add_clicked(GtkButton *button, gpointer user_data);
void mt_window_default_source_remove_clicked(GtkButton *button, gpointer user_data);

/* ---------- 内部数据类型（原 mt-window.c 中定义，供拆分模块共享） ---------- */

typedef struct _MtFileRequest MtFileRequest;
typedef struct _MtCloseRequest MtCloseRequest;
typedef struct _MtFontRequest MtFontRequest;
typedef struct _MtExtensionFileRequest MtExtensionFileRequest;
typedef struct _MtExtensionDeleteRequest MtExtensionDeleteRequest;
typedef struct _MtThemeChooser MtThemeChooser;
typedef struct _MtPropertiesPanel MtPropertiesPanel;
typedef struct _MtAutoUpdateCheck MtAutoUpdateCheck;
typedef struct _MtSchemeApply MtSchemeApply;

struct _MtAutoUpdateCheck
{
    MtWindow *window;
    SoupSession *session;
    SoupMessage *message;
    GCancellable *cancellable;
};

struct _MtFileRequest
{
    MtWindow *window;
    MtDocument *document;
    AdwTabPage *page;
    GtkFileDialog *dialog;
};

struct _MtCloseRequest
{
    MtWindow *window;
    MtDocument *document;
    AdwTabPage *page;
};

struct _MtFontRequest
{
    MtWindow *window;
    AdwActionRow *row;
    GtkFontDialog *dialog;
};

struct _MtExtensionFileRequest
{
    MtWindow *window;
    GtkFileDialog *dialog;
    guint index;
    gboolean importing;
};

struct _MtExtensionDeleteRequest
{
    MtWindow *window;
    GtkWidget *extensions_window;
    guint index;
};

struct _MtThemeChooser
{
    MtWindow *window;
    AdwActionRow *row;
    GtkFlowBox *flow_box;
    GtkWindow *dialog;
};

typedef struct _MtPropertiesItem
{
    gchar *name;
    guint index;
} MtPropertiesItem;

struct _MtPropertiesPanel
{
    MtWindow *window;
    GtkWidget *page;
    AdwActionRow *name_row;
    AdwActionRow *location_row;
    AdwActionRow *type_row;
    GtkWidget *type_button;
    AdwComboRow *encoding_row;
    GtkStringList *encoding_model;
    AdwComboRow *newline_row;
    GtkStringList *newline_model;
    GPtrArray *type_language_ids;
    GPtrArray *type_items;
    GPtrArray *encoding_items;
    GPtrArray *encoding_picker_items;
    guint type_selected;
    guint encoding_selected;
    gboolean refreshing_metadata;
    AdwSwitchRow *auto_indent_row;
    AdwComboRow *indent_char_row;
    AdwSpinRow *tab_width_row;
    AdwSpinRow *indent_width_row;
    AdwActionRow *lines_row;
    AdwActionRow *words_row;
    AdwActionRow *chars_row;
    AdwActionRow *all_chars_row;
    GtkStringList *indent_model;
};

struct _MtSchemeApply
{
    MtWindow *window;
    guint source_id;
    gboolean maps_hidden;
};

/* ---------- 文件请求辅助函数（原 static，因跨模块使用改为非 static） ---------- */
MtFileRequest *mt_file_request_new(MtWindow *window, MtDocument *document, GtkFileDialog *dialog);
void mt_file_request_free(MtFileRequest *request);

/* ---------- 跨模块协作函数声明 ---------- */
void mt_window_apply_editor_style(MtWindow *window);
void mt_window_apply_source_scheme(MtWindow *window);
void mt_window_update_language_label(MtWindow *window);
void mt_window_schedule_language_update(MtWindow *window);
void mt_window_update_position(MtWindow *window);
PangoAttrList *mt_window_statusbar_attrs(void);
void mt_window_properties_refresh(MtWindow *window);
void mt_window_properties_refresh_metadata(MtPropertiesPanel *panel);
void mt_window_properties_schedule_refresh(MtWindow *window);
void mt_window_properties_type_picked(MtPropertiesPanel *panel, guint index, gpointer user_data);
void mt_window_properties_encoding_picked(MtPropertiesPanel *panel, guint index, gpointer user_data);
void mt_window_properties_encoding_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
void mt_window_properties_newline_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
void mt_window_properties_convert_newlines(MtPropertiesPanel *panel, GtkSourceNewlineType newline);
void mt_window_document_load_finished(GObject *source, GAsyncResult *result, gpointer user_data);
void mt_window_save_and_close_finished(GObject *source, GAsyncResult *result, gpointer user_data);
void mt_window_destroyed(GtkWidget *widget, gpointer user_data);
void mt_window_monitor_document(MtWindow *window, MtDocument *document);
void mt_window_find_next(MtWindow *window, gboolean backwards);
void mt_window_set_replace_mode(MtWindow *window, gboolean replace_mode);
void mt_window_show_find_bar(MtWindow *window, gboolean replace_mode);
void mt_window_update_menu_theme_buttons(MtWindow *window);
void mt_window_update_header_title(MtWindow *window);
void mt_window_update_document_title(MtDocument *document);
GtkStack *mt_window_get_panel_stack(MtWindow *window, MtPluginPanelLocation location);
gboolean mt_window_auto_update_check_idle(gpointer user_data);
void mt_window_show_toast(MtWindow *window, const gchar *message);

/* ---------- 跨模块辅助函数 ---------- */
void mt_timing_log(const gchar *format, ...);
void mt_timing_log_duration(const gchar *label, gint64 start);
MtCloseRequest *mt_close_request_new(MtWindow *window, MtDocument *document, AdwTabPage *page);
void mt_close_request_free(MtCloseRequest *request);
gboolean mt_window_is_dark(MtWindow *window);
void mt_window_properties_name_activated(AdwActionRow *row, gpointer user_data);
void mt_window_properties_location_activated(AdwActionRow *row, gpointer user_data);

/* ---------- 更多跨模块函数声明 ---------- */
void mt_properties_item_free(gpointer data);
void mt_window_properties_type_dialog_show(AdwActionRow *row, gpointer user_data);
void mt_window_buffer_modified_changed(GtkTextBuffer *buffer, gpointer user_data);
void mt_window_buffer_changed(GtkTextBuffer *buffer, gpointer user_data);
void mt_window_buffer_inserted(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint length, gpointer user_data);
void mt_window_buffer_deleted(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, gpointer user_data);
void mt_window_cursor_moved(GtkTextBuffer *buffer, GtkTextIter *new_location, GtkTextMark *mark, gpointer user_data);
void mt_window_editor_scrolled(GtkAdjustment *adjustment, gpointer user_data);
GtkStack *mt_window_get_panel_stack(MtWindow *window, MtPluginPanelLocation location);
void mt_window_save_and_close_finished(GObject *source, GAsyncResult *result, gpointer user_data);
void mt_window_paste_finished(GObject *source, GAsyncResult *result, gpointer user_data);
void mt_window_auxiliary_visible(MtWindow *window, gboolean visible);
void mt_window_open_dialog_finished(GObject *source, GAsyncResult *result, gpointer user_data);
void mt_window_document_save_finished(GObject *source, GAsyncResult *result, gpointer user_data);
void mt_window_save_dialog_finished(GObject *source, GAsyncResult *result, gpointer user_data);
void mt_window_extension_import_clicked(GtkButton *button, gpointer user_data);
void mt_window_extension_export_clicked(GtkButton *button, gpointer user_data);
void mt_window_extension_configure_clicked(GtkButton *button, gpointer user_data);
void mt_window_extension_delete_clicked(GtkButton *button, gpointer user_data);
void mt_window_toggle_properties(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_register_source_icon_path(AdwApplicationWindow *window);
gboolean mt_window_key_pressed(GtkEventControllerKey *controller, guint keyval, guint keycode, GdkModifierType state, gpointer user_data);
GMenu *mt_window_build_primary_menu(MtWindow *window);
GtkWidget *mt_window_build_theme_selector(MtWindow *window);
GtkWidget *mt_window_build_zoom_box(MtWindow *window);
void mt_window_properties_toggled(GtkToggleButton *button, gpointer user_data);
void mt_window_external_reload_clicked(GtkButton *button, gpointer user_data);
void mt_window_external_change_close_clicked(GtkButton *button, gpointer user_data);
void mt_window_auxiliary_width_changed(GObject *object, GParamSpec *pspec, gpointer user_data);
MtPropertiesPanel *mt_window_properties_create(MtWindow *window);
void mt_window_system_dark_changed(AdwStyleManager *manager, GParamSpec *pspec, gpointer user_data);
gboolean mt_window_close_page_requested(AdwTabView *tab_view, AdwTabPage *page, gpointer user_data);
void mt_window_page_selected(AdwTabView *tab_view, GParamSpec *pspec, gpointer user_data);
void mt_window_find_previous_clicked(GtkButton *button, gpointer user_data);
void mt_window_find_next_clicked(GtkButton *button, gpointer user_data);
void mt_window_replace_clicked(GtkButton *button, gpointer user_data);
void mt_window_replace_all_clicked(GtkButton *button, gpointer user_data);
void mt_window_replace_mode_toggled(GtkToggleButton *button, gpointer user_data);
void mt_window_search_case_toggled(GtkCheckButton *button, gpointer user_data);
void mt_window_hide_find_bar(GtkButton *button, gpointer user_data);
gboolean mt_window_snapshot_all(MtWindow *window);
void mt_scheme_apply_cancel(MtWindow *window);
void mt_auto_update_check_free(MtAutoUpdateCheck *check);

#endif
