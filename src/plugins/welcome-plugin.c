/*
 * welcome-plugin.c
 * 新手引导插件：首次启动时展示 vellum 功能导览；导览结束后请求用户
 * 自我删除。若用户选择保留，主菜单会保留“新手引导”入口，可随时重开。
 */

#include "mt-plugin.h"

#include <adwaita.h>
#include <gmodule.h>
#include <glib/gstdio.h>

typedef struct _MtWelcomeData
{
    MtPluginHost *host;
    gchar *plugin_id;
    GtkWidget *guide_window;
    guint show_source_id;
    guint remove_source_id;
    guint show_count;
} MtWelcomeData;

static MtWelcomeData *welcome_data;

static void welcome_remove_response(AdwMessageDialog *dialog,
                                    const gchar *response,
                                    gpointer user_data);

static MtPluginInfo welcome_plugin_info = {
    MT_PLUGIN_API_VERSION,
    "io.github.vellum.welcome",
    "Welcome Guide",
    "Show the vellum welcome guide and ask to remove itself",
    "0.1.0"
};

static gboolean
welcome_is_zh(void)
{
    const gchar * const *languages;

    languages = g_get_language_names();
    return languages != NULL && languages[0] != NULL &&
           g_str_has_prefix(languages[0], "zh");
}

static gchar *
welcome_flag_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "vellum", "welcome-guide-shown", NULL);
}

static void
welcome_ask_remove(MtWelcomeData *data)
{
    AdwMessageDialog *dialog;
    const gchar *title;
    const gchar *body;
    const gchar *keep;
    const gchar *remove;
    GtkWindow *parent;

    if (welcome_is_zh())
    {
        title = "删除新手引导插件？";
        body = "新手引导已完成。要删除这个引导插件吗？删除后主菜单将不再显示引导入口。";
        keep = "保留";
        remove = "删除";
    }
    else
    {
        title = "Remove the welcome guide plugin?";
        body = "The welcome guide is complete. Remove the guide plugin? Its menu entry will disappear.";
        keep = "Keep";
        remove = "Remove";
    }

    parent = data->host->get_parent_window(data->host);
    dialog = ADW_MESSAGE_DIALOG(adw_message_dialog_new(parent, title, body));
    adw_message_dialog_add_response(dialog, "keep", keep);
    adw_message_dialog_add_response(dialog, "remove", remove);
    adw_message_dialog_set_response_appearance(dialog, "remove", ADW_RESPONSE_DESTRUCTIVE);
    adw_message_dialog_set_default_response(dialog, "keep");
    adw_message_dialog_set_close_response(dialog, "keep");
    g_signal_connect(dialog,
                     "response",
                     G_CALLBACK(welcome_remove_response),
                     data);
    gtk_window_present(GTK_WINDOW(dialog));
}

static gboolean
welcome_remove_idle(gpointer user_data)
{
    MtWelcomeData *data;
    MtPluginHost *host;

    data = user_data;
    host = data->host;
    data->remove_source_id = 0;
    host->request_plugin_removal(host, data->plugin_id);
    return G_SOURCE_REMOVE;
}

static void
welcome_remove_response(AdwMessageDialog *dialog,
                        const gchar *response,
                        gpointer user_data)
{
    MtWelcomeData *data;
    const gchar *toast;

    (void)dialog;

    data = user_data;
    if (g_strcmp0(response, "remove") != 0)
    {
        gtk_window_destroy(GTK_WINDOW(dialog));
        return;
    }

    toast = welcome_is_zh() ? "新手引导插件已删除" : "Welcome guide plugin removed";
    data->host->show_toast(data->host, toast);
    gtk_window_destroy(GTK_WINDOW(dialog));
    if (data->remove_source_id == 0)
    {
        data->remove_source_id = g_idle_add(welcome_remove_idle, data);
    }
}

static void
welcome_guide_close(MtWelcomeData *data)
{
    if (data->guide_window != NULL)
    {
        gtk_window_destroy(GTK_WINDOW(data->guide_window));
        data->guide_window = NULL;
    }
}

