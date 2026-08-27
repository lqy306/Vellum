/*
 * ai-completion-plugin.c
 * 通用 OpenAI 兼容 AI 补全扩展。密钥仅写入当前用户 0600 权限的本地配置文件。
 */

#include "mt-plugin.h"

#include <adwaita.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gmodule.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <string.h>

#define AI_COMPLETION_GROUP "AI Completion"
#define AI_CONTEXT_LIMIT 8000
#define AI_SUFFIX_LIMIT 2000
#define AI_AUTO_DELAY_MILLISECONDS 1000

typedef struct _AiRequest AiRequest;
typedef struct _AiConfigWidgets AiConfigWidgets;

struct _AiRequest
{
    MtPluginHost *host;
    SoupMessage *message;
    gchar *context;
    gchar *suffix;
    guint generation;
    gboolean automatic;
};

struct _AiCandidate
{
    MtPluginHost *host;
    gchar *text;
    gchar *context;
    gchar *suffix;
};

struct _AiConfigWidgets
{
    MtPluginHost *host;
    AdwEntryRow *endpoint_row;
    AdwEntryRow *model_row;
    AdwPasswordEntryRow *key_row;
    AdwSwitchRow *auto_row;
    GtkWindow *window;
};

static const MtPluginInfo ai_completion_plugin_info = {
    MT_PLUGIN_API_VERSION,
    "io.github.vellum.ai-completion",
    "AI Completion",
    "Complete text through a user-configured OpenAI-compatible API",
    "0.1.0"
};

static SoupSession *ai_session;
static struct _AiCandidate ai_candidate;
/* 每次请求或停用都会推进代际，过期回调绝不能再访问宿主。 */
static guint ai_generation;
/* 输入停顿后自动补全的防抖源；停用插件时必须移除。 */
static guint ai_auto_source_id;
static gboolean ai_auto_context_loaded;
static gchar *ai_auto_context;
static gboolean ai_request_in_flight;
/* 输入停顿后自动补全（无需快捷键）；由“偏好设置”中的开关控制并持久化。 */
static gboolean ai_auto_enabled = TRUE;

static gchar *
ai_completion_config_path(void)
{
    gchar *directory;
    gchar *path;

    directory = g_build_filename(g_get_user_config_dir(), "vellum", NULL);
    g_mkdir_with_parents(directory, 0700);
    path = g_build_filename(directory, "ai-completion.ini", NULL);
    g_free(directory);

    return path;
}

static GKeyFile *
ai_completion_load_settings(void)
{
    GKeyFile *settings;
    gchar *path;

    settings = g_key_file_new();
    path = ai_completion_config_path();
    g_key_file_load_from_file(settings, path, G_KEY_FILE_NONE, NULL);
    g_free(path);

    return settings;
}

static gchar *
ai_completion_get_setting(GKeyFile *settings, const gchar *key)
{
    gchar *value;

    value = g_key_file_get_string(settings, AI_COMPLETION_GROUP, key, NULL);
    if (value == NULL)
    {
        value = g_strdup("");
    }

    return value;
}

static gboolean
ai_completion_get_auto_enabled(GKeyFile *settings)
{
    GError *error;
    gboolean value;

    error = NULL;
    value = g_key_file_get_boolean(settings, AI_COMPLETION_GROUP, "auto-complete", &error);
    if (error != NULL)
    {
        g_clear_error(&error);
        return TRUE;
    }

    return value;
}

static void
ai_completion_set_auto_enabled(gboolean enabled)
{
    GKeyFile *settings;
    gchar *path;
    gchar *contents;

    settings = ai_completion_load_settings();
    g_key_file_set_boolean(settings, AI_COMPLETION_GROUP, "auto-complete", enabled);
    contents = g_key_file_to_data(settings, NULL, NULL);
    if (contents != NULL)
    {
        path = ai_completion_config_path();
        if (g_file_set_contents(path, contents, (gssize)strlen(contents), NULL))
        {
            g_chmod(path, 0600);
        }
        g_free(path);
        g_free(contents);
    }
    g_key_file_unref(settings);
}

static gboolean
ai_completion_pref_auto_get(gpointer user_data)
{
    (void)user_data;

    return ai_auto_enabled;
}

static void
ai_completion_pref_auto_set(gboolean value, gpointer user_data)
{
    (void)user_data;

    ai_auto_enabled = value;
    ai_completion_set_auto_enabled(value);
}

