/*
 * main.c
 * Vellum 的进程入口：先加载本地偏好，再初始化 gettext 与 GTK 应用。
 */

#include "mt-application.h"

#include <glib/gi18n.h>
#include <locale.h>

/*
 * 未安装到系统时，翻译目录的解析顺序：
 * 1. VELLUM_LOCALEDIR 环境变量（AppImage 启动器也用它）；
 * 2. 可执行文件相对位置推导的构建树 po 目录（build/src/vellum -> build/po）。
 * 解析不到时返回 NULL，由调用方回退到编译期 LOCALEDIR。
 */
static gchar *
mt_resolve_locale_directory(const gchar *language)
{
    const gchar *env_dir;
    const gchar *source;
    gchar *lang;
    gchar *exe_path;
    gchar *exe_dir;
    gchar *build_po;
    gchar *catalog;
    gchar *resolved;
    gchar *separator;

    env_dir = g_getenv("VELLUM_LOCALEDIR");
    if (env_dir != NULL && g_file_test(env_dir, G_FILE_TEST_IS_DIR))
        return g_strdup(env_dir);

    source = language;
    if (g_strcmp0(language, "system") == 0)
    {
        source = g_getenv("LANGUAGE");
        if (source == NULL || *source == '\0')
            source = g_getenv("LANG");
        if (source == NULL || *source == '\0')
            return NULL;
    }

    lang = g_strdup(source);
    separator = strpbrk(lang, ":@.");
    if (separator != NULL)
        *separator = '\0';

    exe_path = g_file_read_link("/proc/self/exe", NULL);
    if (exe_path == NULL)
    {
        g_free(lang);
        return NULL;
    }

    exe_dir = g_path_get_dirname(exe_path);
    build_po = g_build_filename(exe_dir, "..", "po", NULL);
    catalog = g_build_filename(build_po, lang, "LC_MESSAGES",
                               GETTEXT_PACKAGE ".mo", NULL);
    if (g_file_test(catalog, G_FILE_TEST_IS_REGULAR))
        resolved = g_strdup(build_po);
    else
        resolved = NULL;

    g_free(exe_path);
    g_free(exe_dir);
    g_free(build_po);
    g_free(catalog);
    g_free(lang);

    return resolved;
}

int
main(int argc, char **argv)
{
    MtSettings *settings;
    MtApplication *application;
    const gchar *language;
    gchar *locale_directory;
    int status;

    settings = mt_settings_new();
    language = mt_settings_get_language(settings);

    if (g_strcmp0(language, "zh_CN") == 0)
    {
        g_setenv("LANGUAGE", "zh_CN", TRUE);
        g_unsetenv("LC_ALL");
    }
    else if (g_strcmp0(language, "en") == 0)
    {
        g_setenv("LANGUAGE", "en", TRUE);
        g_unsetenv("LC_ALL");
    }

    /*
     * LC_ALL 若被设置为 C/C.UTF-8，GNU gettext 会忽略 LANGUAGE，
     * 中文翻译将完全不生效；显式覆盖语言时先取消 LC_ALL，让 LANG 生效。
     */
    if (setlocale(LC_ALL, "") == NULL && g_strcmp0(language, "zh_CN") == 0)
    {
        if (setlocale(LC_ALL, "zh_CN.UTF-8") == NULL)
            setlocale(LC_ALL, "zh_CN");
    }

    locale_directory = mt_resolve_locale_directory(language);
    bindtextdomain(GETTEXT_PACKAGE,
                   locale_directory != NULL ? locale_directory : LOCALEDIR);
    g_free(locale_directory);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    adw_init();
    mt_settings_apply_appearance(settings);

    application = mt_application_new(settings);
    status = mt_application_run(application, argc, argv);

    mt_application_free(application);
    mt_settings_save(settings);
    mt_settings_free(settings);

    return status;
}
