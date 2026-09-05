/*
 * mt-vut-package-manifest.c
 * `.vut` 源码包的清单校验、构建与安装。
 */

#include "mt-vut-package-private.h"
#include "mt-plugin.h"
#include "mt-vut-package.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>

gboolean
mt_vut_validate_manifest(const gchar *contents,
                         gsize length,
                         gchar **module_name_out,
                         gchar **extension_id_out,
                         GError **error)
{
    GKeyFile *settings;
    gint format_version;
    gint plugin_api;
    gchar *module_name;
    gchar *extension_id;
    gchar *target_os;
    gchar *target_architecture;
    gchar *target_abi;
    gboolean valid;

    settings = g_key_file_new();
    if (!g_key_file_load_from_data(settings, contents, (gssize)length, G_KEY_FILE_NONE, error))
    {
        g_key_file_unref(settings);
        return FALSE;
    }

    format_version = g_key_file_get_integer(settings, "Vellum Extension", "format-version", error);
    if (error != NULL && *error != NULL)
    {
        g_key_file_unref(settings);
        return FALSE;
    }
    plugin_api = g_key_file_get_integer(settings, "Vellum Extension", "plugin-api", error);
    if (error != NULL && *error != NULL)
    {
        g_key_file_unref(settings);
        return FALSE;
    }

    extension_id = g_key_file_get_string(settings, "Vellum Extension", "id", error);
    module_name = g_key_file_get_string(settings, "Vellum Extension", "module", error);
    target_os = g_key_file_get_string(settings, "Target", "os", error);
    target_architecture = g_key_file_get_string(settings, "Target", "architecture", error);
    target_abi = g_key_file_get_string(settings, "Target", "abi", error);
    valid = error == NULL || *error == NULL;

    if (!valid || format_version != 1 || plugin_api != MT_PLUGIN_API_VERSION ||
        !mt_vut_is_safe_identifier(extension_id) || !mt_vut_is_safe_entry_name(module_name) ||
        !g_str_has_suffix(module_name, "." G_MODULE_SUFFIX))
    {
        if (valid)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "The .vut manifest has an invalid format, plugin API, id, or module name");
        }
        g_free(extension_id);
        g_free(module_name);
        g_free(target_os);
        g_free(target_architecture);
        g_free(target_abi);
        g_key_file_unref(settings);
        return FALSE;
    }

    if (g_strcmp0(target_os, mt_vut_package_get_host_os()) != 0 ||
        g_strcmp0(target_architecture, mt_vut_package_get_host_architecture()) != 0 ||
        g_strcmp0(target_abi, mt_vut_package_get_host_abi()) != 0)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                    "This .vut targets %s/%s/%s, but this Vellum build is %s/%s/%s",
                    target_os,
                    target_architecture,
                    target_abi,
                    mt_vut_package_get_host_os(),
                    mt_vut_package_get_host_architecture(),
                    mt_vut_package_get_host_abi());
        g_free(extension_id);
        g_free(module_name);
        g_free(target_os);
        g_free(target_architecture);
        g_free(target_abi);
        g_key_file_unref(settings);
        return FALSE;
    }

    g_free(target_os);
    g_free(target_architecture);
    g_free(target_abi);
    g_key_file_unref(settings);
    *module_name_out = module_name;
    *extension_id_out = extension_id;
    return TRUE;
}

gboolean
mt_vut_install_module(const gchar *destination_directory,
                      const gchar *extension_id,
                      const gchar *contents,
                      gsize length,
                      gchar **module_path_out,
                      GError **error)
{
    gchar *base_path;
    gchar *destination_path;

    g_mkdir_with_parents(destination_directory, 0700);
    base_path = g_build_filename(destination_directory, extension_id, NULL);
    destination_path = g_strconcat(base_path, "." G_MODULE_SUFFIX, NULL);
    g_free(base_path);

    if (!g_file_set_contents(destination_path, contents, (gssize)length, error))
    {
        g_free(destination_path);
        return FALSE;
    }
    g_chmod(destination_path, 0700);
    *module_path_out = destination_path;

    return TRUE;
}

