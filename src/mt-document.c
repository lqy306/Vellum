/*
 * mt-document.c
 * 所有文件操作由 GtkSourceView 的异步加载器和保存器完成，避免阻塞界面。
 */

#include "mt-document.h"
#include "mt-settings.h"

#include <libspelling.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <string.h>

#define MT_DRAFT_DIRECTORY "vellum/drafts"
#define MT_ANALYSIS_DELAY_MILLISECONDS 450
#define MT_DIAGNOSTIC_TAG "vellum-diagnostic-error"

static void mt_document_apply_diagnostics(MtDocument *document);

static gboolean
mt_document_is_code_buffer(MtDocument *document)
{
    return gtk_source_buffer_get_language(document->buffer) != NULL;
}

static gboolean
mt_document_insert_pair(MtDocument *document, gunichar opening)
{
    GtkTextBuffer *buffer;
    GtkTextIter start;
    GtkTextIter end;
    GtkTextIter iter;
    gunichar closing;
    gchar opening_text[7];
    gchar closing_text[7];
    gint opening_length;
    gint closing_length;

    if (!mt_document_is_code_buffer(document) ||
        gtk_text_buffer_get_selection_bounds(GTK_TEXT_BUFFER(document->buffer), &start, &end))
    {
        return FALSE;
    }

    closing = 0;
    switch (opening)
    {
        case '<': closing = '>'; break;
        case '(': closing = ')'; break;
        case '[': closing = ']'; break;
        case '{': closing = '}'; break;
        case '\'': closing = '\''; break;
        case '"': closing = '"'; break;
        case '`': closing = '`'; break;
        default: return FALSE;
    }

    buffer = GTK_TEXT_BUFFER(document->buffer);
    gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
    opening_length = g_unichar_to_utf8(opening, opening_text);
    closing_length = g_unichar_to_utf8(closing, closing_text);
    opening_text[opening_length] = '\0';
    closing_text[closing_length] = '\0';
    gtk_text_buffer_begin_user_action(buffer);
    gtk_text_buffer_insert(buffer, &iter, opening_text, -1);
    gtk_text_buffer_insert(buffer, &iter, closing_text, -1);
    gtk_text_iter_backward_char(&iter);
    gtk_text_buffer_place_cursor(buffer, &iter);
    gtk_text_buffer_end_user_action(buffer);

    return TRUE;
}

static gboolean
mt_document_key_pressed(GtkEventControllerKey *controller,
                        guint keyval,
                        guint keycode,
                        GdkModifierType state,
                        gpointer user_data)
{
    MtDocument *document;
    gunichar character;

    (void)controller;
    (void)keycode;
    if ((state & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK | GDK_META_MASK)) != 0)
    {
        return FALSE;
    }

    document = user_data;
    character = gdk_keyval_to_unicode(keyval);
    return character != 0 && mt_document_insert_pair(document, character);
}

static gboolean
mt_document_analysis_cb(gpointer user_data)
{
    MtDocument *document;

    document = user_data;
    document->analysis_source_id = 0;
    mt_document_guess_language(document);
    mt_document_apply_diagnostics(document);

    return G_SOURCE_REMOVE;
}

static void
mt_document_buffer_changed(GtkTextBuffer *buffer, gpointer user_data)
{
    MtDocument *document;

    (void)buffer;
    document = user_data;
    mt_document_schedule_analysis(document);
}

static gchar *
mt_document_make_draft_path(void)
{
    gchar *directory;
    gchar *identifier;
    gchar *path;

    directory = g_build_filename(g_get_user_state_dir(), MT_DRAFT_DIRECTORY, NULL);
    g_mkdir_with_parents(directory, 0700);

    identifier = g_uuid_string_random();
    path = g_build_filename(directory, identifier, NULL);

    g_free(identifier);
    g_free(directory);

    return path;
}