static gchar *
ai_completion_normalize_endpoint(const gchar *endpoint)
{
    gchar *normalized;
    GUri *uri;
    const gchar *path;
    gsize length;

    normalized = g_strdup(endpoint != NULL ? endpoint : "");
    g_strstrip(normalized);

    /* 统一去掉末尾斜杠，便于后续后缀匹配。 */
    length = strlen(normalized);
    while (length > 1 && normalized[length - 1] == '/')
    {
        normalized[--length] = '\0';
    }

    if (g_str_has_suffix(normalized, "/v1"))
    {
        gchar *with_path;

        with_path = g_strconcat(normalized, "/chat/completions", NULL);
        g_free(normalized);
        return with_path;
    }
    if (g_str_has_suffix(normalized, "/chat/completions"))
    {
        return normalized;
    }

    /* 裸主机地址（如 https://api.deepseek.com 或 https://api.deepseek.com/）
     * 自动补上补全端点，与主流 OpenAI 兼容客户端一致。 */
    uri = g_uri_parse(normalized, G_URI_FLAGS_NONE, NULL);
    path = uri != NULL ? g_uri_get_path(uri) : NULL;
    if (uri != NULL && (path == NULL || *path == '\0' || g_str_equal(path, "/")))
    {
        gchar *with_path;

        with_path = g_strconcat(normalized, "/chat/completions", NULL);
        g_free(normalized);
        normalized = with_path;
    }
    if (uri != NULL)
    {
        g_uri_unref(uri);
    }

    return normalized;
}

static gboolean
ai_completion_save_settings(const gchar *endpoint,
                            const gchar *model,
                            const gchar *api_key,
                            gboolean auto_enabled,
                            GError **error)
{
    GKeyFile *settings;
    gchar *path;
    gchar *contents;
    gsize length;
    gboolean saved;

    gchar *normalized_endpoint;

    normalized_endpoint = ai_completion_normalize_endpoint(endpoint);
    settings = g_key_file_new();
    g_key_file_set_string(settings, AI_COMPLETION_GROUP, "endpoint", normalized_endpoint);
    g_key_file_set_string(settings, AI_COMPLETION_GROUP, "model", model);
    g_key_file_set_string(settings, AI_COMPLETION_GROUP, "api-key", api_key);
    g_key_file_set_boolean(settings, AI_COMPLETION_GROUP, "auto-complete", auto_enabled);
    contents = g_key_file_to_data(settings, &length, error);

    if (contents == NULL)
    {
        g_free(normalized_endpoint);
        g_key_file_unref(settings);
        return FALSE;
    }

    path = ai_completion_config_path();
    saved = g_file_set_contents(path, contents, (gssize)length, error);
    if (saved)
    {
        g_chmod(path, 0600);
    }

    g_free(path);
    g_free(contents);
    g_free(normalized_endpoint);
    g_key_file_unref(settings);

    return saved;
}

static void
ai_completion_config_widgets_free(AiConfigWidgets *widgets)
{
    g_free(widgets);
}

static void
ai_completion_config_save_clicked(GtkButton *button, gpointer user_data)
{
    AiConfigWidgets *widgets;
    const gchar *endpoint;
    const gchar *model;
    const gchar *api_key;
    gboolean auto_enabled;
    GError *error;

    (void)button;

    widgets = user_data;
    endpoint = gtk_editable_get_text(GTK_EDITABLE(widgets->endpoint_row));
    model = gtk_editable_get_text(GTK_EDITABLE(widgets->model_row));
    api_key = gtk_editable_get_text(GTK_EDITABLE(widgets->key_row));
    auto_enabled = adw_switch_row_get_active(widgets->auto_row);
    error = NULL;

    if (!g_str_has_prefix(endpoint, "https://") && !g_str_has_prefix(endpoint, "http://"))
    {
        widgets->host->show_toast(widgets->host, _("AI endpoint must begin with https:// or http://"));
        return;
    }

    if (*model == '\0' || *api_key == '\0')
    {
        widgets->host->show_toast(widgets->host, _("AI model and API key are required"));
        return;
    }

    if (ai_completion_save_settings(endpoint, model, api_key, auto_enabled, &error))
    {
        widgets->host->show_toast(widgets->host, _("AI completion settings saved"));
        ai_auto_enabled = auto_enabled;
        gtk_window_destroy(widgets->window);
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to save AI settings: %s"), error->message);
        widgets->host->show_toast(widgets->host, message);
        g_free(message);
        g_clear_error(&error);
    }
}

