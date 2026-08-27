/*
 * mt-settings.c
 * 使用 XDG 配置目录中的 INI 文件保存偏好，避免安装额外 schema 的负担。
 */

#include "mt-settings.h"

#define MT_SETTINGS_DIRECTORY "vellum"
#define MT_SETTINGS_FILENAME "settings.ini"

static gboolean
mt_settings_style_scheme_is_valid(const gchar *style_scheme)
{
    static const gchar * const values[] = {
        "auto",
        "adwaita",
        "classic",
        "cobalt",
        "kate",
        "oblivion",
        "solarized",
        "tango",
        NULL
    };
    guint index;

    for (index = 0; values[index] != NULL; index++)
    {
        if (g_strcmp0(style_scheme, values[index]) == 0)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static gchar *
mt_settings_get_path(void)
{
    gchar *directory;
    gchar *path;

    directory = g_build_filename(g_get_user_config_dir(), MT_SETTINGS_DIRECTORY, NULL);
    g_mkdir_with_parents(directory, 0700);
    path = g_build_filename(directory, MT_SETTINGS_FILENAME, NULL);
    g_free(directory);

    return path;
}

static void
mt_settings_save_shortcut(gpointer key, gpointer value, gpointer user_data)
{
    g_key_file_set_string(user_data, "Shortcuts", key, value);
}

static gboolean
mt_settings_get_boolean_or_default(GKeyFile *key_file,
                                   const gchar *group,
                                   const gchar *key,
                                   gboolean fallback)
{
    if (!g_key_file_has_key(key_file, group, key, NULL))
    {
        return fallback;
    }

    return g_key_file_get_boolean(key_file, group, key, NULL);
}

MtSettings *
mt_settings_new(void)
{
    MtSettings *settings;
    GKeyFile *key_file;
    GError *error;
    gint appearance;
    gint tab_width;
    gdouble font_scale;
    gchar *language;
    gchar *font_family;
    gchar *style_scheme;

    settings = g_new0(MtSettings, 1);
    settings->path = mt_settings_get_path();
    settings->appearance = MT_APPEARANCE_SYSTEM;
    settings->language = g_strdup("system");
    settings->font_family = g_strdup("Monospace");
    settings->style_scheme = g_strdup("auto");
    settings->tab_width = 4;
    settings->indent_width = 4;
    settings->right_margin_position = 80;
    settings->show_line_numbers = TRUE;
    settings->highlight_current_line = TRUE;
    settings->show_overview = FALSE;
    settings->spell_check = FALSE;
    settings->show_right_margin = FALSE;
    settings->word_wrap = TRUE;
    settings->auto_indent = TRUE;
    settings->auto_pair_brackets = TRUE;
    settings->restore_session = TRUE;
    settings->extensions_enabled = TRUE;
    settings->auto_check_updates = TRUE;
    settings->custom_font = FALSE;
    settings->insert_spaces = FALSE;
    settings->font_scale = 1.0;
    settings->shortcuts = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);

    key_file = g_key_file_new();
    error = NULL;

    if (!g_key_file_load_from_file(key_file, settings->path, G_KEY_FILE_NONE, &error))
    {
        g_clear_error(&error);
        g_key_file_unref(key_file);
        return settings;
    }

    appearance = g_key_file_get_integer(key_file, "Interface", "appearance", NULL);
    if (appearance >= MT_APPEARANCE_SYSTEM && appearance <= MT_APPEARANCE_DARK)
    {
        settings->appearance = (MtAppearance)appearance;
    }

    language = g_key_file_get_string(key_file, "Interface", "language", NULL);
    if (language != NULL)
    {
        g_free(settings->language);
        settings->language = language;
    }

    font_family = g_key_file_get_string(key_file, "Editor", "font-family", NULL);
    if (font_family != NULL && *font_family != '\0')
    {
        g_free(settings->font_family);
        settings->font_family = font_family;
    }
    else
    {
        g_free(font_family);
    }

    style_scheme = g_key_file_get_string(key_file, "Editor", "style-scheme", NULL);
    if (style_scheme != NULL && mt_settings_style_scheme_is_valid(style_scheme))
    {
        g_free(settings->style_scheme);
        settings->style_scheme = style_scheme;
    }
    else
    {
        g_free(style_scheme);
    }

    tab_width = g_key_file_get_integer(key_file, "Editor", "tab-width", NULL);
    if (tab_width >= 1 && tab_width <= 16)
    {
        settings->tab_width = tab_width;
    }

    font_scale = g_key_file_get_double(key_file, "Editor", "font-scale", NULL);
    if (font_scale >= 0.75 && font_scale <= 2.0)
    {
        settings->font_scale = font_scale;
    }

    if (g_key_file_has_key(key_file, "Editor", "indent-width", NULL))
    {
        settings->indent_width = CLAMP(g_key_file_get_integer(key_file, "Editor", "indent-width", NULL), 1, 16);
    }
    if (g_key_file_has_key(key_file, "Editor", "right-margin-position", NULL))
    {
        settings->right_margin_position = CLAMP(g_key_file_get_integer(key_file, "Editor", "right-margin-position", NULL), 40, 200);
    }
    settings->show_line_numbers = mt_settings_get_boolean_or_default(key_file, "Editor", "show-line-numbers", settings->show_line_numbers);
    settings->highlight_current_line = mt_settings_get_boolean_or_default(key_file, "Editor", "highlight-current-line", settings->highlight_current_line);
    settings->show_overview = mt_settings_get_boolean_or_default(key_file, "Editor", "show-overview", settings->show_overview);
    settings->spell_check = mt_settings_get_boolean_or_default(key_file, "Editor", "spell-check", settings->spell_check);
    settings->show_right_margin = mt_settings_get_boolean_or_default(key_file, "Editor", "show-right-margin", settings->show_right_margin);
    settings->word_wrap = mt_settings_get_boolean_or_default(key_file, "Editor", "word-wrap", settings->word_wrap);
    settings->auto_indent = mt_settings_get_boolean_or_default(key_file, "Editor", "auto-indent", settings->auto_indent);
    settings->auto_pair_brackets = mt_settings_get_boolean_or_default(key_file, "Editor", "auto-pair-brackets", settings->auto_pair_brackets);
    settings->restore_session = mt_settings_get_boolean_or_default(key_file, "Editor", "restore-session", settings->restore_session);
    settings->extensions_enabled = mt_settings_get_boolean_or_default(key_file, "Interface", "extensions-enabled", settings->extensions_enabled);
    settings->auto_check_updates = mt_settings_get_boolean_or_default(key_file, "Interface", "auto-check-updates", settings->auto_check_updates);
    settings->custom_font = mt_settings_get_boolean_or_default(key_file, "Editor", "custom-font", settings->custom_font);
    settings->insert_spaces = mt_settings_get_boolean_or_default(key_file, "Editor", "insert-spaces", settings->insert_spaces);

    if (g_key_file_has_group(key_file, "Shortcuts"))
    {
        gsize shortcut_count;
        gchar **shortcut_names;
        gsize index;

        shortcut_names = g_key_file_get_keys(key_file, "Shortcuts", &shortcut_count, NULL);
        for (index = 0; shortcut_names != NULL && index < shortcut_count; index++)
        {
            gchar *accelerator;

            accelerator = g_key_file_get_string(key_file, "Shortcuts", shortcut_names[index], NULL);
            if (accelerator != NULL && gtk_accelerator_parse(accelerator, NULL, NULL))
            {
                g_hash_table_replace(settings->shortcuts,
                                     g_strdup(shortcut_names[index]),
                                     accelerator);
            }
            else
            {
                g_free(accelerator);
            }
        }
        g_strfreev(shortcut_names);
    }

    g_key_file_unref(key_file);

    return settings;
}

void
mt_settings_free(MtSettings *settings)
{
    if (settings == NULL)
    {
        return;
    }

    g_free(settings->path);
    g_free(settings->language);
    g_free(settings->font_family);
    g_free(settings->style_scheme);
    g_hash_table_unref(settings->shortcuts);
    g_free(settings);
}

void
mt_settings_save(MtSettings *settings)
{
    GKeyFile *key_file;
    gchar *contents;
    gsize length;
    GError *error;

    key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "Interface", "appearance", settings->appearance);
    g_key_file_set_string(key_file, "Interface", "language", settings->language);
    g_key_file_set_boolean(key_file, "Interface", "extensions-enabled", settings->extensions_enabled);
    g_key_file_set_boolean(key_file, "Interface", "auto-check-updates", settings->auto_check_updates);
    g_key_file_set_string(key_file, "Editor", "font-family", settings->font_family);
    g_key_file_set_string(key_file, "Editor", "style-scheme", settings->style_scheme);
    g_key_file_set_integer(key_file, "Editor", "tab-width", settings->tab_width);
    g_key_file_set_integer(key_file, "Editor", "indent-width", settings->indent_width);
    g_key_file_set_integer(key_file, "Editor", "right-margin-position", settings->right_margin_position);
    g_key_file_set_boolean(key_file, "Editor", "show-line-numbers", settings->show_line_numbers);
    g_key_file_set_boolean(key_file, "Editor", "highlight-current-line", settings->highlight_current_line);
    g_key_file_set_boolean(key_file, "Editor", "show-overview", settings->show_overview);
    g_key_file_set_boolean(key_file, "Editor", "spell-check", settings->spell_check);
    g_key_file_set_boolean(key_file, "Editor", "show-right-margin", settings->show_right_margin);
    g_key_file_set_boolean(key_file, "Editor", "word-wrap", settings->word_wrap);
    g_key_file_set_boolean(key_file, "Editor", "auto-indent", settings->auto_indent);
    g_key_file_set_boolean(key_file, "Editor", "auto-pair-brackets", settings->auto_pair_brackets);
    g_key_file_set_boolean(key_file, "Editor", "restore-session", settings->restore_session);
    g_key_file_set_boolean(key_file, "Editor", "custom-font", settings->custom_font);
    g_key_file_set_boolean(key_file, "Editor", "insert-spaces", settings->insert_spaces);
    g_key_file_set_double(key_file, "Editor", "font-scale", settings->font_scale);
    g_hash_table_foreach(settings->shortcuts, mt_settings_save_shortcut, key_file);

    error = NULL;
    contents = g_key_file_to_data(key_file, &length, &error);

    if (contents == NULL)
    {
        g_warning("Unable to serialize settings: %s", error->message);
        g_clear_error(&error);
        g_key_file_unref(key_file);
        return;
    }

    if (!g_file_set_contents(settings->path, contents, (gssize)length, &error))
    {
        g_warning("Unable to save settings: %s", error->message);
        g_clear_error(&error);
    }

    g_free(contents);
    g_key_file_unref(key_file);
}

