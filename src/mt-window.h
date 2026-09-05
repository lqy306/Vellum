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

/* 窗口内部状态定义在 mt-window-private.h，避免被应用和插件错误依赖。 */

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
void mt_window_show_inline_diff(MtWindow *window, gint offset,
                                const gchar *old_text, const gchar *new_text);
void mt_window_clear_inline_diff(MtWindow *window);
void mt_window_apply_inline_diff(MtWindow *window);
/* 编译器报错的红色波浪下划线 + 悬停描述。offset/length 为字节偏移。 */
void mt_window_show_error_underline(MtWindow *window, gint offset,
                                    gint length, const gchar *message);
void mt_window_clear_error_underlines(MtWindow *window);
/* 断点：1-based 逻辑行号。set/clear 同步 gutter 标记。 */
void mt_window_set_breakpoint(MtWindow *window, gint line);
void mt_window_clear_breakpoint(MtWindow *window, gint line);
void mt_window_clear_all_breakpoints(MtWindow *window);
/* 滚动到指定 1-based 行并短暂高亮，用于错误跳转与断点定位。 */
void mt_window_scroll_to_line(MtWindow *window, gint line);
gchar *mt_window_get_text_after_cursor(MtWindow *window);
void mt_window_show_toast(MtWindow *window, const gchar *message);
gchar *mt_window_get_current_file_path(MtWindow *window);
void mt_window_open_file_path(MtWindow *window, const gchar *path);
/* 返回当前窗口已打开文档的绝对文件路径（仅已保存文件）；count 可为 NULL。
 * 以 NULL 结尾的 GStrv，调用者 g_strfreev 释放。 */
gchar **mt_window_get_open_document_paths(MtWindow *window, gsize *count);
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
void mt_window_action_toggle_breakpoint(GSimpleAction *action, GVariant *parameter, gpointer user_data);

G_END_DECLS

#endif