static gchar *
ai_completion_trim_context(const gchar *text, glong limit)
{
    glong characters;
    const gchar *start;

    characters = g_utf8_strlen(text, -1);
    if (characters <= limit)
    {
        return g_strdup(text);
    }

    start = g_utf8_offset_to_pointer(text, characters - limit);
    return g_strdup(start);
}

static gchar *
ai_completion_build_body(const gchar *model, const gchar *prefix, const gchar *suffix)
{
    JsonBuilder *builder;
    JsonGenerator *generator;
    JsonNode *root;
    gchar *body;
    gchar *prompt;

    if (suffix != NULL && *suffix != '\0')
    {
        /* 与真实补全工具一致的 fill-in-the-middle 思路：同时给出光标前后文，
         * 让模型只补中间段；聊天模型用标记符描述前缀与后缀。 */
        prompt = g_strdup_printf("Complete the text at the cursor. The document text before the cursor is between <fim_prefix> and <fim_suffix>, and the text after the cursor follows <fim_suffix>. Return only the continuation that leads from the prefix toward the suffix, without markdown, explanation, fences, or repeating the existing text.\n\n<fim_prefix>\n%s\n<fim_suffix>\n%s\n<fim_middle>\n",
                                 prefix,
                                 suffix);
    }
    else
    {
        prompt = g_strdup_printf("Complete the text at the cursor. Return only the continuation, without markdown, explanation, fences, or repetition of the existing text.\n\n%s",
                                 prefix);
    }
    builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "model");
    json_builder_add_string_value(builder, model);
    json_builder_set_member_name(builder, "messages");
    json_builder_begin_array(builder);
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "role");
    json_builder_add_string_value(builder, "system");
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, "You are an inline text completion engine.");
    json_builder_end_object(builder);
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "role");
    json_builder_add_string_value(builder, "user");
    json_builder_set_member_name(builder, "content");
    json_builder_add_string_value(builder, prompt);
    json_builder_end_object(builder);
    json_builder_end_array(builder);
    /* 内联补全需要短、快的正文，不请求默认开启的思考链。 */
    json_builder_set_member_name(builder, "thinking");
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "type");
    json_builder_add_string_value(builder, "disabled");
    json_builder_end_object(builder);
    json_builder_set_member_name(builder, "temperature");
    json_builder_add_double_value(builder, 0.2);
    json_builder_set_member_name(builder, "max_tokens");
    json_builder_add_int_value(builder, 160);
    json_builder_set_member_name(builder, "stream");
    json_builder_add_boolean_value(builder, FALSE);
    json_builder_end_object(builder);

    root = json_builder_get_root(builder);
    generator = json_generator_new();
    json_generator_set_root(generator, root);
    body = json_generator_to_data(generator, NULL);

    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    g_free(prompt);

    return body;
}

static gchar *
ai_completion_extract_content_from_node(JsonNode *content_node)
{
    const gchar *content;

    content = NULL;
    if (content_node == NULL)
    {
        return NULL;
    }
    if (JSON_NODE_HOLDS_VALUE(content_node) &&
        json_node_get_value_type(content_node) == G_TYPE_STRING)
    {
        content = json_node_get_string(content_node);
    }
    else if (JSON_NODE_HOLDS_ARRAY(content_node))
    {
        /* 部分 OpenAI 兼容服务把 content 返回为 [{ "type": "text", "text": "..." }] */
        JsonArray *parts;
        guint part_index;

        parts = json_node_get_array(content_node);
        for (part_index = 0; part_index < json_array_get_length(parts); part_index++)
        {
            JsonNode *part_node;

            part_node = json_array_get_element(parts, part_index);
            if (!JSON_NODE_HOLDS_OBJECT(part_node))
            {
                continue;
            }
            if (json_object_has_member(json_node_get_object(part_node), "text"))
            {
                JsonNode *text_node;

                text_node = json_object_get_member(json_node_get_object(part_node), "text");
                if (JSON_NODE_HOLDS_VALUE(text_node) &&
                    json_node_get_value_type(text_node) == G_TYPE_STRING)
                {
                    content = json_node_get_string(text_node);
                    if (content != NULL && *content != '\0')
                    {
                        break;
                    }
                }
            }
        }
    }

    return content != NULL && *content != '\0' ? g_strdup(content) : NULL;
}