MtAppearance
mt_settings_get_appearance(MtSettings *settings)
{
    return settings->appearance;
}

void
mt_settings_set_appearance(MtSettings *settings, MtAppearance appearance)
{
    settings->appearance = appearance;
}

const gchar *
mt_settings_get_language(MtSettings *settings)
{
    return settings->language;
}

void
mt_settings_set_language(MtSettings *settings, const gchar *language)
{
    g_free(settings->language);
    settings->language = g_strdup(language != NULL ? language : "system");
}

const gchar *
mt_settings_get_font_family(MtSettings *settings)
{
    return settings->font_family;
}

void
mt_settings_set_font_family(MtSettings *settings, const gchar *family)
{
    g_free(settings->font_family);
    settings->font_family = g_strdup(family != NULL && *family != '\0' ? family : "Monospace");
}

const gchar *
mt_settings_get_style_scheme(MtSettings *settings)
{
    return settings->style_scheme;
}

void
mt_settings_set_style_scheme(MtSettings *settings, const gchar *style_scheme)
{
    g_free(settings->style_scheme);
    settings->style_scheme = g_strdup(mt_settings_style_scheme_is_valid(style_scheme) ? style_scheme : "auto");
}

gint
mt_settings_get_tab_width(MtSettings *settings)
{
    return settings->tab_width;
}