/* 默认语言（跟随系统 locale）若没有可用词典，回退到英语词典，否则拼写检查会空转。 */
static SpellingChecker *
mt_document_create_spell_checker(void)
{
    static const gchar *const fallbacks[] = { "en_US", "en_GB", "en", NULL };
    SpellingProvider *provider;
    const gchar *default_code;
    const gchar *language;
    gint i;

    provider = spelling_provider_get_default();
    default_code = spelling_provider_get_default_code(provider);
    language = NULL;

    if (default_code != NULL && spelling_provider_supports_language(provider, default_code))
    {
        language = default_code;
    }
    else
    {
        for (i = 0; fallbacks[i] != NULL; i++)
        {
            if (spelling_provider_supports_language(provider, fallbacks[i]))
            {
                language = fallbacks[i];
                break;
            }
        }
    }

    return spelling_checker_new(provider, language);
}

static void
mt_document_spelling_popover_closed(GtkPopover *popover, gpointer user_data)
{
    (void)user_data;
    gtk_widget_unparent(GTK_WIDGET(popover));
    g_object_unref(popover);
}

static void
mt_document_context_menu_pressed(GtkGestureClick *gesture,
                                 gint n_press,
                                 gdouble x,
                                 gdouble y,
                                 MtDocument *document)
{
    GtkTextIter iter;
    GdkRectangle rect;
    GMenuModel *menu_model;
    GtkWidget *popover;
    gint bx;
    gint by;

    (void)gesture;
    (void)n_press;

    if (document->spell_adapter == NULL ||
        !spelling_text_buffer_adapter_get_enabled(document->spell_adapter))
    {
        return;
    }

    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(document->view),
                                          GTK_TEXT_WINDOW_WIDGET,
                                          (gint)x,
                                          (gint)y,
                                          &bx,
                                          &by);
    gtk_text_view_get_iter_at_position(GTK_TEXT_VIEW(document->view), &iter, NULL, bx, by);
    gtk_text_buffer_place_cursor(GTK_TEXT_BUFFER(document->buffer), &iter);

    menu_model = spelling_text_buffer_adapter_get_menu_model(document->spell_adapter);
    if (menu_model == NULL)
    {
        return;
    }

    popover = gtk_popover_menu_new_from_model(menu_model);
    rect = (GdkRectangle){ (gint)x, (gint)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_widget_set_parent(popover, document->view);
    g_object_ref(popover);
    g_signal_connect(popover, "closed", G_CALLBACK(mt_document_spelling_popover_closed), NULL);
    gtk_popover_popup(GTK_POPOVER(popover));
}

MtDocument *
mt_document_new(void)
{
    MtDocument *document;
    GtkSourceView *source_view;
    GtkEventController *key_controller;
    GtkGesture *click_gesture;

    document = g_new0(MtDocument, 1);
    document->buffer = gtk_source_buffer_new(NULL);
    document->source_file = gtk_source_file_new();
    document->display_name = g_strdup(_("Untitled"));
    document->draft_path = mt_document_make_draft_path();
    document->is_draft = TRUE;

    source_view = GTK_SOURCE_VIEW(gtk_source_view_new_with_buffer(document->buffer));
    document->view = GTK_WIDGET(source_view);

    gtk_source_view_set_show_line_numbers(source_view, TRUE);
    gtk_source_view_set_highlight_current_line(source_view, TRUE);
    gtk_source_view_set_auto_indent(source_view, TRUE);
    /* Tab 键插入真实 U+0009；显示宽度由用户偏好决定，绝不改写为若干空格。 */
    gtk_source_view_set_insert_spaces_instead_of_tabs(source_view, FALSE);
    gtk_source_view_set_indent_width(source_view, -1);
    gtk_source_view_set_tab_width(source_view, 4);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(source_view), TRUE);
    /* 与原版 GNOME Text Editor 一致的编辑区留白。 */
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(source_view), 12);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(source_view), 12);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(source_view), 12);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(source_view), 12);
    gtk_widget_set_vexpand(document->view, TRUE);
    gtk_widget_set_hexpand(document->view, TRUE);
    key_controller = gtk_event_controller_key_new();
    g_object_set_data(G_OBJECT(key_controller), "vellum-smart-pair-controller", GINT_TO_POINTER(1));
    g_signal_connect(key_controller,
                     "key-pressed",
                     G_CALLBACK(mt_document_key_pressed),
                     document);
    gtk_widget_add_controller(document->view, key_controller);
    click_gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(click_gesture,
                     "pressed",
                     G_CALLBACK(mt_document_context_menu_pressed),
                     document);
    gtk_widget_add_controller(document->view, GTK_EVENT_CONTROLLER(click_gesture));
    g_signal_connect(document->buffer,
                     "changed",
                     G_CALLBACK(mt_document_buffer_changed),
                     document);

    document->spell_checker = mt_document_create_spell_checker();
    document->spell_adapter = spelling_text_buffer_adapter_new(document->buffer,
                                                               document->spell_checker);
    gtk_widget_insert_action_group(document->view,
                                   "spelling",
                                   G_ACTION_GROUP(document->spell_adapter));

    return document;
}