void
mt_vut_remove_tree(const gchar *path)
{
    GDir *directory;
    const gchar *name;

    directory = g_dir_open(path, 0, NULL);
    if (directory != NULL)
    {
        while ((name = g_dir_read_name(directory)) != NULL)
        {
            gchar *child;

            child = g_build_filename(path, name, NULL);
            if (g_file_test(child, G_FILE_TEST_IS_DIR))
            {
                mt_vut_remove_tree(child);
            }
            else
            {
                g_remove(child);
            }
            g_free(child);
        }
        g_dir_close(directory);
    }
    g_rmdir(path);
}

gboolean
mt_vut_validate_build_arguments(const gchar *arguments, gchar ***arguments_out, GError **error)
{
    gchar **parsed;
    gint count;
    gint index;

    parsed = NULL;
    count = 0;
    if (arguments == NULL || *arguments == '\0')
    {
        *arguments_out = g_new0(gchar *, 1);
        return TRUE;
    }

    if (strpbrk(arguments, ";|&`$><\n\r") != NULL ||
        !g_shell_parse_argv(arguments, &count, &parsed, error))
    {
        if (error != NULL && *error == NULL)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "The source .vut build arguments contain unsupported shell control characters");
        }
        return FALSE;
    }

    for (index = 0; index < count; index++)
    {
        if (strpbrk(parsed[index], ";|&`$><\n\r") != NULL)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "The source .vut build arguments contain unsupported shell control characters");
            g_strfreev(parsed);
            return FALSE;
        }
    }

    *arguments_out = parsed;
    return TRUE;
}

gboolean
mt_vut_check_required_tools(GKeyFile *settings, GError **error)
{
    gchar **tools;
    gsize tool_count;
    guint index;
    gboolean available;

    tools = g_key_file_get_string_list(settings, "Source", "required-tools", &tool_count, error);
    if (tools == NULL)
    {
        return FALSE;
    }

    available = TRUE;
    for (index = 0; index < tool_count; index++)
    {
        gchar *tool_path;

        if (!mt_vut_is_safe_entry_name(tools[index]))
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "The source .vut declares an invalid build tool name");
            available = FALSE;
            break;
        }
        tool_path = g_find_program_in_path(tools[index]);
        if (tool_path == NULL)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "The source .vut requires the unavailable build tool '%s'", tools[index]);
            available = FALSE;
            break;
        }
        g_free(tool_path);
    }

    g_strfreev(tools);
    return available;
}

gboolean
mt_vut_extract_source_tree(const guchar *archive,
                           gsize archive_length,
                           GPtrArray *entries,
                           const gchar *source_root,
                           const gchar *temporary_directory,
                           GError **error)
{
    gchar *prefix;
    guint index;
    gboolean extracted;

    prefix = g_strconcat(source_root, "/", NULL);
    extracted = FALSE;
    for (index = 0; index < entries->len; index++)
    {
        MtVutEntry *entry;

        entry = g_ptr_array_index(entries, index);
        if (g_str_has_prefix(entry->name, prefix))
        {
            gchar *contents;
            gsize length;
            gchar *destination;
            gchar *parent;

            contents = NULL;
            length = 0;
            if (!mt_vut_extract_entry(archive, archive_length, entry, &contents, &length, error))
            {
                g_free(prefix);
                return FALSE;
            }
            destination = g_build_filename(temporary_directory, entry->name, NULL);
            parent = g_path_get_dirname(destination);
            g_mkdir_with_parents(parent, 0700);
            if (!g_file_set_contents(destination, contents, (gssize)length, error))
            {
                g_free(parent);
                g_free(destination);
                g_free(contents);
                g_free(prefix);
                return FALSE;
            }
            g_chmod(destination, 0600);
            g_free(parent);
            g_free(destination);
            g_free(contents);
            extracted = TRUE;
        }
    }
    g_free(prefix);

    if (!extracted)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    "The source .vut does not contain the declared source-root");
    }

    return extracted;
}