static void
welcome_get_started_clicked(GtkButton *button, gpointer user_data)
{
    MtWelcomeData *data;

    (void)button;

    data = user_data;
    welcome_guide_close(data);
    if (data->show_count == 1)
    {
        welcome_ask_remove(data);
    }
}

static gboolean
welcome_guide_close_request(GtkWidget *widget, gpointer user_data)
{
    MtWelcomeData *data;

    (void)widget;

    data = user_data;
    data->guide_window = NULL;
    if (data->show_count == 1)
    {
        welcome_ask_remove(data);
    }
    return FALSE;
}

static void
welcome_add_feature(AdwPreferencesGroup *group,
                    const gchar *icon_name,
                    const gchar *title,
                    const gchar *subtitle)
{
    AdwActionRow *row;
    GtkWidget *icon;

    row = ADW_ACTION_ROW(adw_action_row_new());
    icon = gtk_image_new_from_icon_name(icon_name);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), title);
    adw_action_row_set_subtitle(row, subtitle);
    adw_action_row_add_prefix(row, icon);
    adw_preferences_group_add(group, GTK_WIDGET(row));
}

static void
welcome_show_guide(MtWelcomeData *data)
{
    AdwWindow *guide;
    AdwHeaderBar *header;
    GtkWidget *toolbar;
    GtkWidget *content;
    GtkWidget *icon;
    GtkWidget *title;
    GtkWidget *subtitle;
    AdwPreferencesGroup *features;
    GtkWidget *start_button;
    GtkWindow *parent;
    const gchar *guide_title;
    const gchar *intro;
    const gchar *start_label;

    if (data->guide_window != NULL)
    {
        gtk_window_present(GTK_WINDOW(data->guide_window));
        return;
    }

    data->show_count++;
    if (data->show_count == 1)
    {
        gchar *flag;
        gchar *directory;

        flag = welcome_flag_path();
        directory = g_path_get_dirname(flag);
        g_mkdir_with_parents(directory, 0700);
        g_free(directory);
        g_file_set_contents(flag, "1", 1, NULL);
        g_free(flag);
    }

    if (welcome_is_zh())
    {
        guide_title = "欢迎使用 vellum";
        intro = "一个专注、清爽的 GTK4 文本编辑器";
        start_label = "开始使用";
    }
    else
    {
        guide_title = "Welcome to vellum";
        intro = "A clean, focused GTK4 text editor";
        start_label = "Get Started";
    }

    guide = ADW_WINDOW(adw_window_new());
    gtk_window_set_title(GTK_WINDOW(guide), guide_title);
    gtk_window_set_default_size(GTK_WINDOW(guide), 480, 560);
    parent = data->host->get_parent_window(data->host);
    if (parent != NULL)
    {
        gtk_window_set_transient_for(GTK_WINDOW(guide), parent);
        gtk_window_set_modal(GTK_WINDOW(guide), TRUE);
    }
    g_signal_connect(guide,
                     "close-request",
                     G_CALLBACK(welcome_guide_close_request),
                     data);

    toolbar = adw_toolbar_view_new();
    header = ADW_HEADER_BAR(adw_header_bar_new());
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), GTK_WIDGET(header));

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(content, 28);
    gtk_widget_set_margin_end(content, 28);
    gtk_widget_set_margin_top(content, 24);
    gtk_widget_set_margin_bottom(content, 24);

    icon = gtk_image_new_from_icon_name("io.github.vellum.Vellum");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 72);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), icon);

    title = gtk_label_new("vellum");
    gtk_widget_add_css_class(title, "title-1");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), title);

    subtitle = gtk_label_new(intro);
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(subtitle), TRUE);
    gtk_label_set_justify(GTK_LABEL(subtitle), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(content), subtitle);

    features = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    if (welcome_is_zh())
    {
        welcome_add_feature(features, "tab-new-symbolic",
                            "多文档标签页", "同时编辑多个文件，随时切换");
        welcome_add_feature(features, "edit-find-symbolic",
                            "查找与替换", "支持大小写、整词与全部替换");
        welcome_add_feature(features, "document-save-symbolic",
                            "本地草稿恢复", "意外退出后自动找回未保存内容");
        welcome_add_feature(features, "system-run-symbolic",
                            "可扩展插件", "AI 补全、构建运行、项目侧栏等");
    }
    else
    {
        welcome_add_feature(features, "tab-new-symbolic",
                            "Multiple documents", "Edit several files and switch freely");
        welcome_add_feature(features, "edit-find-symbolic",
                            "Find and replace", "Case, whole-word and replace-all support");
        welcome_add_feature(features, "document-save-symbolic",
                            "Draft recovery", "Restore unsaved content after a crash");
        welcome_add_feature(features, "system-run-symbolic",
                            "Extensible plugins", "AI completion, build & run, project sidebar");
    }
    gtk_box_append(GTK_BOX(content), GTK_WIDGET(features));

    start_button = gtk_button_new_with_label(start_label);
    gtk_widget_add_css_class(start_button, "suggested-action");
    gtk_widget_set_halign(start_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(start_button, 6);
    g_signal_connect(start_button,
                     "clicked",
                     G_CALLBACK(welcome_get_started_clicked),
                     data);
    gtk_box_append(GTK_BOX(content), start_button);

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), content);
    adw_window_set_content(guide, toolbar);
    data->guide_window = GTK_WIDGET(guide);
    gtk_window_present(GTK_WINDOW(guide));
}