void
mt_document_free(MtDocument *document)
{
    if (document == NULL)
    {
        return;
    }

    if (document->analysis_source_id != 0)
    {
        g_source_remove(document->analysis_source_id);
    }
    if (document->loader != NULL)
    {
        g_object_unref(document->loader);
    }

    if (document->file_monitor != NULL)
    {
        g_file_monitor_cancel(document->file_monitor);
    }
    if (document->inline_completion_label != NULL)
    {
        if (GTK_IS_TEXT_VIEW(document->view) &&
            gtk_widget_get_parent(document->inline_completion_label) == document->view)
        {
            gtk_text_view_remove(GTK_TEXT_VIEW(document->view),
                                 document->inline_completion_label);
        }
        else if (gtk_widget_get_parent(document->inline_completion_label) != NULL)
        {
            gtk_widget_unparent(document->inline_completion_label);
        }
        document->inline_completion_label = NULL;
    }
    g_clear_object(&document->file_monitor);
    g_clear_object(&document->save_target);
    g_clear_pointer(&document->save_contents, g_free);

    g_clear_object(&document->spell_adapter);
    g_clear_object(&document->spell_checker);
    g_clear_object(&document->buffer);
    g_clear_object(&document->source_file);
    g_free(document->display_name);
    g_free(document->draft_path);
    g_free(document);
}

GtkWidget *
mt_document_get_view(MtDocument *document)
{
    return document->view;
}

void
mt_document_apply_editor_settings(MtDocument *document, MtSettings *settings)
{
    GtkSourceView *view;

    view = GTK_SOURCE_VIEW(document->view);
    gtk_source_view_set_show_line_numbers(view,
                                          mt_settings_get_show_line_numbers(settings));
    gtk_source_view_set_highlight_current_line(view,
                                               mt_settings_get_highlight_current_line(settings));
    gtk_source_view_set_auto_indent(view, mt_settings_get_auto_indent(settings));
    gtk_source_view_set_insert_spaces_instead_of_tabs(view,
                                                      mt_settings_get_insert_spaces(settings));
    gtk_source_view_set_tab_width(view, mt_settings_get_tab_width(settings));
    gtk_source_view_set_indent_width(view, mt_settings_get_indent_width(settings));
    gtk_source_view_set_show_right_margin(view,
                                          mt_settings_get_show_right_margin(settings));
    gtk_source_view_set_right_margin_position(view,
                                              mt_settings_get_right_margin_position(settings));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),
                                mt_settings_get_word_wrap(settings) ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
    if (document->overview != NULL)
    {
        gtk_widget_set_visible(document->overview, mt_settings_get_show_overview(settings));
    }
    if (document->spell_adapter != NULL)
    {
        spelling_text_buffer_adapter_set_enabled(document->spell_adapter,
                                                 mt_settings_get_spell_check(settings));
    }
}

