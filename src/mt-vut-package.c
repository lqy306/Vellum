/*
 * mt-vut-package.c
 * `.vut` 是受限 ZIP 容器：仅支持小型、非加密的 Store/Deflate 文件和根目录清单。
 */

#include "mt-vut-package.h"

#include <glib/gstdio.h>
#include <string.h>
#include <zlib.h>

#define MT_VUT_MANIFEST_NAME "vellum-extension.ini"
#define MT_VUT_MAX_ARCHIVE_SIZE (32 * 1024 * 1024)
#define MT_VUT_MAX_ENTRY_SIZE (16 * 1024 * 1024)
#define MT_VUT_MAX_ENTRIES 256

typedef struct _MtVutEntry MtVutEntry;

struct _MtVutEntry
{
    gchar *name;
    guint16 flags;
    guint16 method;
    guint32 crc;
    guint32 compressed_size;
    guint32 uncompressed_size;
    guint32 local_offset;
};

static guint16
mt_vut_read_u16(const guchar *data)
{
    return (guint16)data[0] | ((guint16)data[1] << 8);
}

static guint32
mt_vut_read_u32(const guchar *data)
{
    return (guint32)data[0] |
           ((guint32)data[1] << 8) |
           ((guint32)data[2] << 16) |
           ((guint32)data[3] << 24);
}

static void
mt_vut_append_u16(GByteArray *array, guint16 value)
{
    guint8 bytes[2];

    bytes[0] = (guint8)(value & 0xff);
    bytes[1] = (guint8)((value >> 8) & 0xff);
    g_byte_array_append(array, bytes, sizeof(bytes));
}

static void
mt_vut_append_u32(GByteArray *array, guint32 value)
{
    guint8 bytes[4];

    bytes[0] = (guint8)(value & 0xff);
    bytes[1] = (guint8)((value >> 8) & 0xff);
    bytes[2] = (guint8)((value >> 16) & 0xff);
    bytes[3] = (guint8)((value >> 24) & 0xff);
    g_byte_array_append(array, bytes, sizeof(bytes));
}

static gboolean
mt_vut_is_safe_entry_name(const gchar *name)
{
    return name != NULL && *name != '\0' &&
           strchr(name, '/') == NULL && strchr(name, '\\') == NULL &&
           g_strcmp0(name, ".") != 0 && g_strcmp0(name, "..") != 0;
}

static gboolean
mt_vut_is_safe_archive_name(const gchar *name)
{
    gchar **parts;
    guint index;
    gboolean safe;

    if (name == NULL || *name == '\0' || name[0] == '/' || strchr(name, '\\') != NULL)
    {
        return FALSE;
    }

    parts = g_strsplit(name, "/", -1);
    safe = TRUE;
    for (index = 0; parts[index] != NULL; index++)
    {
        if (*parts[index] == '\0' || g_strcmp0(parts[index], ".") == 0 ||
            g_strcmp0(parts[index], "..") == 0)
        {
            safe = FALSE;
            break;
        }
    }
    g_strfreev(parts);

    return safe;
}

