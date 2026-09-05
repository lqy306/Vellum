/*
 * mt-vut-package.c
 * `.vut` 是受限 ZIP 容器：仅支持小型、非加密的 Store/Deflate 文件和根目录清单。
 */

#include "mt-vut-package.h"
#include "mt-vut-package-private.h"

#include <glib/gstdio.h>
#include <string.h>
#include <zlib.h>

const gchar *
mt_vut_package_get_host_os(void)
{
#if defined(_WIN32)
    return "windows";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__NetBSD__)
    return "netbsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

const gchar *
mt_vut_package_get_host_architecture(void)
{
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__arm__)
    return "armv7";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

const gchar *
mt_vut_package_get_host_abi(void)
{
#if defined(__linux__) && defined(__GLIBC__)
    return "gnu";
#elif defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "msvc";
#else
    return "native";
#endif
}

static void
mt_vut_append_file(GByteArray *archive,
                   GPtrArray *central_entries,
                   const gchar *name,
                   const gchar *contents,
                   gsize length)
{
    GByteArray *central;
    guint32 offset;
    guint32 crc;
    guint16 name_length;

    offset = archive->len;
    crc = (guint32)crc32(0L, (const Bytef *)contents, length);
    name_length = (guint16)strlen(name);
    mt_vut_append_u32(archive, 0x04034b50);
    mt_vut_append_u16(archive, 20);
    mt_vut_append_u16(archive, 0);
    mt_vut_append_u16(archive, 0);
    mt_vut_append_u16(archive, 0);
    mt_vut_append_u16(archive, 0);
    mt_vut_append_u32(archive, crc);
    mt_vut_append_u32(archive, (guint32)length);
    mt_vut_append_u32(archive, (guint32)length);
    mt_vut_append_u16(archive, name_length);
    mt_vut_append_u16(archive, 0);
    g_byte_array_append(archive, (const guint8 *)name, name_length);
    g_byte_array_append(archive, (const guint8 *)contents, length);

    central = g_byte_array_new();
    mt_vut_append_u32(central, 0x02014b50);
    mt_vut_append_u16(central, 0x031E);
    mt_vut_append_u16(central, 20);
    mt_vut_append_u16(central, 0);
    mt_vut_append_u16(central, 0);
    mt_vut_append_u16(central, 0);
    mt_vut_append_u16(central, 0);
    mt_vut_append_u32(central, crc);
    mt_vut_append_u32(central, (guint32)length);
    mt_vut_append_u32(central, (guint32)length);
    mt_vut_append_u16(central, name_length);
    mt_vut_append_u16(central, 0);
    mt_vut_append_u16(central, 0);
    mt_vut_append_u16(central, 0);
    mt_vut_append_u16(central, 0);
    mt_vut_append_u32(central, 0);
    mt_vut_append_u32(central, offset);
    g_byte_array_append(central, (const guint8 *)name, name_length);
    g_ptr_array_add(central_entries, central);
}

gboolean
mt_vut_package_import(const gchar *archive_path,
                      const gchar *destination_directory,
                      gchar **module_path_out,
                      gchar **extension_id_out,
                      GError **error)
{
    gchar *archive;
    gsize archive_length;
    GPtrArray *entries;
    MtVutEntry *manifest_entry;
    MtVutEntry *module_entry;
    gchar *manifest_contents;
    gsize manifest_length;
    gchar *module_name;
    gchar *extension_id;
    gchar *module_contents;
    gsize module_length;
    gchar *destination_path;
    GKeyFile *manifest_settings;
    gchar *payload;

    archive = NULL;
    archive_length = 0;
    entries = NULL;
    manifest_contents = NULL;
    module_name = NULL;
    extension_id = NULL;
    module_contents = NULL;
    destination_path = NULL;
    manifest_settings = NULL;
    payload = NULL;

    if (!g_file_get_contents(archive_path, &archive, &archive_length, error))
    {
        return FALSE;
    }
    entries = mt_vut_parse_entries((const guchar *)archive, archive_length, error);
    if (entries == NULL)
    {
        g_free(archive);
        return FALSE;
    }

    manifest_entry = mt_vut_find_entry(entries, MT_VUT_MANIFEST_NAME);
    if (manifest_entry == NULL ||
        !mt_vut_extract_entry((const guchar *)archive,
                              archive_length,
                              manifest_entry,
                              &manifest_contents,
                              &manifest_length,
                              error))
    {
        if (manifest_entry == NULL)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "The .vut archive does not contain vellum-extension.ini");
        }
        g_free(manifest_contents);
        g_ptr_array_unref(entries);
        g_free(archive);
        return FALSE;
    }

    manifest_settings = g_key_file_new();
    if (!g_key_file_load_from_data(manifest_settings,
                                   manifest_contents,
                                   (gssize)manifest_length,
                                   G_KEY_FILE_NONE,
                                   error))
    {
        g_key_file_unref(manifest_settings);
        g_free(manifest_contents);
        g_ptr_array_unref(entries);
        g_free(archive);
        return FALSE;
    }
    payload = g_key_file_get_string(manifest_settings,
                                    "Vellum Extension",
                                    "payload",
                                    NULL);
    g_key_file_unref(manifest_settings);
    manifest_settings = NULL;

    if (g_strcmp0(payload, "source") == 0)
    {
        gboolean source_imported;

        source_imported = mt_vut_import_source_package((const guchar *)archive,
                                                        archive_length,
                                                        entries,
                                                        manifest_contents,
                                                        manifest_length,
                                                        destination_directory,
                                                        module_path_out,
                                                        extension_id_out,
                                                        error);
        g_free(payload);
        g_free(manifest_contents);
        g_ptr_array_unref(entries);
        g_free(archive);
        return source_imported;
    }
    g_free(payload);

    if (!mt_vut_validate_manifest(manifest_contents,
                                  manifest_length,
                                  &module_name,
                                  &extension_id,
                                  error))
    {
        g_free(manifest_contents);
        g_ptr_array_unref(entries);
        g_free(archive);
        return FALSE;
    }

    module_entry = mt_vut_find_entry(entries, module_name);
    if (module_entry == NULL ||
        !mt_vut_extract_entry((const guchar *)archive,
                              archive_length,
                              module_entry,
                              &module_contents,
                              &module_length,
                              error))
    {
        if (module_entry == NULL)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        "The .vut manifest module is not present in the ZIP archive");
        }
        g_free(extension_id);
        g_free(module_name);
        g_free(manifest_contents);
        g_ptr_array_unref(entries);
        g_free(archive);
        return FALSE;
    }

    if (!mt_vut_install_module(destination_directory,
                               extension_id,
                               module_contents,
                               module_length,
                               &destination_path,
                               error))
    {
        g_free(module_contents);
        g_free(extension_id);
        g_free(module_name);
        g_free(manifest_contents);
        g_ptr_array_unref(entries);
        g_free(archive);
        return FALSE;
    }

    *module_path_out = destination_path;
    *extension_id_out = extension_id;
    g_free(module_contents);
    g_free(module_name);
    g_free(manifest_contents);
    g_ptr_array_unref(entries);
    g_free(archive);
    return TRUE;
}

