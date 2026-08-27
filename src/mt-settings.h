/*
 * mt-settings.h
 * 不依赖 GSettings schema 的用户偏好存储，便于 AppImage 和可移动安装使用。
 */

#ifndef MT_SETTINGS_H
#define MT_SETTINGS_H

#include <adwaita.h>

G_BEGIN_DECLS

typedef enum
{
    MT_APPEARANCE_SYSTEM,
    MT_APPEARANCE_LIGHT,
    MT_APPEARANCE_DARK
} MtAppearance;

typedef struct _MtSettings MtSettings;

struct _MtSettings
{
    gchar *path;
    MtAppearance appearance;
    gchar *language;
    gchar *font_family;
    gchar *style_scheme;
    gint tab_width;
    gint indent_width;
    gint right_margin_position;
    gboolean show_line_numbers;
    gboolean highlight_current_line;
    gboolean show_overview;
    gboolean spell_check;
    gboolean show_right_margin;
    gboolean word_wrap;
    gboolean auto_indent;
    gboolean restore_session;
    /* 关闭时仅运行核心编辑器；重新启动后生效。 */
    gboolean extensions_enabled;
    gboolean custom_font;
    gboolean insert_spaces;
    /* 启动后自动向 GitHub 发布页查询新版本，发现新版时用 Toast 提醒。 */
    gboolean auto_check_updates;
    gdouble font_scale;
    GHashTable *shortcuts;
};

MtSettings *mt_settings_new(void);
void mt_settings_free(MtSettings *settings);
void mt_settings_save(MtSettings *settings);

MtAppearance mt_settings_get_appearance(MtSettings *settings);
void mt_settings_set_appearance(MtSettings *settings, MtAppearance appearance);
const gchar *mt_settings_get_language(MtSettings *settings);
void mt_settings_set_language(MtSettings *settings, const gchar *language);
const gchar *mt_settings_get_font_family(MtSettings *settings);
void mt_settings_set_font_family(MtSettings *settings, const gchar *family);
const gchar *mt_settings_get_style_scheme(MtSettings *settings);
void mt_settings_set_style_scheme(MtSettings *settings, const gchar *style_scheme);
gint mt_settings_get_tab_width(MtSettings *settings);
void mt_settings_set_tab_width(MtSettings *settings, gint tab_width);
gint mt_settings_get_indent_width(MtSettings *settings);
void mt_settings_set_indent_width(MtSettings *settings, gint indent_width);
gint mt_settings_get_right_margin_position(MtSettings *settings);
void mt_settings_set_right_margin_position(MtSettings *settings, gint position);
gboolean mt_settings_get_show_line_numbers(MtSettings *settings);
void mt_settings_set_show_line_numbers(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_highlight_current_line(MtSettings *settings);
void mt_settings_set_highlight_current_line(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_show_overview(MtSettings *settings);
void mt_settings_set_show_overview(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_spell_check(MtSettings *settings);
void mt_settings_set_spell_check(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_show_right_margin(MtSettings *settings);
void mt_settings_set_show_right_margin(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_word_wrap(MtSettings *settings);
void mt_settings_set_word_wrap(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_auto_indent(MtSettings *settings);
void mt_settings_set_auto_indent(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_restore_session(MtSettings *settings);
void mt_settings_set_restore_session(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_extensions_enabled(MtSettings *settings);
void mt_settings_set_extensions_enabled(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_auto_check_updates(MtSettings *settings);
void mt_settings_set_auto_check_updates(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_custom_font(MtSettings *settings);
void mt_settings_set_custom_font(MtSettings *settings, gboolean enabled);
gboolean mt_settings_get_insert_spaces(MtSettings *settings);
void mt_settings_set_insert_spaces(MtSettings *settings, gboolean enabled);
gdouble mt_settings_get_font_scale(MtSettings *settings);
void mt_settings_set_font_scale(MtSettings *settings, gdouble scale);
const gchar *mt_settings_get_shortcut(MtSettings *settings, const gchar *detailed_action_name);
void mt_settings_set_shortcut(MtSettings *settings,
                              const gchar *detailed_action_name,
                              const gchar *accelerator);
void mt_settings_apply_appearance(MtSettings *settings);

G_END_DECLS

#endif