static gchar *
ai_completion_extract_content(const gchar *response, gsize length, GError **error)
{
    JsonParser *parser;
    JsonNode *root;
    JsonObject *object;
    JsonArray *choices;
    JsonObject *choice;
    JsonObject *message;
    const gchar *content;
    gchar *result;

    if (response == NULL || length == 0)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "AI response is empty");
        return NULL;
    }

    parser = json_parser_new();
    if (!json_parser_load_from_data(parser, response, (gssize)length, error))
    {
        g_object_unref(parser);
        return NULL;
    }

    root = json_parser_get_root(parser);
    if (!JSON_NODE_HOLDS_OBJECT(root))
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "AI response is not a JSON object");
        g_object_unref(parser);
        return NULL;
    }

    object = json_node_get_object(root);
    if (!json_object_has_member(object, "choices"))
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "AI response has no choices array");
        g_object_unref(parser);
        return NULL;
    }

    choices = json_object_get_array_member(object, "choices");
    if (json_array_get_length(choices) == 0)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "AI response has an empty choices array");
        g_object_unref(parser);
        return NULL;
    }

    choice = json_array_get_object_element(choices, 0);
    content = NULL;
    if (json_object_has_member(choice, "message"))
    {
        message = json_object_get_object_member(choice, "message");
        if (message != NULL && json_object_has_member(message, "content"))
        {
            JsonNode *content_node;

            content_node = json_object_get_member(message, "content");
            content = ai_completion_extract_content_from_node(content_node);
        }
    }
    if (content == NULL && json_object_has_member(choice, "delta"))
    {
        message = json_object_get_object_member(choice, "delta");
        if (message != NULL && json_object_has_member(message, "content"))
        {
            JsonNode *content_node;

            content_node = json_object_get_member(message, "content");
            content = ai_completion_extract_content_from_node(content_node);
        }
    }
    if (content == NULL && json_object_has_member(choice, "text"))
    {
        content = ai_completion_extract_content_from_node(json_object_get_member(choice, "text"));
    }

    if (content == NULL)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "AI response contains no completion text");
        g_object_unref(parser);
        return NULL;
    }

    result = content;
    g_object_unref(parser);

    return result;
}

static gchar *
ai_completion_error_detail(GBytes *bytes)
{
    const gchar *data;
    gsize length;

    if (bytes == NULL)
    {
        return g_strdup("");
    }
    data = g_bytes_get_data(bytes, &length);
    if (data == NULL || length == 0)
    {
        return g_strdup("");
    }

    return g_strndup(data, MIN(length, (gsize)160));
}

/* 内联候选必须与屏幕上实际预览的内容完全相同。
 * 多行响应无法可靠地在光标后的单行 overlay 中排版，因此只接受第一条非空逻辑行。 */
static gchar *
ai_completion_prepare_inline_candidate(const gchar *completion)
{
    const gchar *start;
    const gchar *end;

    if (completion == NULL)
    {
        return NULL;
    }

    start = completion;
    while (*start == '\r' || *start == '\n')
    {
        start++;
    }
    end = strpbrk(start, "\r\n");
    if (end == NULL)
    {
        end = start + strlen(start);
    }
    if (end == start)
    {
        return NULL;
    }

    return g_strndup(start, (gsize)(end - start));
}

static void
ai_completion_clear_candidate(void)
{
    if (ai_candidate.host != NULL && ai_candidate.host->clear_inline_completion != NULL)
    {
        ai_candidate.host->clear_inline_completion(ai_candidate.host);
    }
    g_clear_pointer(&ai_candidate.text, g_free);
    g_clear_pointer(&ai_candidate.context, g_free);
    g_clear_pointer(&ai_candidate.suffix, g_free);
    ai_candidate.host = NULL;
}

static void
ai_completion_auto_cancel(void)
{
    if (ai_auto_source_id != 0)
    {
        g_source_remove(ai_auto_source_id);
        ai_auto_source_id = 0;
    }
}

static void
ai_completion_request_start(MtPluginHost *host, gboolean automatic);