static gboolean
welcome_auto_show(gpointer user_data)
{
    MtWelcomeData *data;

    data = user_data;
    data->show_source_id = 0;
    welcome_show_guide(data);
    return G_SOURCE_REMOVE;
}

static void
welcome_show_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWelcomeData *data;

    (void)action;
    (void)parameter;

    data = user_data;
    welcome_show_guide(data);
}

static void
welcome_data_free(gpointer user_data)
{
    MtWelcomeData *data;
    gboolean was_current;

    data = user_data;
    was_current = (welcome_data == data);
    if (data->show_source_id != 0)
    {
        g_source_remove(data->show_source_id);
        data->show_source_id = 0;
    }
    if (data->remove_source_id != 0)
    {
        g_source_remove(data->remove_source_id);
        data->remove_source_id = 0;
    }
    if (data->guide_window != NULL)
    {
        gtk_window_destroy(GTK_WINDOW(data->guide_window));
        data->guide_window = NULL;
    }
    g_free(data->plugin_id);
    g_free(data);
    if (was_current)
    {
        welcome_data = NULL;
    }
}

G_MODULE_EXPORT const MtPluginInfo *
mt_plugin_query(void)
{
    if (welcome_is_zh())
    {
        welcome_plugin_info.name = "新手引导";
        welcome_plugin_info.description = "展示新手引导并请求自我删除";
    }
    else
    {
        welcome_plugin_info.name = "Welcome Guide";
        welcome_plugin_info.description = "Show the vellum welcome guide and ask to remove itself";
    }

    return &welcome_plugin_info;
}

G_MODULE_EXPORT gboolean
mt_plugin_activate(MtPluginHost *host, GError **error)
{
    MtWelcomeData *data;
    gchar *flag;

    data = g_new0(MtWelcomeData, 1);
    data->host = host;
    data->plugin_id = g_strdup(welcome_plugin_info.id);
    welcome_data = data;

    if (!host->add_action(host,
                          "show-welcome",
                          welcome_show_action,
                          data,
                          (GDestroyNotify)welcome_data_free))
    {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_EXISTS,
                    "The show-welcome action is already registered");
        welcome_data = NULL;
        g_free(data->plugin_id);
        g_free(data);
        return FALSE;
    }

    flag = welcome_flag_path();
    if (!g_file_test(flag, G_FILE_TEST_EXISTS))
    {
        data->show_source_id = g_timeout_add(600, welcome_auto_show, data);
    }
    g_free(flag);

    return TRUE;
}

G_MODULE_EXPORT void
mt_plugin_deactivate(MtPluginHost *host)
{
    (void)host;

    if (welcome_data == NULL)
    {
        return;
    }

    if (welcome_data->show_source_id != 0)
    {
        g_source_remove(welcome_data->show_source_id);
        welcome_data->show_source_id = 0;
    }
    if (welcome_data->remove_source_id != 0)
    {
        g_source_remove(welcome_data->remove_source_id);
        welcome_data->remove_source_id = 0;
    }
    welcome_guide_close(welcome_data);
}