void
mt_document_set_tab_width(MtDocument *document, gint tab_width)
{
    GtkSourceView *view;

    view = GTK_SOURCE_VIEW(document->view);
    gtk_source_view_set_insert_spaces_instead_of_tabs(view, FALSE);
    gtk_source_view_set_indent_width(view, -1);
    gtk_source_view_set_tab_width(view, CLAMP(tab_width, 1, 16));
}

GtkSourceBuffer *
mt_document_get_buffer(MtDocument *document)
{
    return document->buffer;
}

GFile *
mt_document_get_file(MtDocument *document)
{
    return gtk_source_file_get_location(document->source_file);
}

const gchar *
mt_document_get_display_name(MtDocument *document)
{
    return document->display_name;
}

gboolean
mt_document_is_modified(MtDocument *document)
{
    return gtk_text_buffer_get_modified(GTK_TEXT_BUFFER(document->buffer));
}

gboolean
mt_document_is_untitled(MtDocument *document)
{
    return mt_document_get_file(document) == NULL;
}

gboolean
mt_document_is_saving(MtDocument *document)
{
    return document->saving;
}

void
mt_document_set_page(MtDocument *document, AdwTabPage *page)
{
    document->page = page;
}

void
mt_document_set_display_name(MtDocument *document, const gchar *name)
{
    g_free(document->display_name);
    document->display_name = g_strdup(name != NULL ? name : _("Untitled"));

    if (document->page != NULL)
    {
        adw_tab_page_set_title(document->page, document->display_name);
    }
}

void
mt_document_set_file(MtDocument *document, GFile *file)
{
    gchar *basename;

    gtk_source_file_set_location(document->source_file, file);
    document->is_draft = file == NULL;

    if (file == NULL)
    {
        mt_document_set_display_name(document, _("Untitled"));
        return;
    }

    basename = g_file_get_basename(file);
    mt_document_set_display_name(document, basename);
    g_free(basename);
}

static void
mt_document_apply_diagnostic_tag(GtkTextBuffer *buffer, gint start_offset, gint end_offset)
{
    GtkTextIter start;
    GtkTextIter end;

    gtk_text_buffer_get_iter_at_offset(buffer, &start, start_offset);
    gtk_text_buffer_get_iter_at_offset(buffer, &end, end_offset);
    gtk_text_buffer_apply_tag_by_name(buffer, MT_DIAGNOSTIC_TAG, &start, &end);
}

static gboolean
mt_document_brackets_match(gunichar opening, gunichar closing)
{
    return (opening == '(' && closing == ')') ||
           (opening == '[' && closing == ']') ||
           (opening == '{' && closing == '}');
}