static gboolean
ai_completion_key_is_editing(guint keyval, guint state)
{
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) != 0)
    {
        return FALSE;
    }
    if ((keyval >= GDK_KEY_Left && keyval <= GDK_KEY_Down) ||
        (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F12) ||
        keyval == GDK_KEY_Home || keyval == GDK_KEY_End ||
        keyval == GDK_KEY_Page_Up || keyval == GDK_KEY_Page_Down ||
        keyval == GDK_KEY_Insert || keyval == GDK_KEY_Tab)
    {
        return FALSE;
    }

    return TRUE;
}

static gboolean
ai_completion_auto_cb(gpointer user_data)
{
    MtPluginHost *host;

    host = user_data;
    ai_auto_source_id = 0;
    if (ai_session == NULL)
    {
        return G_SOURCE_REMOVE;
    }
    ai_completion_request_start(host, TRUE);
    return G_SOURCE_REMOVE;
}

static void
ai_completion_auto_schedule(MtPluginHost *host)
{
    if (ai_auto_source_id != 0)
    {
        g_source_remove(ai_auto_source_id);
    }
    ai_auto_source_id = g_timeout_add(AI_AUTO_DELAY_MILLISECONDS,
                                      ai_completion_auto_cb,
                                      host);
}

static gboolean
ai_completion_handle_key(MtPluginHost *host,
                         guint keyval,
                         guint keycode,
                         guint state,
                         gpointer user_data)
{
    gchar *current_context;

    (void)keycode;
    (void)user_data;
    if (ai_candidate.text != NULL && ai_candidate.host == host)
    {
        current_context = host->get_text_before_cursor(host);
        if (g_strcmp0(current_context, ai_candidate.context) == 0)
        {
            if (keyval == GDK_KEY_Tab && state == 0)
            {
                gchar *accepted;

                /* 先移除文本视图覆盖层，再作为一次用户操作插入同一份候选文本。
                 * 避免 buffer 的 changed 回调与 GTK overlay 重绘交错造成半透明残影。 */
                accepted = g_strdup(ai_candidate.text);
                g_free(current_context);
                ai_completion_clear_candidate();
                host->insert_text(host, accepted);
                g_free(accepted);
                return TRUE;
            }
            if (keyval == GDK_KEY_Escape && state == 0)
            {
                g_free(current_context);
                ai_completion_clear_candidate();
                return TRUE;
            }
        }
        g_free(current_context);
        ai_completion_clear_candidate();
    }

    if (ai_auto_enabled && ai_completion_key_is_editing(keyval, state))
    {
        ai_completion_auto_schedule(host);
    }
    return FALSE;
}

static void
ai_completion_request_free(AiRequest *request)
{
    if (request == NULL)
    {
        return;
    }
    g_clear_object(&request->message);
    g_clear_pointer(&request->context, g_free);
    g_clear_pointer(&request->suffix, g_free);
    g_free(request);
}

static void
ai_completion_request_finished(GObject *source, GAsyncResult *result, gpointer user_data)
{
    AiRequest *request;
    GBytes *bytes;
    GError *error;
    const gchar *response;
    gsize length;
    guint status;
    gchar *completion;

    request = user_data;
    if (request->generation == ai_generation)
    {
        ai_request_in_flight = FALSE;
    }
    error = NULL;
    bytes = soup_session_send_and_read_finish(SOUP_SESSION(source), result, &error);

    /* 会话停用或有较新的请求时，只完成并释放 I/O，不再触及插件宿主。 */
    if (request->generation != ai_generation || ai_session == NULL)
    {
        g_clear_error(&error);
        g_clear_pointer(&bytes, g_bytes_unref);
        ai_completion_request_free(request);
        return;
    }

    if (bytes == NULL)
    {
        if (error == NULL || !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            gchar *message;

            message = g_strdup_printf(_("AI completion request failed: %s"),
                                      error != NULL ? error->message : _("No response received"));
            if (!request->automatic)
            {
                request->host->show_toast(request->host, message);
            }
            g_free(message);
        }
        g_clear_error(&error);
        ai_completion_request_free(request);
        return;
    }

    status = soup_message_get_status(request->message);
    if (status < 200 || status >= 300)
    {
        gchar *message;
        gchar *detail;

        detail = ai_completion_error_detail(bytes);
        message = g_strdup_printf(_("AI service returned HTTP %u: %s"),
                                  status,
                                  (detail != NULL && *detail != '\0') ?
                                  detail : soup_message_get_reason_phrase(request->message));
        if (!request->automatic)
        {
            request->host->show_toast(request->host, message);
        }
        g_free(detail);
        g_free(message);
        g_bytes_unref(bytes);
        ai_completion_request_free(request);
        return;
    }

    response = g_bytes_get_data(bytes, &length);
    completion = ai_completion_extract_content(response, length, &error);
    if (completion != NULL)
    {
        gchar *current_context;
        gchar *current_suffix;
        gchar *inline_candidate;

        inline_candidate = ai_completion_prepare_inline_candidate(completion);
        current_context = request->host->get_text_before_cursor(request->host);
        current_suffix = request->host->get_text_after_cursor != NULL ?
                         request->host->get_text_after_cursor(request->host) : g_strdup("");
        if (inline_candidate != NULL &&
            g_strcmp0(current_context, request->context) == 0 &&
            g_strcmp0(current_suffix, request->suffix) == 0)
        {
            ai_completion_clear_candidate();
            ai_candidate.host = request->host;
            ai_candidate.text = inline_candidate;
            ai_candidate.context = g_strdup(request->context);
            ai_candidate.suffix = g_strdup(request->suffix);
            request->host->show_inline_completion(request->host, ai_candidate.text);
            if (!request->automatic)
            {
                request->host->show_toast(request->host, _("AI completion ready: press Tab to accept or Escape to dismiss"));
            }
            inline_candidate = NULL;
        }
        g_free(current_context);
        g_free(current_suffix);
        g_free(inline_candidate);
        g_free(completion);
    }
    else
    {
        gchar *message;

        message = g_strdup_printf(_("Unable to parse AI completion: %s"), error->message);
        if (!request->automatic)
        {
            request->host->show_toast(request->host, message);
        }
        g_free(message);
        g_clear_error(&error);
    }

    g_bytes_unref(bytes);
    ai_completion_request_free(request);
}

