/*
 * mt-vut-package-parse.c
 * `.vut` ZIP 容器的条目解析与提取。
 */

#include "mt-vut-package-private.h"

#include <gio/gio.h>
#include <string.h>

guint16
mt_vut_read_u16(const guchar *data)
{
    return (guint16)data[0] | ((guint16)data[1] << 8);
}

guint32
mt_vut_read_u32(const guchar *data)
{
    return (guint32)data[0] |
           ((guint32)data[1] << 8) |
           ((guint32)data[2] << 16) |
           ((guint32)data[3] << 24);
}

void
mt_vut_append_u16(GByteArray *array, guint16 value)
{
    guint8 bytes[2];

    bytes[0] = (guint8)(value & 0xff);
    bytes[1] = (guint8)((value >> 8) & 0xff);
    g_byte_array_append(array, bytes, sizeof(bytes));
}

void
mt_vut_append_u32(GByteArray *array, guint32 value)
{
    guint8 bytes[4];

    bytes[0] = (guint8)(value & 0xff);
    bytes[1] = (guint8)((value >> 8) & 0xff);
    bytes[2] = (guint8)((value >> 16) & 0xff);
    bytes[3] = (guint8)((value >> 24) & 0xff);
    g_byte_array_append(array, bytes, sizeof(bytes));
}

gboolean
mt_vut_is_safe_entry_name(const gchar *name)
{
    return name != NULL && *name != '\0' &&
           strchr(name, '/') == NULL && strchr(name, '\\') == NULL &&
           g_strcmp0(name, ".") != 0 && g_strcmp0(name, "..") != 0;
}

gboolean
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

gboolean
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

void
mt_vut_entry_free(MtVutEntry *entry)
{
    if (entry != NULL)
    {
        g_free(entry->name);
        g_free(entry);
    }
}

GPtrArray *
mt_vut_parse_entries(const guchar *data,
                     gsize length,
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
        return NULL;
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
        return NULL;
    }

    entry_count = mt_vut_read_u16(data + eocd_offset + 10);
    directory_size = mt_vut_read_u32(data + eocd_offset + 12);
    directory_offset = mt_vut_read_u32(data + eocd_offset + 16);
    if (entry_count == 0 || entry_count > MT_VUT_MAX_ENTRIES ||
        (gsize)directory_offset + directory_size > length)
    {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "The .vut archive has an invalid or oversized ZIP directory");
        return NULL;
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
            return NULL;
        }

        name_length = mt_vut_read_u16(data + cursor + 28);
        extra_length = mt_vut_read_u16(data + cursor + 30);
        comment_length = mt_vut_read_u16(data + cursor + 32);
        if (name_length == 0 || cursor + 46 + name_length + extra_length + comment_length > length)
        {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "The .vut archive contains a truncated ZIP entry");
            g_ptr_array_unref(entries);
            return NULL;
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
            return NULL;
        }

        g_ptr_array_add(entries, entry);
        cursor += 46 + name_length + extra_length + comment_length;
    }

    return entries;
}

MtVutEntry *
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

gboolean
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