static void
mt_document_apply_diagnostics(MtDocument *document)
{
    GtkTextBuffer *buffer;
    GtkTextTagTable *table;
    GtkTextTag *tag;
    GtkTextIter start;
    GtkTextIter end;
    gchar *contents;
    const gchar *cursor;
    GArray *openings;
    GArray *offsets;
    GdkRGBA color;
    gint offset;
    gboolean in_single_quote;
    gboolean in_double_quote;
    gboolean escaped;

    buffer = GTK_TEXT_BUFFER(document->buffer);
    gtk_text_buffer_get_bounds(buffer, &start, &end);

    table = gtk_text_buffer_get_tag_table(buffer);
    tag = gtk_text_tag_table_lookup(table, MT_DIAGNOSTIC_TAG);
    if (tag == NULL)
    {
        color.red = 0.88;
        color.green = 0.16;
        color.blue = 0.18;
        color.alpha = 1.0;
        tag = gtk_text_buffer_create_tag(buffer,
                                         MT_DIAGNOSTIC_TAG,
                                         "underline", PANGO_UNDERLINE_ERROR,
                                         "underline-rgba", &color,
                                         NULL);
    }

    gtk_text_buffer_remove_tag(buffer, tag, &start, &end);
    contents = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    openings = g_array_new(FALSE, FALSE, sizeof(gunichar));
    offsets = g_array_new(FALSE, FALSE, sizeof(gint));
    cursor = contents;
    offset = 0;
    in_single_quote = FALSE;
    in_double_quote = FALSE;
    escaped = FALSE;

    while (*cursor != '\0')
    {
        gunichar character;

        character = g_utf8_get_char(cursor);
        if (escaped)
        {
            escaped = FALSE;
        }
        else if (character == '\\' && (in_single_quote || in_double_quote))
        {
            escaped = TRUE;
        }
        else if (character == '\'' && !in_double_quote)
        {
            in_single_quote = !in_single_quote;
        }
        else if (character == '"' && !in_single_quote)
        {
            in_double_quote = !in_double_quote;
        }
        else if (!in_single_quote && !in_double_quote &&
                 (character == '(' || character == '[' || character == '{'))
        {
            g_array_append_val(openings, character);
            g_array_append_val(offsets, offset);
        }
        else if (!in_single_quote && !in_double_quote &&
                 (character == ')' || character == ']' || character == '}'))
        {
            if (openings->len == 0 ||
                !mt_document_brackets_match(g_array_index(openings, gunichar, openings->len - 1), character))
            {
                mt_document_apply_diagnostic_tag(buffer, offset, offset + 1);
            }
            else
            {
                g_array_set_size(openings, openings->len - 1);
                g_array_set_size(offsets, offsets->len - 1);
            }
        }

        cursor = g_utf8_next_char(cursor);
        offset++;
    }

    while (offsets->len > 0)
    {
        gint opening_offset;

        opening_offset = g_array_index(offsets, gint, offsets->len - 1);
        mt_document_apply_diagnostic_tag(buffer, opening_offset, opening_offset + 1);
        g_array_set_size(offsets, offsets->len - 1);
    }

    if (in_single_quote || in_double_quote)
    {
        mt_document_apply_diagnostic_tag(buffer, MAX(offset - 1, 0), offset);
    }

    g_array_unref(openings);
    g_array_unref(offsets);
    g_free(contents);
}

static const gchar *
mt_document_guess_content_type(MtDocument *document)
{
    GtkTextIter start;
    GtkTextIter end;
    gchar *contents;
    gchar *trimmed;
    const gchar *content_type;

    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(document->buffer), &start, &end);
    contents = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(document->buffer), &start, &end, FALSE);
    trimmed = g_strstrip(contents);
    content_type = NULL;

    if (g_str_has_prefix(trimmed, "#!"))
    {
        if (strstr(trimmed, "python") != NULL)
        {
            content_type = "text/x-python";
        }
        else if (strstr(trimmed, "bash") != NULL || strstr(trimmed, "/sh") != NULL)
        {
            content_type = "application/x-shellscript";
        }
        else if (strstr(trimmed, "perl") != NULL)
        {
            content_type = "text/x-perl";
        }
        else if (strstr(trimmed, "ruby") != NULL)
        {
            content_type = "text/x-ruby";
        }
        else if (strstr(trimmed, "node") != NULL || strstr(trimmed, "deno") != NULL)
        {
            content_type = "application/javascript";
        }
    }
    else if (g_str_has_prefix(trimmed, "<?xml"))
    {
        content_type = "application/xml";
    }
    else if (g_ascii_strncasecmp(trimmed, "<!doctype html", 14) == 0 ||
             g_ascii_strncasecmp(trimmed, "<html", 5) == 0)
    {
        content_type = "text/html";
    }
    else if (*trimmed == '{' || *trimmed == '[')
    {
        content_type = "application/json";
    }
    else if (g_str_has_prefix(trimmed, "---") && strstr(trimmed, ":") != NULL)
    {
        content_type = "text/x-yaml";
    }
    else if (strstr(trimmed, "std::") != NULL || strstr(trimmed, "#include <iostream>") != NULL)
    {
        content_type = "text/x-c++src";
    }
    else if (strstr(trimmed, "#include") != NULL || strstr(trimmed, "int main(") != NULL)
    {
        content_type = "text/x-csrc";
    }
    else if (g_str_has_prefix(trimmed, "def ") || g_str_has_prefix(trimmed, "import ") ||
             strstr(trimmed, "\ndef ") != NULL)
    {
        content_type = "text/x-python";
    }
    else if (strstr(trimmed, "fn main") != NULL || strstr(trimmed, "let mut ") != NULL)
    {
        content_type = "text/x-rust";
    }
    else if (g_str_has_prefix(trimmed, "package main") || strstr(trimmed, "\nfunc ") != NULL)
    {
        content_type = "text/x-go";
    }
    else if (strstr(trimmed, "public class ") != NULL || strstr(trimmed, "static void main") != NULL)
    {
        content_type = "text/x-java";
    }
    else if (g_str_has_prefix(trimmed, "SELECT ") || g_str_has_prefix(trimmed, "select "))
    {
        content_type = "text/x-sql";
    }

    g_free(contents);

    return content_type;
}