static void
ai_completion_request_start(MtPluginHost *host, gboolean automatic)
{
    GKeyFile *settings;
    gchar *endpoint;
    gchar *model;
    gchar *api_key;
    gchar *context;
    gchar *suffix;
    gchar *trimmed_context;
    gchar *trimmed_suffix;
    gchar *body;
    gchar *authorization;
    SoupMessage *message;
    GBytes *body_bytes;
    AiRequest *request;

    if (automatic && !ai_auto_enabled)
    {
        /* 自动补全已在“偏好设置”中关闭，忽略迟到的防抖回调。 */
        return;
    }

    settings = ai_completion_load_settings();
    endpoint = ai_completion_get_setting(settings, "endpoint");
    model = ai_completion_get_setting(settings, "model");
    api_key = ai_completion_get_setting(settings, "api-key");
    g_key_file_unref(settings);

    /* 兼容旧版本已保存的 /v1 基础地址，实际请求始终指向补全端点。 */
    {
        gchar *normalized_endpoint;

        normalized_endpoint = ai_completion_normalize_endpoint(endpoint);
        g_free(endpoint);
        endpoint = normalized_endpoint;
    }

    if (*endpoint == '\0' || *model == '\0' || *api_key == '\0')
    {
        if (!automatic)
        {
            host->show_toast(host, _("Configure an AI endpoint, model and API key in Extensions first"));
        }
        g_free(endpoint);
        g_free(model);
        g_free(api_key);
        return;
    }

    context = host->get_text_before_cursor(host);
    {
        gchar *trimmed_check;

        trimmed_check = g_strdup(context);
        g_strstrip(trimmed_check);
        if (*trimmed_check == '\0')
        {
            g_free(trimmed_check);
            if (!automatic)
            {
                host->show_toast(host, _("Type some text before requesting AI completion"));
            }
            g_free(context);
            g_free(endpoint);
            g_free(model);
            g_free(api_key);
            return;
        }
        g_free(trimmed_check);
    }

    suffix = host->get_text_after_cursor != NULL ?
             host->get_text_after_cursor(host) : g_strdup("");
    trimmed_context = ai_completion_trim_context(context, AI_CONTEXT_LIMIT);
    trimmed_suffix = ai_completion_trim_context(suffix, AI_SUFFIX_LIMIT);
    if (automatic && ai_auto_context_loaded && g_strcmp0(trimmed_context, ai_auto_context) == 0)
    {
        /* 与上次自动请求相同的上下文，避免重复消耗额度。 */
        g_free(trimmed_suffix);
        g_free(trimmed_context);
        g_free(suffix);
        g_free(context);
        g_free(endpoint);
        g_free(model);
        g_free(api_key);
        return;
    }
    g_free(ai_auto_context);
    ai_auto_context = g_strdup(trimmed_context);
    ai_auto_context_loaded = TRUE;
    body = ai_completion_build_body(model, trimmed_context, trimmed_suffix);
    message = soup_message_new("POST", endpoint);
    if (message == NULL)
    {
        if (!automatic)
        {
            host->show_toast(host, _("AI endpoint URL is invalid"));
        }
        g_free(body);
        g_free(trimmed_context);
        g_free(trimmed_suffix);
        g_free(context);
        g_free(suffix);
        g_free(endpoint);
        g_free(model);
        g_free(api_key);
        return;
    }

    authorization = g_strdup_printf("Bearer %s", api_key);
    soup_message_headers_append(soup_message_get_request_headers(message), "Authorization", authorization);
    soup_message_headers_append(soup_message_get_request_headers(message), "Accept", "application/json");
    body_bytes = g_bytes_new_take(body, strlen(body));
    soup_message_set_request_body_from_bytes(message, "application/json", body_bytes);
    g_bytes_unref(body_bytes);

    if (ai_request_in_flight)
    {
        /* 新输入使旧请求作废：推进代际并中止会话，旧回调只释放 I/O。 */
        ai_generation++;
        soup_session_abort(ai_session);
    }
    ai_completion_clear_candidate();
    ai_generation++;
    request = g_new0(AiRequest, 1);
    request->host = host;
    request->message = g_object_ref(message);
    request->context = g_strdup(context);
    request->suffix = g_strdup(suffix);
    request->generation = ai_generation;
    request->automatic = automatic;
    ai_request_in_flight = TRUE;
    soup_session_send_and_read_async(ai_session,
                                     message,
                                     G_PRIORITY_DEFAULT,
                                     NULL,
                                     ai_completion_request_finished,
                                     request);
    if (!automatic)
    {
        host->show_toast(host, _("Requesting AI completion…"));
    }

    g_object_unref(message);
    g_free(authorization);
    g_free(trimmed_context);
    g_free(trimmed_suffix);
    g_free(context);
    g_free(suffix);
    g_free(endpoint);
    g_free(model);
    g_free(api_key);
}