gboolean
mt_vut_import_source_package(const guchar *archive,
                             gsize archive_length,
                             GPtrArray *entries,
                             const gchar *manifest_contents,
                             gsize manifest_length,
                             const gchar *destination_directory,
                             gchar **module_path_out,
                             gchar **extension_id_out,
                             GError **error)
{
    GKeyFile *settings;
    gint format_version;
    gint plugin_api;
    gchar *extension_id;
    gchar *payload;
    gchar *target_os;
    gchar *target_architecture;
    gchar *target_abi;
    gchar *source_root;
    gchar *build_tool;
    gchar *build_arguments;
    gchar *output_module;
    gchar *tool_path;
    gchar **parsed_arguments;
    gchar **command;
    gint command_count;
    gchar *temporary_directory;
    gchar *source_directory;
    gchar *output_path;
    GSubprocessLauncher *launcher;
    GSubprocess *process;
    gchar *stdout_text;
    gchar *stderr_text;
    gboolean completed;
    gboolean success;
    gchar *module_contents;
    gsize module_length;
    guint index;

    settings = g_key_file_new();
    extension_id = NULL;
    payload = NULL;
    target_os = NULL;
    target_architecture = NULL;
    target_abi = NULL;
    source_root = NULL;
    build_tool = NULL;
    build_arguments = NULL;
    output_module = NULL;
    tool_path = NULL;
    parsed_arguments = NULL;
    command = NULL;
    temporary_directory = NULL;
    source_directory = NULL;
    output_path = NULL;
    launcher = NULL;
    process = NULL;
    stdout_text = NULL;
    stderr_text = NULL;
    module_contents = NULL;
    module_length = 0;

    if (!g_key_file_load_from_data(settings, manifest_contents, (gssize)manifest_length, G_KEY_FILE_NONE, error))
    {
        goto failed;
    }
    format_version = g_key_file_get_integer(settings, "Vellum Extension", "format-version", error);
    plugin_api = g_key_file_get_integer(settings, "Vellum Extension", "plugin-api", error);
    extension_id = g_key_file_get_string(settings, "Vellum Extension", "id", error);
    payload = g_key_file_get_string(settings, "Vellum Extension", "payload", error);
    target_os = g_key_file_get_string(settings, "Target", "os", error);
    target_architecture = g_key_file_get_string(settings, "Target", "architecture", error);
    target_abi = g_key_file_get_string(settings, "Target", "abi", error);
    source_root = g_key_file_get_string(settings, "Source", "source-root", error);
    build_tool = g_key_file_get_string(settings, "Source", "build-tool", error);
    build_arguments = g_key_file_get_string(settings, "Source", "build-arguments", NULL);
    output_module = g_key_file_get_string(settings, "Source", "output-module", error);
    if (error != NULL && *error != NULL)
    {
        goto failed;
    }

    if (format_version != 1 || plugin_api != MT_PLUGIN_API_VERSION ||
        g_strcmp0(payload, "source") != 0 || !mt_vut_is_safe_identifier(extension_id) ||
        !mt_vut_is_safe_archive_name(source_root) || !mt_vut_is_safe_entry_name(build_tool) ||
        !mt_vut_is_safe_archive_name(output_module) ||
        !g_str_has_suffix(output_module, "." G_MODULE_SUFFIX) ||
        (g_strcmp0(build_tool, "make") != 0 && g_strcmp0(build_tool, "cc") != 0 &&
         g_strcmp0(build_tool, "gcc") != 0))
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "The source .vut manifest has invalid build metadata");
        goto failed;
    }
    if (g_strcmp0(target_os, mt_vut_package_get_host_os()) != 0 ||
        (g_strcmp0(target_architecture, "any") != 0 &&
         g_strcmp0(target_architecture, mt_vut_package_get_host_architecture()) != 0) ||
        (g_strcmp0(target_abi, "any") != 0 &&
         g_strcmp0(target_abi, mt_vut_package_get_host_abi()) != 0))
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                    "This source .vut targets %s/%s/%s, but this Vellum build is %s/%s/%s",
                    target_os, target_architecture, target_abi,
                    mt_vut_package_get_host_os(), mt_vut_package_get_host_architecture(),
                    mt_vut_package_get_host_abi());
        goto failed;
    }
    if (!mt_vut_check_required_tools(settings, error) ||
        !mt_vut_validate_build_arguments(build_arguments, &parsed_arguments, error))
    {
        goto failed;
    }

    tool_path = g_find_program_in_path(build_tool);
    if (tool_path == NULL)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    "The source .vut requires the unavailable build tool '%s'", build_tool);
        goto failed;
    }
    temporary_directory = g_dir_make_tmp("vellum-vut-source-XXXXXX", error);
    if (temporary_directory == NULL ||
        !mt_vut_extract_source_tree(archive, archive_length, entries, source_root, temporary_directory, error))
    {
        goto failed;
    }

    source_directory = g_build_filename(temporary_directory, source_root, NULL);
    command_count = g_strv_length(parsed_arguments) + 1;
    command = g_new0(gchar *, command_count + 1);
    command[0] = tool_path;
    for (index = 0; index < (guint)(command_count - 1); index++)
    {
        command[index + 1] = parsed_arguments[index];
    }
    launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                         G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_set_cwd(launcher, source_directory);
    process = g_subprocess_launcher_spawnv(launcher, (const gchar * const *)command, error);
    if (process == NULL)
    {
        goto failed;
    }
    completed = g_subprocess_communicate_utf8(process,
                                               NULL,
                                               NULL,
                                               &stdout_text,
                                               &stderr_text,
                                               error);
    success = completed && g_subprocess_get_successful(process);
    if (!success)
    {
        if (completed)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Source .vut build exited with status %d: %.4096s",
                        g_subprocess_get_exit_status(process),
                        stderr_text != NULL ? stderr_text : "");
        }
        goto failed;
    }

    output_path = g_build_filename(source_directory, output_module, NULL);
    if (!g_file_test(output_path, G_FILE_TEST_IS_REGULAR) ||
        !g_file_get_contents(output_path, &module_contents, &module_length, error))
    {
        if (module_contents == NULL && (error == NULL || *error == NULL))
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "The source .vut build did not produce its declared module");
        }
        goto failed;
    }
    if (!mt_vut_install_module(destination_directory,
                               extension_id,
                               module_contents,
                               module_length,
                               module_path_out,
                               error))
    {
        goto failed;
    }

    *extension_id_out = g_steal_pointer(&extension_id);
    g_free(module_contents);
    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_object(&process);
    g_clear_object(&launcher);
    g_free(command);
    g_strfreev(parsed_arguments);
    g_free(tool_path);
    g_free(output_path);
    g_free(source_directory);
    mt_vut_remove_tree(temporary_directory);
    g_free(temporary_directory);
    g_free(payload);
    g_free(target_os);
    g_free(target_architecture);
    g_free(target_abi);
    g_free(source_root);
    g_free(build_tool);
    g_free(build_arguments);
    g_free(output_module);
    g_key_file_unref(settings);
    return TRUE;

failed:
    g_free(module_contents);
    g_free(stdout_text);
    g_free(stderr_text);
    g_clear_object(&process);
    g_clear_object(&launcher);
    g_free(command);
    g_strfreev(parsed_arguments);
    g_free(tool_path);
    g_free(output_path);
    g_free(source_directory);
    if (temporary_directory != NULL)
    {
        mt_vut_remove_tree(temporary_directory);
    }
    g_free(temporary_directory);
    g_free(extension_id);
    g_free(payload);
    g_free(target_os);
    g_free(target_architecture);
    g_free(target_abi);
    g_free(source_root);
    g_free(build_tool);
    g_free(build_arguments);
    g_free(output_module);
    g_key_file_unref(settings);
    return FALSE;
}