gboolean
mt_vut_package_export(const gchar *module_path,
                      const MtPluginInfo *info,
                      const gchar *destination_path,
                      GError **error)
{
    gchar *module_contents;
    gsize module_length;
    GKeyFile *settings;
    gchar *manifest_contents;
    gsize manifest_length;
    GByteArray *archive;
    GPtrArray *central_entries;
    guint32 central_offset;
    guint index;
    guint32 central_size;
    const gchar *module_name;
    gboolean saved;

    module_contents = NULL;
    module_length = 0;
    if (!g_file_get_contents(module_path, &module_contents, &module_length, error))
    {
        return FALSE;
    }
    if (module_length > MT_VUT_MAX_ENTRY_SIZE)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NO_SPACE,
                    "The native module is too large for the .vut export limit");
        g_free(module_contents);
        return FALSE;
    }

    module_name = "module." G_MODULE_SUFFIX;
    settings = g_key_file_new();
    g_key_file_set_integer(settings, "Vellum Extension", "format-version", 1);
    g_key_file_set_string(settings, "Vellum Extension", "id", info->id);
    g_key_file_set_string(settings, "Vellum Extension", "name", info->name);
    g_key_file_set_string(settings, "Vellum Extension", "version", info->version != NULL ? info->version : "0");
    g_key_file_set_integer(settings, "Vellum Extension", "plugin-api", MT_PLUGIN_API_VERSION);
    g_key_file_set_string(settings, "Vellum Extension", "license", "Unspecified");
    g_key_file_set_string(settings, "Vellum Extension", "module", module_name);
    g_key_file_set_string(settings, "Target", "os", mt_vut_package_get_host_os());
    g_key_file_set_string(settings, "Target", "architecture", mt_vut_package_get_host_architecture());
    g_key_file_set_string(settings, "Target", "abi", mt_vut_package_get_host_abi());
    manifest_contents = g_key_file_to_data(settings, &manifest_length, error);
    g_key_file_unref(settings);
    if (manifest_contents == NULL)
    {
        g_free(module_contents);
        return FALSE;
    }

    archive = g_byte_array_new();
    central_entries = g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);
    mt_vut_append_file(archive,
                       central_entries,
                       MT_VUT_MANIFEST_NAME,
                       manifest_contents,
                       manifest_length);
    mt_vut_append_file(archive,
                       central_entries,
                       module_name,
                       module_contents,
                       module_length);
    central_offset = archive->len;
    for (index = 0; index < central_entries->len; index++)
    {
        GByteArray *central;

        central = g_ptr_array_index(central_entries, index);
        g_byte_array_append(archive, central->data, central->len);
    }
    central_size = archive->len - central_offset;
    mt_vut_append_u32(archive, 0x06054b50);
    mt_vut_append_u16(archive, 0);
    mt_vut_append_u16(archive, 0);
    mt_vut_append_u16(archive, central_entries->len);
    mt_vut_append_u16(archive, central_entries->len);
    mt_vut_append_u32(archive, central_size);
    mt_vut_append_u32(archive, central_offset);
    mt_vut_append_u16(archive, 0);

    saved = g_file_set_contents(destination_path,
                                (const gchar *)archive->data,
                                archive->len,
                                error);
    g_ptr_array_unref(central_entries);
    g_byte_array_unref(archive);
    g_free(manifest_contents);
    g_free(module_contents);

    return saved;
}