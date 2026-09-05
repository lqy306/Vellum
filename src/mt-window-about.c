#include "mt-window-private.h"

typedef struct _MtUpdateCheck
{
    MtWindow *window;
    GtkWidget *dialog;
    GtkWidget *check_button;
    GtkWidget *result_label;
    GtkWidget *download_button;
    SoupSession *session;
    SoupMessage *message;
    GCancellable *cancellable;
} MtUpdateCheck;

static void
mt_update_check_free(MtUpdateCheck *check)
{
    if (check == NULL)
    {
        return;
    }
    if (check->cancellable != NULL)
    {
        g_cancellable_cancel(check->cancellable);
    }
    g_clear_object(&check->cancellable);
    g_clear_object(&check->message);
    g_clear_object(&check->session);
    g_free(check);
}

/* 比较 “YYYY.MM.DD” 形式的日期版本号；a>b 返回正数，相等返回 0。 */
static gint
mt_version_compare(const gchar *a, const gchar *b)
{
    guint av[3] = { 0, 0, 0 };
    guint bv[3] = { 0, 0, 0 };
    gchar **as;
    gchar **bs;
    guint index;

    as = g_strsplit(a != NULL ? a : "", ".", 3);
    bs = g_strsplit(b != NULL ? b : "", ".", 3);
    for (index = 0; index < 3; index++)
    {
        if (as[index] != NULL)
        {
            av[index] = (guint)g_ascii_strtoull(as[index], NULL, 10);
        }
        if (bs[index] != NULL)
        {
            bv[index] = (guint)g_ascii_strtoull(bs[index], NULL, 10);
        }
    }
    g_strfreev(as);
    g_strfreev(bs);
    for (index = 0; index < 3; index++)
    {
        if (av[index] != bv[index])
        {
            return av[index] < bv[index] ? -1 : 1;
        }
    }
    return 0;
}

static void
mt_window_update_check_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtUpdateCheck *check;
    GBytes *bytes;
    GError *error;
    guint status;

    check = user_data;
    error = NULL;
    bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    status = SOUP_STATUS_NONE;
    if (check->message != NULL)
    {
        status = soup_message_get_status(check->message);
    }
    g_clear_object(&check->cancellable);
    g_clear_object(&check->message);

    if (error != NULL)
    {
        gchar *text;

        text = g_strdup_printf(_("Check failed: %s"), error->message);
        gtk_label_set_text(GTK_LABEL(check->result_label), text);
        g_free(text);
        g_clear_error(&error);
        gtk_widget_set_sensitive(check->check_button, TRUE);
        return;
    }

    if (status == SOUP_STATUS_NOT_FOUND)
    {
        gtk_label_set_text(GTK_LABEL(check->result_label), _("No published releases yet"));
    }
    else if (status < 200 || status >= 300)
    {
        gchar *text;

        text = g_strdup_printf(_("Check failed: HTTP %u"), status);
        gtk_label_set_text(GTK_LABEL(check->result_label), text);
        g_free(text);
    }
    else if (bytes != NULL)
    {
        JsonParser *parser;
        JsonNode *root;
        const gchar *tag;

        parser = json_parser_new();
        if (json_parser_load_from_data(parser,
                                       g_bytes_get_data(bytes, NULL),
                                       g_bytes_get_size(bytes),
                                       &error))
        {
            root = json_parser_get_root(parser);
            tag = NULL;
            if (root != NULL && JSON_NODE_HOLDS_OBJECT(root))
            {
                JsonObject *object;

                object = json_node_get_object(root);
                tag = json_object_get_string_member_with_default(object, "tag_name", NULL);
            }
            if (tag != NULL && *tag != '\0')
            {
                gint comparison;

                comparison = mt_version_compare(tag, VELLUM_VERSION);
                if (comparison > 0)
                {
                    gchar *text;

                    text = g_strdup_printf(_("New version available: %s"), tag);
                    gtk_label_set_text(GTK_LABEL(check->result_label), text);
                    g_free(text);
                    gtk_widget_set_visible(check->download_button, TRUE);
                }
                else
                {
                    gtk_label_set_text(GTK_LABEL(check->result_label),
                                       _("You are up to date"));
                }
            }
            else
            {
                gtk_label_set_text(GTK_LABEL(check->result_label),
                                   _("Unable to read release information"));
            }
        }
        else
        {
            gtk_label_set_text(GTK_LABEL(check->result_label), _("Unable to read release information"));
            g_clear_error(&error);
        }
        g_object_unref(parser);
    }
    else
    {
        gtk_label_set_text(GTK_LABEL(check->result_label), _("Check failed: no response"));
    }

    g_bytes_unref(bytes);
    gtk_widget_set_sensitive(check->check_button, TRUE);
}