static gboolean
mt_vut_is_safe_identifier(const gchar *identifier)
{
    const gchar *cursor;

    if (identifier == NULL || *identifier == '\0')
    {
        return FALSE;
    }

    for (cursor = identifier; *cursor != '\0'; cursor++)
    {
        if (!(g_ascii_isalnum(*cursor) || *cursor == '.' || *cursor == '-' || *cursor == '_'))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static void
mt_vut_entry_free(MtVutEntry *entry)
{
    if (entry != NULL)
    {
        g_free(entry->name);
        g_free(entry);
    }
}

static gboolean
mt_vut_parse_entries(const guchar *data,
                     gsize length,
                     GPtrArray **entries_out,
                     GError **error)
{
    gsize search_start;
    gssize index;
    gsize eocd_offset;
    guint16 entry_count;
    guint32 directory_size;
    guint32 directory_offset;
    gsize cursor;
    GPtrArray *entries;
    guint index_entry;

    if (length < 22 || length > MT_VUT_MAX_ARCHIVE_SIZE)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "The .vut archive has an unsupported size");
        return FALSE;
    }

    search_start = length > (65535 + 22) ? length - (65535 + 22) : 0;
    eocd_offset = 0;
    for (index = (gssize)length - 22; index >= (gssize)search_start; index--)
    {
        if (mt_vut_read_u32(data + index) == 0x06054b50)
        {
            eocd_offset = (gsize)index;
            break;
        }
    }

    if (index < (gssize)search_start || eocd_offset + 22 > length ||
        mt_vut_read_u16(data + eocd_offset + 4) != 0 ||
        mt_vut_read_u16(data + eocd_offset + 6) != 0 ||
        mt_vut_read_u16(data + eocd_offset + 20) != 0)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "The .vut archive does not contain a supported ZIP directory");
        return FALSE;
    }

    entry_count = mt_vut_read_u16(data + eocd_offset + 10);
    directory_size = mt_vut_read_u32(data + eocd_offset + 12);
    directory_offset = mt_vut_read_u32(data + eocd_offset + 16);
    if (entry_count == 0 || entry_count > MT_VUT_MAX_ENTRIES ||
        (gsize)directory_offset + directory_size > length)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "The .vut archive has an invalid or oversized ZIP directory");
        return FALSE;
    }

    entries = g_ptr_array_new_with_free_func((GDestroyNotify)mt_vut_entry_free);
    cursor = directory_offset;
    for (index_entry = 0; index_entry < entry_count; index_entry++)
    {
        MtVutEntry *entry;
        guint16 name_length;
        guint16 extra_length;
        guint16 comment_length;

        if (cursor + 46 > length || mt_vut_read_u32(data + cursor) != 0x02014b50)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "The .vut archive contains an invalid central directory entry");
            g_ptr_array_unref(entries);
            return FALSE;
        }

        name_length = mt_vut_read_u16(data + cursor + 28);
        extra_length = mt_vut_read_u16(data + cursor + 30);
        comment_length = mt_vut_read_u16(data + cursor + 32);
        if (name_length == 0 || cursor + 46 + name_length + extra_length + comment_length > length)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "The .vut archive contains a truncated ZIP entry");
            g_ptr_array_unref(entries);
            return FALSE;
        }

        entry = g_new0(MtVutEntry, 1);
        entry->flags = mt_vut_read_u16(data + cursor + 8);
        entry->method = mt_vut_read_u16(data + cursor + 10);
        entry->crc = mt_vut_read_u32(data + cursor + 16);
        entry->compressed_size = mt_vut_read_u32(data + cursor + 20);
        entry->uncompressed_size = mt_vut_read_u32(data + cursor + 24);
        entry->local_offset = mt_vut_read_u32(data + cursor + 42);
        entry->name = g_strndup((const gchar *)(data + cursor + 46), name_length);

        if (!mt_vut_is_safe_archive_name(entry->name) ||
            (entry->flags & 0x0001) != 0 || (entry->flags & 0x0008) != 0 ||
            (entry->method != 0 && entry->method != 8) ||
            entry->compressed_size > MT_VUT_MAX_ENTRY_SIZE ||
            entry->uncompressed_size > MT_VUT_MAX_ENTRY_SIZE ||
            entry->compressed_size == G_MAXUINT32 || entry->uncompressed_size == G_MAXUINT32 ||
            entry->local_offset == G_MAXUINT32)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                        "The .vut archive contains an unsupported or unsafe ZIP entry");
            mt_vut_entry_free(entry);
            g_ptr_array_unref(entries);
            return FALSE;
        }

        g_ptr_array_add(entries, entry);
        cursor += 46 + name_length + extra_length + comment_length;
    }

    *entries_out = entries;
    return TRUE;
}

static MtVutEntry *
mt_vut_find_entry(GPtrArray *entries, const gchar *name)
{
    guint index;

    for (index = 0; index < entries->len; index++)
    {
        MtVutEntry *entry;

        entry = g_ptr_array_index(entries, index);
        if (g_strcmp0(entry->name, name) == 0)
        {
            return entry;
        }
    }

    return NULL;
}

static gboolean
mt_vut_extract_entry(const guchar *archive,
                     gsize archive_length,
                     MtVutEntry *entry,
                     gchar **contents_out,
                     gsize *length_out,
                     GError **error)
{
    guint16 name_length;
    guint16 extra_length;
    gsize data_offset;
    gchar *contents;
    uLong crc;

    if ((gsize)entry->local_offset + 30 > archive_length ||
        mt_vut_read_u32(archive + entry->local_offset) != 0x04034b50 ||
        mt_vut_read_u16(archive + entry->local_offset + 8) != entry->method)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "The .vut archive has an invalid local ZIP entry");
        return FALSE;
    }

    name_length = mt_vut_read_u16(archive + entry->local_offset + 26);
    extra_length = mt_vut_read_u16(archive + entry->local_offset + 28);
    data_offset = (gsize)entry->local_offset + 30 + name_length + extra_length;
    if (data_offset + entry->compressed_size > archive_length)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "The .vut archive has a truncated module entry");
        return FALSE;
    }

    contents = g_malloc(entry->uncompressed_size + 1);
    if (entry->method == 0)
    {
        if (entry->compressed_size != entry->uncompressed_size)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "The .vut archive has invalid stored entry sizes");
            g_free(contents);
            return FALSE;
        }
        memcpy(contents, archive + data_offset, entry->uncompressed_size);
    }
    else
    {
        z_stream stream;
        gint status;

        memset(&stream, 0, sizeof(stream));
        stream.next_in = (Bytef *)(archive + data_offset);
        stream.avail_in = entry->compressed_size;
        stream.next_out = (Bytef *)contents;
        stream.avail_out = entry->uncompressed_size;
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "Unable to initialize .vut ZIP decompression");
            g_free(contents);
            return FALSE;
        }
        status = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (status != Z_STREAM_END || stream.total_out != entry->uncompressed_size)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "Unable to decompress the .vut ZIP entry");
            g_free(contents);
            return FALSE;
        }
    }

    crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, (const Bytef *)contents, entry->uncompressed_size);
    if ((guint32)crc != entry->crc)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "The .vut ZIP entry failed its CRC check");
        g_free(contents);
        return FALSE;
    }

    contents[entry->uncompressed_size] = '\0';
    *contents_out = contents;
    *length_out = entry->uncompressed_size;
    return TRUE;
}

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

static gboolean
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

static gboolean
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

static void
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

static gboolean
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

static gboolean
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

static gboolean
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

static gboolean
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

    if (!g_file_get_contents(archive_path, &archive, &archive_length, error) ||
        !mt_vut_parse_entries((const guchar *)archive, archive_length, &entries, error))
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