void
mt_settings_set_tab_width(MtSettings *settings, gint tab_width)
{
    settings->tab_width = CLAMP(tab_width, 1, 16);
}

gdouble
mt_settings_get_font_scale(MtSettings *settings)
{
    return settings->font_scale;
}

void
mt_settings_set_font_scale(MtSettings *settings, gdouble scale)
{
    settings->font_scale = CLAMP(scale, 0.75, 2.0);
}

const gchar *
mt_settings_get_shortcut(MtSettings *settings, const gchar *detailed_action_name)
{
    return detailed_action_name != NULL ?
           g_hash_table_lookup(settings->shortcuts, detailed_action_name) : NULL;
}

void
mt_settings_set_shortcut(MtSettings *settings,
                         const gchar *detailed_action_name,
                         const gchar *accelerator)
{
    if (detailed_action_name == NULL || accelerator == NULL || *accelerator == '\0')
    {
        return;
    }

    g_hash_table_replace(settings->shortcuts,
                         g_strdup(detailed_action_name),
                         g_strdup(accelerator));
}

gint
mt_settings_get_indent_width(MtSettings *settings)
{
    return settings->indent_width;
}

void
mt_settings_set_indent_width(MtSettings *settings, gint indent_width)
{
    settings->indent_width = CLAMP(indent_width, 1, 16);
}