static void
mt_window_update_check_clicked(GtkButton *button, gpointer user_data)
{
    MtUpdateCheck *check;

    (void)button;
    check = user_data;
    if (check->message != NULL)
    {
        return;
    }

    gtk_widget_set_visible(check->download_button, FALSE);
    gtk_label_set_text(GTK_LABEL(check->result_label), _("Checking for updates…"));
    gtk_widget_set_sensitive(check->check_button, FALSE);

    if (check->session == NULL)
    {
        check->session = soup_session_new();
        g_object_set(check->session, "timeout", 20, NULL);
    }
    check->cancellable = g_cancellable_new();
    check->message = soup_message_new("GET",
                                      "https://api.github.com/repos/lqy306/Vellum/releases/latest");
    soup_message_headers_append(soup_message_get_request_headers(check->message),
                                "Accept",
                                "application/vnd.github+json");
    soup_message_headers_append(soup_message_get_request_headers(check->message),
                                "User-Agent",
                                "Vellum-update-check");
    soup_session_send_and_read_async(check->session,
                                     check->message,
                                     G_PRIORITY_DEFAULT,
                                     check->cancellable,
                                     mt_window_update_check_finished,
                                     check);
}

void
mt_auto_update_check_free(MtAutoUpdateCheck *check)
{
    if (check == NULL)
    {
        return;
    }
    if (check->cancellable != NULL)
    {
        g_cancellable_cancel(check->cancellable);
    }
    g_clear_object(&check->cancellable);
    g_clear_object(&check->message);
    g_clear_object(&check->session);
    g_free(check);
}

/* 启动后的静默检查：仅在发现更新时弹 Toast，其余情况一律不打扰用户。 */
static void
mt_window_auto_update_check_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    MtAutoUpdateCheck *check;
    GBytes *bytes;
    GError *error;
    guint status;

    check = user_data;
    error = NULL;
    bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);
    status = SOUP_STATUS_NONE;
    if (check->message != NULL)
    {
        status = soup_message_get_status(check->message);
    }

    if (check->window != NULL && check->window->auto_update_check == check)
    {
        check->window->auto_update_check = NULL;
    }

    if (error == NULL && status >= 200 && status < 300 && bytes != NULL)
    {
        JsonParser *parser;
        JsonNode *root;
        const gchar *tag;

        parser = json_parser_new();
        if (json_parser_load_from_data(parser,
                                       g_bytes_get_data(bytes, NULL),
                                       g_bytes_get_size(bytes),
                                       NULL))
        {
            root = json_parser_get_root(parser);
            tag = NULL;
            if (root != NULL && JSON_NODE_HOLDS_OBJECT(root))
            {
                JsonObject *object;

                object = json_node_get_object(root);
                tag = json_object_get_string_member_with_default(object, "tag_name", NULL);
            }
            if (tag != NULL && *tag != '\0' &&
                mt_version_compare(tag, VELLUM_VERSION) > 0)
            {
                MtWindow *window;

                window = check->window;
                if (window != NULL && !window->disposed &&
                    ADW_IS_TOAST_OVERLAY(window->toast_overlay))
                {
                    AdwToast *toast;
                    gchar *text;

                    text = g_strdup_printf(_("New version available: %s"), tag);
                    toast = adw_toast_new(text);
                    g_free(text);
                    adw_toast_set_button_label(toast, _("View"));
                    adw_toast_set_detailed_action_name(toast, "win.open-releases");
                    adw_toast_overlay_add_toast(window->toast_overlay, toast);
                }
            }
        }
        g_object_unref(parser);
    }

    g_bytes_unref(bytes);
    g_clear_error(&error);
    mt_auto_update_check_free(check);
}

static void
mt_window_auto_update_check_start(MtWindow *window)
{
    MtAutoUpdateCheck *check;

    if (window == NULL || window->disposed || window->auto_update_check != NULL)
    {
        return;
    }

    check = g_new0(MtAutoUpdateCheck, 1);
    check->window = window;
    check->session = soup_session_new();
    g_object_set(check->session, "timeout", 20, NULL);
    check->cancellable = g_cancellable_new();
    check->message = soup_message_new("GET",
                                      "https://api.github.com/repos/lqy306/Vellum/releases/latest");
    soup_message_headers_append(soup_message_get_request_headers(check->message),
                                "Accept",
                                "application/vnd.github+json");
    soup_message_headers_append(soup_message_get_request_headers(check->message),
                                "User-Agent",
                                "Vellum-update-check");
    window->auto_update_check = check;
    soup_session_send_and_read_async(check->session,
                                     check->message,
                                     G_PRIORITY_DEFAULT,
                                     check->cancellable,
                                     mt_window_auto_update_check_finished,
                                     check);
}

gboolean
mt_window_auto_update_check_idle(gpointer user_data)
{
    MtWindow *window;

    window = user_data;
    window->auto_update_source_id = 0;
    if (!window->disposed &&
        mt_settings_get_auto_check_updates(window->settings))
    {
        mt_window_auto_update_check_start(window);
    }
    return G_SOURCE_REMOVE;
}

void
mt_window_action_open_releases(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    GtkUriLauncher *launcher;

    (void)action;
    (void)parameter;
    window = user_data;
    launcher = gtk_uri_launcher_new("https://github.com/lqy306/Vellum/releases/latest");
    gtk_uri_launcher_launch(launcher,
                            GTK_WINDOW(window->window),
                            NULL,
                            NULL,
                            NULL);
    g_object_unref(launcher);
}

