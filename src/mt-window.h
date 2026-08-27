/*
 * mt-window.h
 * 主窗口公开接口，供应用对象和插件主机调用。
 */

#ifndef MT_WINDOW_H
#define MT_WINDOW_H

#include <adwaita.h>

#include "mt-document.h"
#include "mt-plugin.h"
#include "mt-settings.h"

G_BEGIN_DECLS

typedef struct _MtWindow MtWindow;

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
    /* 右侧检查器在宽屏固定停靠，窄屏自动成为不挤压编辑器的抽屉。 */
    AdwOverlaySplitView *auxiliary_split;
    GtkLabel *language_label;
    GtkLabel *position_label;
    /* 标题栏的文档身份区，保持与当前标签和文件状态同步。 */
    GtkLabel *header_title_label;
    GtkLabel *header_subtitle_label;
    GtkWidget *header_modified_indicator;
    /* OverlaySplitView 负责抽屉边界与显隐，不再维护易残留的手动分隔线。 */
    GtkToggleButton *replace_mode_button;
    GtkWidget *replace_button;
    GtkWidget *replace_all_button;
    gboolean search_case_sensitive;
    gboolean search_whole_word;
    GtkWidget *external_change_bar;
    MtDocument *external_change_document;
    /* AI 候选层归属的文档可能不同于当前选中标签页。 */
    MtDocument *inline_completion_document;
    GtkCssProvider *css_provider;
    GdkDisplay *display;
    MtSettings *settings;
    gpointer plugin_manager;
    /* 文档属性面板（右侧栏，学习原版 GNOME Text Editor）。 */
    GtkToggleButton *properties_button;
    gpointer properties;
    guint properties_source_id;
    /* 主菜单顶部的主题切换与缩放控件（学习原版 GNOME Text Editor）。 */
    GtkCheckButton *menu_theme_follow;
    GtkCheckButton *menu_theme_light;
    GtkCheckButton *menu_theme_dark;
    GtkButton *menu_zoom_label;
    /* 主菜单模型与新手引导入口所在的分区；插件加载状态变化时动态增删。 */
    GMenu *primary_menu;
    GMenu *welcome_menu_section;
    gboolean welcome_menu_present;
    /* 配色切换的异步分片应用状态（大文档下避免全量重绘卡死）。 */
    gpointer scheme_apply;
    guint snapshot_source_id;
    guint language_source_id;
    /* 启动时自动检查更新的延迟计时器与进行中的请求。 */
    guint auto_update_source_id;
    gpointer auto_update_check;
    gulong style_manager_handler_id;
    gboolean disposed;
};

MtWindow *mt_window_new(AdwApplication *application, MtSettings *settings);
void mt_window_free(MtWindow *window);
GtkWindow *mt_window_get_gtk_window(MtWindow *window);
void mt_window_set_plugin_manager(MtWindow *window, gpointer plugin_manager);

void mt_window_new_document(MtWindow *window);
void mt_window_open_files(MtWindow *window, GListModel *files);
void mt_window_open_file(MtWindow *window, GFile *file);
guint mt_window_restore_snapshots(MtWindow *window);
MtDocument *mt_window_get_current_document(MtWindow *window);
void mt_window_insert_text(MtWindow *window, const gchar *text);
void mt_window_show_inline_completion(MtWindow *window, const gchar *text);
void mt_window_clear_inline_completion(MtWindow *window);
gchar *mt_window_get_text_after_cursor(MtWindow *window);
void mt_window_show_toast(MtWindow *window, const gchar *message);
gchar *mt_window_get_current_file_path(MtWindow *window);
void mt_window_open_file_path(MtWindow *window, const gchar *path);
gboolean mt_window_run_editor_command(MtWindow *window,
                                      MtPluginEditorCommand command);
void mt_window_set_plugin_panel(MtWindow *window,
                                const gchar *id,
                                MtPluginPanelLocation location,
                                GtkWidget *panel);
void mt_window_hide_plugin_panel(MtWindow *window,
                                 const gchar *id,
                                 MtPluginPanelLocation location);
/* 插件加载状态变化后刷新主菜单（例如新手引导入口的显示与隐藏）。 */
void mt_window_sync_plugin_menu(MtWindow *window);

void mt_window_action_new(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_open(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_save(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_save_as(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_close(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_find(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_replace(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_zoom_in(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_zoom_out(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_zoom_reset(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_preferences(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_shortcuts(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_extensions(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data);
void mt_window_action_open_releases(GSimpleAction *action, GVariant *parameter, gpointer user_data);

G_END_DECLS

#endif