static void
ai_completion_activate_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    MtPluginHost *host;

    (void)action;
    (void)parameter;

    host = user_data;
    ai_completion_auto_cancel();
    ai_completion_request_start(host, FALSE);
}

G_MODULE_EXPORT const MtPluginInfo *
mt_plugin_query(void)
{
    return &ai_completion_plugin_info;
}

G_MODULE_EXPORT gboolean
mt_plugin_activate(MtPluginHost *host, GError **error)
{
    static const gchar *accelerators[] = { "<Primary><Shift>space", NULL };

    if (host->show_inline_completion == NULL || host->clear_inline_completion == NULL ||
        host->add_key_handler == NULL)
    {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_NOT_SUPPORTED,
                    "Vellum host does not provide inline completion services");
        return FALSE;
    }

    if (ai_session == NULL)
    {
        ai_session = soup_session_new();
        g_object_set(ai_session, "timeout", 30, NULL);
    }
    ai_auto_context_loaded = FALSE;
    g_clear_pointer(&ai_auto_context, g_free);
    ai_request_in_flight = FALSE;
    {
        GKeyFile *settings;

        settings = ai_completion_load_settings();
        ai_auto_enabled = ai_completion_get_auto_enabled(settings);
        g_key_file_unref(settings);
    }

    if (!host->add_action(host,
                          "ai-complete",
                          ai_completion_activate_action,
                          host,
                          NULL))
    {
        g_set_error(error,
                    G_IO_ERROR,
                    G_IO_ERROR_EXISTS,
                    "The ai-complete action is already registered");
        return FALSE;
    }

    host->set_accelerators(host, "app.ai-complete", accelerators);
    host->add_key_handler(host, ai_completion_handle_key, NULL, NULL);

    if (host->add_preference_switch != NULL)
    {
        host->add_preference_switch(host,
                                    _("AI Completion"),
                                    _("Automatic AI completion"),
                                    _("Wait for a short pause after typing, then request a completion without pressing a shortcut."),
                                    ai_completion_pref_auto_get,
                                    ai_completion_pref_auto_set,
                                    NULL,
                                    NULL);
    }

    return TRUE;
}