static void
mt_window_open_uri_clicked(GtkButton *button, gpointer user_data)
{
    const gchar *uri;
    GtkUriLauncher *launcher;

    (void)user_data;
    uri = g_object_get_data(G_OBJECT(button), "vellum-uri");
    if (uri == NULL)
    {
        return;
    }
    launcher = gtk_uri_launcher_new(uri);
    gtk_uri_launcher_launch(launcher,
                            GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(button))),
                            NULL,
                            NULL,
                            NULL);
    g_object_unref(launcher);
}

void
mt_window_action_about(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtWindow *window;
    MtUpdateCheck *check;
    AdwWindow *dialog;
    GtkWidget *toolbar;
    GtkWidget *header;
    GtkWidget *content;
    GtkWidget *icon;
    GtkWidget *name;
    GtkWidget *description;
    GtkWidget *version_label;
    GtkWidget *check_button;
    GtkWidget *result_label;
    GtkWidget *download_button;
    GtkWidget *repo_button;
    GtkWidget *footer;

    (void)action;
    (void)parameter;

    window = user_data;
    dialog = ADW_WINDOW(adw_window_new());
    gtk_window_set_title(GTK_WINDOW(dialog), _("About Vellum"));
    gtk_window_set_default_size(GTK_WINDOW(dialog), 420, 480);
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(window->window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);

    toolbar = adw_toolbar_view_new();
    header = adw_header_bar_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), header);

    content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_start(content, 28);
    gtk_widget_set_margin_end(content, 28);
    gtk_widget_set_margin_top(content, 28);
    gtk_widget_set_margin_bottom(content, 24);

    icon = gtk_image_new_from_icon_name("io.github.vellum.Vellum");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 96);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), icon);

    name = gtk_label_new("Vellum");
    gtk_widget_add_css_class(name, "title-1");
    gtk_widget_set_halign(name, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), name);

    description = gtk_label_new(_("A focused GTK4 text editor."));
    gtk_widget_add_css_class(description, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(description), TRUE);
    gtk_label_set_justify(GTK_LABEL(description), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(content), description);

    version_label = gtk_label_new(g_strdup_printf(_("Version %s"), VELLUM_VERSION));
    gtk_widget_add_css_class(version_label, "dim-label");
    gtk_widget_add_css_class(version_label, "caption");
    gtk_widget_set_halign(version_label, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(version_label, 4);
    gtk_box_append(GTK_BOX(content), version_label);

    check_button = gtk_button_new_with_label(_("Check for Updates"));
    gtk_widget_set_halign(check_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(check_button, 10);
    gtk_box_append(GTK_BOX(content), check_button);

    result_label = gtk_label_new("");
    gtk_widget_add_css_class(result_label, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(result_label), TRUE);
    gtk_label_set_justify(GTK_LABEL(result_label), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(content), result_label);

    download_button = gtk_button_new_with_label(_("Open Download Page"));
    gtk_widget_set_halign(download_button, GTK_ALIGN_CENTER);
    gtk_widget_set_visible(download_button, FALSE);
    g_object_set_data(G_OBJECT(download_button),
                      "vellum-uri",
                      "https://github.com/lqy306/Vellum/releases/latest");
    g_signal_connect(download_button,
                     "clicked",
                     G_CALLBACK(mt_window_open_uri_clicked),
                     NULL);
    gtk_box_append(GTK_BOX(content), download_button);

    repo_button = gtk_button_new_with_label(_("GitHub Repository"));
    gtk_widget_set_halign(repo_button, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(repo_button, 10);
    g_object_set_data(G_OBJECT(repo_button),
                      "vellum-uri",
                      "https://github.com/lqy306/Vellum");
    g_signal_connect(repo_button,
                     "clicked",
                     G_CALLBACK(mt_window_open_uri_clicked),
                     NULL);
    gtk_box_append(GTK_BOX(content), repo_button);

    footer = gtk_label_new(_("vellum is licensed under BSD-2-Clause."));
    gtk_widget_add_css_class(footer, "dim-label");
    gtk_widget_add_css_class(footer, "caption");
    gtk_widget_set_halign(footer, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(footer, 14);
    gtk_box_append(GTK_BOX(content), footer);

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), content);
    adw_window_set_content(dialog, toolbar);

    check = g_new0(MtUpdateCheck, 1);
    check->window = window;
    check->dialog = GTK_WIDGET(dialog);
    check->check_button = check_button;
    check->result_label = result_label;
    check->download_button = download_button;
    g_signal_connect(check_button,
                     "clicked",
                     G_CALLBACK(mt_window_update_check_clicked),
                     check);
    g_object_set_data_full(G_OBJECT(dialog),
                           "vellum-update-check",
                           check,
                           (GDestroyNotify)mt_update_check_free);

    gtk_window_present(GTK_WINDOW(dialog));
}