void
mt_document_guess_language(MtDocument *document)
{
    GtkSourceLanguageManager *manager;
    GtkSourceLanguage *language;
    GFile *file;
    gchar *basename;
    const gchar *content_type;

    file = mt_document_get_file(document);
    manager = gtk_source_language_manager_get_default();
    basename = file != NULL ? g_file_get_basename(file) : g_strdup("untitled");
    /* 同时结合文件名、shebang 和常见文本结构推断语言，不进行阻塞式文件查询。 */
    content_type = mt_document_guess_content_type(document);
    language = gtk_source_language_manager_guess_language(manager, basename, content_type);

    gtk_source_buffer_set_language(document->buffer, language);

    g_free(basename);
}

void
mt_document_schedule_analysis(MtDocument *document)
{
    if (document->analysis_source_id != 0)
    {
        g_source_remove(document->analysis_source_id);
    }

    document->analysis_source_id = g_timeout_add(MT_ANALYSIS_DELAY_MILLISECONDS,
                                                 mt_document_analysis_cb,
                                                 document);
}

void
mt_document_load_async(MtDocument *document,
                       GFile *file,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    g_clear_object(&document->loader);
    mt_document_set_file(document, file);
    document->loader = gtk_source_file_loader_new(document->buffer, document->source_file);

    gtk_source_file_loader_load_async(document->loader,
                                      G_PRIORITY_DEFAULT,
                                      NULL,
                                      NULL,
                                      NULL,
                                      NULL,
                                      callback,
                                      user_data);
}

void
mt_document_reload_with_encoding(MtDocument *document,
                                 const GtkSourceEncoding *encoding,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
    GFile *file;
    GSList *candidates;

    file = mt_document_get_file(document);
    if (file == NULL)
    {
        return;
    }

    g_clear_object(&document->loader);
    document->loader = gtk_source_file_loader_new(document->buffer, document->source_file);
    candidates = g_slist_append(NULL, (GtkSourceEncoding *)encoding);
    gtk_source_file_loader_set_candidate_encodings(document->loader, candidates);
    g_slist_free(candidates);
    gtk_source_file_loader_load_async(document->loader,
                                      G_PRIORITY_DEFAULT,
                                      NULL,
                                      NULL,
                                      NULL,
                                      NULL,
                                      callback,
                                      user_data);
}

gboolean
mt_document_load_finish(MtDocument *document,
                        GAsyncResult *result,
                        GError **error)
{
    gboolean success;

    success = gtk_source_file_loader_load_finish(document->loader, result, error);

    if (success)
    {
        mt_document_guess_language(document);
        mt_document_apply_diagnostics(document);
        gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(document->buffer), FALSE);
    }

    g_clear_object(&document->loader);

    return success;
}

