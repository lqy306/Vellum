/*
 * test-vut-package.c
 * Vellum `.vut` 二进制扩展包的最小往返回归测试。
 */

#include "../src/mt-vut-package.h"

#include <glib/gstdio.h>
#include <zlib.h>

static void
remove_tree(const gchar *path)
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
            g_remove(child);
            g_free(child);
        }
        g_dir_close(directory);
    }
    g_rmdir(path);
}

static void
test_vut_binary_roundtrip(void)
{
    MtPluginInfo info;
    gchar *root;
    gchar *module_path;
    gchar *package_path;
    gchar *install_directory;
    gchar *imported_path;
    gchar *extension_id;
    gchar *contents;
    gsize length;
    GError *error;

    root = g_dir_make_tmp("vellum-vut-test-XXXXXX", NULL);
    g_assert_nonnull(root);
    module_path = g_build_filename(root, "fixture." G_MODULE_SUFFIX, NULL);
    package_path = g_build_filename(root, "fixture.vut", NULL);
    install_directory = g_build_filename(root, "installed", NULL);
    g_assert_true(g_file_set_contents(module_path, "fixture-native-module", -1, NULL));

    info.api_version = MT_PLUGIN_API_VERSION;
    info.id = "io.github.vellum.fixture";
    info.name = "Fixture";
    info.description = "Test fixture";
    info.version = "1.0.0";
    error = NULL;
    g_assert_true(mt_vut_package_export(module_path, &info, package_path, &error));
    g_assert_no_error(error);

    imported_path = NULL;
    extension_id = NULL;
    g_assert_true(mt_vut_package_import(package_path,
                                        install_directory,
                                        &imported_path,
                                        &extension_id,
                                        &error));
    g_assert_no_error(error);
    g_assert_cmpstr(extension_id, ==, info.id);
    g_assert_true(g_file_get_contents(imported_path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpstr(contents, ==, "fixture-native-module");

    g_free(contents);
    g_free(extension_id);
    g_remove(imported_path);
    g_free(imported_path);
    g_rmdir(install_directory);
    g_remove(package_path);
    g_remove(module_path);
    g_free(install_directory);
    g_free(package_path);
    g_free(module_path);
    remove_tree(root);
    g_free(root);
}

static void
append_u16(GByteArray *array, guint16 value)
{
    guint8 bytes[2];

    bytes[0] = (guint8)(value & 0xff);
    bytes[1] = (guint8)((value >> 8) & 0xff);
    g_byte_array_append(array, bytes, sizeof(bytes));
}

static void
append_u32(GByteArray *array, guint32 value)
{
    guint8 bytes[4];

    bytes[0] = (guint8)(value & 0xff);
    bytes[1] = (guint8)((value >> 8) & 0xff);
    bytes[2] = (guint8)((value >> 16) & 0xff);
    bytes[3] = (guint8)((value >> 24) & 0xff);
    g_byte_array_append(array, bytes, sizeof(bytes));
}

static void
append_zip_file(GByteArray *archive,
                GPtrArray *central_entries,
                const gchar *name,
                const gchar *contents)
{
    GByteArray *central;
    guint32 offset;
    guint32 crc;
    guint16 name_length;
    guint32 length;

    offset = archive->len;
    length = (guint32)strlen(contents);
    crc = (guint32)crc32(0L, (const Bytef *)contents, length);
    name_length = (guint16)strlen(name);
    append_u32(archive, 0x04034b50);
    append_u16(archive, 20);
    append_u16(archive, 0);
    append_u16(archive, 0);
    append_u16(archive, 0);
    append_u16(archive, 0);
    append_u32(archive, crc);
    append_u32(archive, length);
    append_u32(archive, length);
    append_u16(archive, name_length);
    append_u16(archive, 0);
    g_byte_array_append(archive, (const guint8 *)name, name_length);
    g_byte_array_append(archive, (const guint8 *)contents, length);

    central = g_byte_array_new();
    append_u32(central, 0x02014b50);
    append_u16(central, 0x031E);
    append_u16(central, 20);
    append_u16(central, 0);
    append_u16(central, 0);
    append_u16(central, 0);
    append_u16(central, 0);
    append_u32(central, crc);
    append_u32(central, length);
    append_u32(central, length);
    append_u16(central, name_length);
    append_u16(central, 0);
    append_u16(central, 0);
    append_u16(central, 0);
    append_u16(central, 0);
    append_u32(central, 0);
    append_u32(central, offset);
    g_byte_array_append(central, (const guint8 *)name, name_length);
    g_ptr_array_add(central_entries, central);
}

static gboolean
write_source_vut(const gchar *path, GError **error)
{
    gchar *manifest;
    GByteArray *archive;
    GPtrArray *central_entries;
    guint32 central_offset;
    guint32 central_size;
    guint index;
    gboolean saved;

    manifest = g_strdup_printf("[Vellum Extension]\n"
                               "format-version=1\n"
                               "id=io.github.vellum.source-fixture\n"
                               "name=Source Fixture\n"
                               "version=1.0.0\n"
                               "plugin-api=%d\n"
                               "license=BSD-2-Clause\n"
                               "payload=source\n"
                               "\n"
                               "[Target]\n"
                               "os=%s\n"
                               "architecture=%s\n"
                               "abi=%s\n"
                               "\n"
                               "[Source]\n"
                               "source-root=source\n"
                               "build-tool=make\n"
                               "build-arguments=plugin\n"
                               "output-module=build/module.%s\n"
                               "required-tools=make;cc\n",
                               MT_PLUGIN_API_VERSION,
                               mt_vut_package_get_host_os(),
                               mt_vut_package_get_host_architecture(),
                               mt_vut_package_get_host_abi(),
                               G_MODULE_SUFFIX);
    archive = g_byte_array_new();
    central_entries = g_ptr_array_new_with_free_func((GDestroyNotify)g_byte_array_unref);
    append_zip_file(archive, central_entries, "vellum-extension.ini", manifest);
    append_zip_file(archive,
                    central_entries,
                    "source/Makefile",
                    "plugin:\n\tmkdir -p build\n\tcc -shared -fPIC module.c -o build/module." G_MODULE_SUFFIX "\n");
    append_zip_file(archive,
                    central_entries,
                    "source/module.c",
                    "int source_fixture(void) { return 7; }\n");
    central_offset = archive->len;
    for (index = 0; index < central_entries->len; index++)
    {
        GByteArray *central;

        central = g_ptr_array_index(central_entries, index);
        g_byte_array_append(archive, central->data, central->len);
    }
    central_size = archive->len - central_offset;
    append_u32(archive, 0x06054b50);
    append_u16(archive, 0);
    append_u16(archive, 0);
    append_u16(archive, central_entries->len);
    append_u16(archive, central_entries->len);
    append_u32(archive, central_size);
    append_u32(archive, central_offset);
    append_u16(archive, 0);
    saved = g_file_set_contents(path, (const gchar *)archive->data, archive->len, error);
    g_ptr_array_unref(central_entries);
    g_byte_array_unref(archive);
    g_free(manifest);

    return saved;
}

static void
test_vut_source_build(void)
{
    gchar *root;
    gchar *package_path;
    gchar *install_directory;
    gchar *imported_path;
    gchar *extension_id;
    GError *error;

    if (g_find_program_in_path("make") == NULL || g_find_program_in_path("cc") == NULL)
    {
        g_test_skip("The source .vut test requires make and cc");
        return;
    }

    root = g_dir_make_tmp("vellum-vut-source-test-XXXXXX", NULL);
    g_assert_nonnull(root);
    package_path = g_build_filename(root, "fixture-source.vut", NULL);
    install_directory = g_build_filename(root, "installed", NULL);
    error = NULL;
    g_assert_true(write_source_vut(package_path, &error));
    g_assert_no_error(error);

    imported_path = NULL;
    extension_id = NULL;
    if (!mt_vut_package_import(package_path,
                               install_directory,
                               &imported_path,
                               &extension_id,
                               &error))
    {
        g_test_message("Source .vut import error: %s", error != NULL ? error->message : "unknown");
        g_assert_not_reached();
    }
    g_assert_no_error(error);
    g_assert_cmpstr(extension_id, ==, "io.github.vellum.source-fixture");
    g_assert_true(g_file_test(imported_path, G_FILE_TEST_IS_REGULAR));

    g_remove(imported_path);
    g_free(imported_path);
    g_free(extension_id);
    g_rmdir(install_directory);
    g_remove(package_path);
    g_free(install_directory);
    g_free(package_path);
    remove_tree(root);
    g_free(root);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/vellum/vut/binary-roundtrip", test_vut_binary_roundtrip);
    g_test_add_func("/vellum/vut/source-build", test_vut_source_build);

    return g_test_run();
}