gint
mt_settings_get_right_margin_position(MtSettings *settings)
{
    return settings->right_margin_position;
}

void
mt_settings_set_right_margin_position(MtSettings *settings, gint position)
{
    settings->right_margin_position = CLAMP(position, 40, 200);
}

#define MT_SETTINGS_BOOLEAN_ACCESSORS(name, field) \
gboolean mt_settings_get_##name(MtSettings *settings) { return settings->field; } \
void mt_settings_set_##name(MtSettings *settings, gboolean enabled) { settings->field = !!enabled; }

MT_SETTINGS_BOOLEAN_ACCESSORS(show_line_numbers, show_line_numbers)
MT_SETTINGS_BOOLEAN_ACCESSORS(highlight_current_line, highlight_current_line)
MT_SETTINGS_BOOLEAN_ACCESSORS(show_overview, show_overview)
MT_SETTINGS_BOOLEAN_ACCESSORS(spell_check, spell_check)
MT_SETTINGS_BOOLEAN_ACCESSORS(show_right_margin, show_right_margin)
MT_SETTINGS_BOOLEAN_ACCESSORS(word_wrap, word_wrap)
MT_SETTINGS_BOOLEAN_ACCESSORS(auto_indent, auto_indent)
MT_SETTINGS_BOOLEAN_ACCESSORS(auto_pair_brackets, auto_pair_brackets)
MT_SETTINGS_BOOLEAN_ACCESSORS(restore_session, restore_session)
MT_SETTINGS_BOOLEAN_ACCESSORS(extensions_enabled, extensions_enabled)
MT_SETTINGS_BOOLEAN_ACCESSORS(auto_check_updates, auto_check_updates)
MT_SETTINGS_BOOLEAN_ACCESSORS(custom_font, custom_font)
MT_SETTINGS_BOOLEAN_ACCESSORS(insert_spaces, insert_spaces)

#undef MT_SETTINGS_BOOLEAN_ACCESSORS

void
mt_settings_apply_appearance(MtSettings *settings)
{
    AdwStyleManager *manager;

    manager = adw_style_manager_get_default();

    switch (settings->appearance)
    {
    case MT_APPEARANCE_LIGHT:
        adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_FORCE_LIGHT);
        break;

    case MT_APPEARANCE_DARK:
        adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_FORCE_DARK);
        break;

    case MT_APPEARANCE_SYSTEM:
    default:
        adw_style_manager_set_color_scheme(manager, ADW_COLOR_SCHEME_DEFAULT);
        break;
    }
}