void
mt_document_save_async(MtDocument *document,
                       GFile *target,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
    GtkTextIter start;
    GtkTextIter end;
    GFile *file;

    g_return_if_fail(!document->saving);

    file = target != NULL ? target : mt_document_get_file(document);
    g_return_if_fail(file != NULL);

    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(document->buffer), &start, &end);
    document->save_contents = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(document->buffer),
                                                        &start,
                                                        &end,
                                                        FALSE);
    document->save_target = g_object_ref(file);
    document->saving = TRUE;

    /* GIO 在工作线程执行写入；主循环只负责回调，不会等待文件系统。 */
    g_file_replace_contents_async(document->save_target,
                                  document->save_contents,
                                  strlen(document->save_contents),
                                  NULL,
                                  FALSE,
                                  G_FILE_CREATE_NONE,
                                  NULL,
                                  callback,
                                  user_data);
}

gboolean
mt_document_save_finish(MtDocument *document,
                        GAsyncResult *result,
                        GError **error)
{
    gboolean success;

    g_return_val_if_fail(document->saving, FALSE);
    g_return_val_if_fail(document->save_target != NULL, FALSE);

    success = g_file_replace_contents_finish(document->save_target,
                                             result,
                                             NULL,
                                             error);
    if (success)
    {
        mt_document_set_file(document, document->save_target);
        gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(document->buffer), FALSE);
        mt_document_remove_snapshot(document);
    }

    document->saving = FALSE;
    g_clear_object(&document->save_target);
    g_clear_pointer(&document->save_contents, g_free);

    return success;
}

void
mt_document_snapshot(MtDocument *document)
{
    GtkTextIter start;
    GtkTextIter end;
    gchar *contents;
    GError *error;

    if (!mt_document_is_modified(document))
    {
        return;
    }

    gtk_text_buffer_get_bounds(GTK_TEXT_BUFFER(document->buffer), &start, &end);
    contents = gtk_text_buffer_get_text(GTK_TEXT_BUFFER(document->buffer), &start, &end, FALSE);
    error = NULL;

    if (!g_file_set_contents(document->draft_path, contents, -1, &error))
    {
        g_warning("Unable to create draft snapshot: %s", error->message);
        g_clear_error(&error);
    }

    g_free(contents);
}

gboolean
mt_document_restore_snapshot(MtDocument *document,
                            const gchar *path,
                            GError **error)
{
    gchar *contents;
    gsize length;

    contents = NULL;
    length = 0;

    if (!g_file_get_contents(path, &contents, &length, error))
    {
        return FALSE;
    }

    gtk_text_buffer_set_text(GTK_TEXT_BUFFER(document->buffer), contents, (gint)length);
    gtk_text_buffer_set_modified(GTK_TEXT_BUFFER(document->buffer), TRUE);
    mt_document_set_display_name(document, _("Recovered draft"));
    document->is_draft = TRUE;

    g_free(contents);

    return TRUE;
}

void
mt_document_remove_snapshot(MtDocument *document)
{
    if (document->draft_path != NULL)
    {
        g_remove(document->draft_path);
    }
}

GPtrArray *
mt_document_list_snapshots(void)
{
    GPtrArray *snapshots;
    gchar *directory;
    GDir *dir;
    const gchar *name;

    snapshots = g_ptr_array_new_with_free_func(g_free);
    directory = g_build_filename(g_get_user_state_dir(), MT_DRAFT_DIRECTORY, NULL);
    dir = g_dir_open(directory, 0, NULL);

    if (dir != NULL)
    {
        while ((name = g_dir_read_name(dir)) != NULL)
        {
            g_ptr_array_add(snapshots, g_build_filename(directory, name, NULL));
        }

        g_dir_close(dir);
    }

    g_free(directory);

    return snapshots;
}