G_MODULE_EXPORT void
mt_plugin_deactivate(MtPluginHost *host)
{
    (void)host;

    ai_completion_auto_cancel();
    ai_completion_clear_candidate();
    g_clear_pointer(&ai_auto_context, g_free);
    ai_auto_context_loaded = FALSE;
    ai_request_in_flight = FALSE;
    ai_generation++;
    if (ai_session != NULL)
    {
        soup_session_abort(ai_session);
        g_clear_object(&ai_session);
    }
}

G_MODULE_EXPORT void
mt_plugin_configure(MtPluginHost *host, gpointer parent_window)
{
    GKeyFile *settings;
    gchar *endpoint;
    gchar *model;
    gchar *api_key;
    gboolean auto_enabled;
    AdwPreferencesWindow *window;
    AdwPreferencesPage *page;
    AdwPreferencesGroup *group;
    AdwActionRow *save_row;
    GtkWidget *save_button;
    AiConfigWidgets *widgets;

    settings = ai_completion_load_settings();
    endpoint = ai_completion_get_setting(settings, "endpoint");
    model = ai_completion_get_setting(settings, "model");
    api_key = ai_completion_get_setting(settings, "api-key");
    auto_enabled = ai_completion_get_auto_enabled(settings);
    g_key_file_unref(settings);

    window = ADW_PREFERENCES_WINDOW(adw_preferences_window_new());
    gtk_window_set_title(GTK_WINDOW(window), _("AI Completion Settings"));
    gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(parent_window));
    gtk_window_set_modal(GTK_WINDOW(window), TRUE);
    page = ADW_PREFERENCES_PAGE(adw_preferences_page_new());
    group = ADW_PREFERENCES_GROUP(adw_preferences_group_new());
    adw_preferences_group_set_title(group, _("OpenAI-Compatible Service"));
    adw_preferences_group_set_description(group,
                                          _("Enter a full Chat Completions URL, an OpenAI-compatible URL ending in /v1, or a bare service host. Text around the cursor is sent to this service."));

    widgets = g_new0(AiConfigWidgets, 1);
    widgets->host = host;
    widgets->window = GTK_WINDOW(window);
    widgets->endpoint_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(widgets->endpoint_row), _("API Endpoint URL"));
    gtk_editable_set_text(GTK_EDITABLE(widgets->endpoint_row), endpoint);
    adw_preferences_group_add(group, GTK_WIDGET(widgets->endpoint_row));

    widgets->model_row = ADW_ENTRY_ROW(adw_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(widgets->model_row), _("Model"));
    gtk_editable_set_text(GTK_EDITABLE(widgets->model_row), model);
    adw_preferences_group_add(group, GTK_WIDGET(widgets->model_row));

    widgets->key_row = ADW_PASSWORD_ENTRY_ROW(adw_password_entry_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(widgets->key_row), _("API Key"));
    gtk_editable_set_text(GTK_EDITABLE(widgets->key_row), api_key);
    adw_preferences_group_add(group, GTK_WIDGET(widgets->key_row));

    widgets->auto_row = ADW_SWITCH_ROW(adw_switch_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(widgets->auto_row),
                                  _("Suggest automatically while typing"));
    adw_action_row_set_subtitle(ADW_ACTION_ROW(widgets->auto_row),
                                _("Wait for a short pause after typing, then request a completion without pressing a shortcut."));
    adw_switch_row_set_active(widgets->auto_row, auto_enabled);
    adw_preferences_group_add(group, GTK_WIDGET(widgets->auto_row));

    save_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(save_row), _("Save AI Settings"));
    save_button = gtk_button_new_with_label(_("Save"));
    gtk_widget_set_valign(save_button, GTK_ALIGN_CENTER);
    adw_action_row_add_suffix(save_row, save_button);
    adw_preferences_group_add(group, GTK_WIDGET(save_row));
    g_signal_connect(save_button,
                     "clicked",
                     G_CALLBACK(ai_completion_config_save_clicked),
                     widgets);
    g_object_set_data_full(G_OBJECT(window),
                           "vellum-ai-config-widgets",
                           widgets,
                           (GDestroyNotify)ai_completion_config_widgets_free);

    adw_preferences_page_add(page, group);
    adw_preferences_window_add(window, page);
    gtk_window_present(GTK_WINDOW(window));

    g_free(endpoint);
    g_free(model);
    g_free(api_key);
}
