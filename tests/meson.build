test_document = executable(
  'test-document',
  files('test-document.c', '../src/mt-document.c', '../src/mt-settings.c'),
  include_directories : include_directories('../src'),
  dependencies : [
    dependency('gtk4', version : '>= 4.10'),
    dependency('libadwaita-1', version : '>= 1.4'),
    dependency('gtksourceview-5', version : '>= 5.10'),
    dependency('libspelling-1', version : '>= 0.2'),
    dependency('gio-2.0')
  ]
)

test(
  'document',
  test_document,
  suite : 'unit',
  env : ['GTK_A11Y=none']
)

test_vut_package = executable(
  'test-vut-package',
  files('test-vut-package.c',
        '../src/mt-vut-package.c',
        '../src/mt-vut-package-parse.c',
        '../src/mt-vut-package-manifest.c'),
  include_directories : include_directories('../src'),
  dependencies : [
    dependency('gio-2.0'),
    dependency('gmodule-2.0'),
    dependency('zlib')
  ]
)

test(
  'vut-package',
  test_vut_package,
  suite : 'unit'
)

test_theme_chooser = executable(
  'test-theme-chooser',
  files(
    'test-theme-chooser.c',
    '../src/mt-application.c',
    '../src/mt-window-about.c',
    '../src/mt-window-buffer.c',
    '../src/mt-window-core.c',
    '../src/mt-window-documents.c',
    '../src/mt-window-extensions.c',
    '../src/mt-window-find-replace.c',
    '../src/mt-window-properties-ui.c',
    '../src/mt-window-properties.c',
    '../src/mt-window-window.c',
    '../src/mt-window-shortcuts.c',
    '../src/mt-window-preferences.c',
    '../src/mt-overview-map.c',
    '../src/mt-document.c',
    '../src/mt-plugin-manager.c',
    '../src/mt-plugin-manager-bootstrap.c',
    '../src/mt-plugin-manager-lifecycle.c',
    '../src/mt-plugin-manager-marketplace.c',
    '../src/mt-vut-package-parse.c',
    '../src/mt-vut-package-manifest.c',
    '../src/mt-settings.c',
    '../src/mt-vut-package.c'
  ),
  include_directories : include_directories('../src'),
  dependencies : [
    dependency('gtk4', version : '>= 4.10'),
    dependency('libadwaita-1', version : '>= 1.4'),
    dependency('gtksourceview-5', version : '>= 5.10'),
    dependency('libspelling-1', version : '>= 0.2'),
    dependency('gio-2.0'),
    dependency('gmodule-2.0'),
    dependency('libsoup-3.0'),
    dependency('json-glib-1.0'),
    dependency('zlib')
  ]
)

test(
  'theme-chooser',
  test_theme_chooser,
  suite : 'unit',
  env : [
    'GTK_A11Y=none',
    'VELLUM_EXTENSIONS_BOOTSTRAP=0',
    'GDK_DISABLE=gl',
    'G_DEBUG=fatal-warnings'
  ]
)

test_document_save = executable(
  'test-document-save',
  files('test-document-save.c', '../src/mt-document.c', '../src/mt-settings.c'),
  include_directories : include_directories('../src'),
  dependencies : [
    dependency('gtk4', version : '>= 4.10'),
    dependency('libadwaita-1', version : '>= 1.4'),
    dependency('gtksourceview-5', version : '>= 5.10'),
    dependency('libspelling-1', version : '>= 0.2'),
    dependency('gio-2.0')
  ]
)

test(
  'document-save',
  test_document_save,
  suite : 'unit',
  env : [
    'GTK_A11Y=none',
    'GDK_DISABLE=gl',
    'G_DEBUG=fatal-warnings'
  ]
)





test_properties_panel = executable(
  'test-properties-panel',
  files(
    'test-properties-panel.c',
    '../src/mt-application.c',
    '../src/mt-window-about.c',
    '../src/mt-window-buffer.c',
    '../src/mt-window-core.c',
    '../src/mt-window-documents.c',
    '../src/mt-window-extensions.c',
    '../src/mt-window-find-replace.c',
    '../src/mt-window-properties-ui.c',
    '../src/mt-window-properties.c',
    '../src/mt-window-window.c',
    '../src/mt-window-shortcuts.c',
    '../src/mt-window-preferences.c',
    '../src/mt-overview-map.c',
    '../src/mt-document.c',
    '../src/mt-plugin-manager.c',
    '../src/mt-plugin-manager-bootstrap.c',
    '../src/mt-plugin-manager-lifecycle.c',
    '../src/mt-plugin-manager-marketplace.c',
    '../src/mt-vut-package-parse.c',
    '../src/mt-vut-package-manifest.c',
    '../src/mt-settings.c',
    '../src/mt-vut-package.c'
  ),
  include_directories : include_directories('../src'),
  dependencies : [
    dependency('gtk4', version : '>= 4.10'),
    dependency('libadwaita-1', version : '>= 1.4'),
    dependency('gtksourceview-5', version : '>= 5.10'),
    dependency('libspelling-1', version : '>= 0.2'),
    dependency('gio-2.0'),
    dependency('gmodule-2.0'),
    dependency('libsoup-3.0'),
    dependency('json-glib-1.0'),
    dependency('zlib')
  ]
)



test(
  'properties-panel',
  test_properties_panel,
  suite : 'unit',
  env : [
    'GTK_A11Y=none',
    'VELLUM_EXTENSIONS_BOOTSTRAP=0',
    'GDK_DISABLE=gl'
  ]
)



test_plugin_reinstall = executable(
  'test-plugin-reinstall',
  files(
    'test-plugin-reinstall.c',
    '../src/mt-application.c',
    '../src/mt-window-about.c',
    '../src/mt-window-buffer.c',
    '../src/mt-window-core.c',
    '../src/mt-window-documents.c',
    '../src/mt-window-extensions.c',
    '../src/mt-window-find-replace.c',
    '../src/mt-window-properties-ui.c',
    '../src/mt-window-properties.c',
    '../src/mt-window-window.c',
    '../src/mt-window-shortcuts.c',
    '../src/mt-window-preferences.c',
    '../src/mt-overview-map.c',
    '../src/mt-document.c',
    '../src/mt-plugin-manager.c',
    '../src/mt-plugin-manager-bootstrap.c',
    '../src/mt-plugin-manager-lifecycle.c',
    '../src/mt-plugin-manager-marketplace.c',
    '../src/mt-settings.c',
    '../src/mt-vut-package.c',
    '../src/mt-vut-package-parse.c',
    '../src/mt-vut-package-manifest.c'
  ),
  include_directories : include_directories('../src'),
  dependencies : [
    dependency('gtk4', version : '>= 4.10'),
    dependency('libadwaita-1', version : '>= 1.4'),
    dependency('gtksourceview-5', version : '>= 5.10'),
    dependency('libspelling-1', version : '>= 0.2'),
    dependency('gio-2.0'),
    dependency('gmodule-2.0'),
    dependency('libsoup-3.0'),
    dependency('json-glib-1.0'),
    dependency('zlib'),
    dependency('gio-unix-2.0')
  ]
)

test(
  'plugin-reinstall',
  test_plugin_reinstall,
  suite : 'unit',
  env : [
    'GTK_A11Y=none',
    'VELLUM_EXTENSIONS_BOOTSTRAP=0',
    'GDK_DISABLE=gl',
    'GIO_USE_PROXY_RESOLVER=dummy',
    'VELLUM_TEST_PLUGIN=' + meson.project_build_root() / 'src/plugins/timestamp-plugin.so'
  ]
)

test(
  'tui-debug',
  find_program('test-tui-debug.sh'),
  suite : 'unit',
  env : [
    'GTK_A11Y=none',
    'VELLUM_EXTENSIONS_BOOTSTRAP=0',
    'GDK_DISABLE=gl',
    'G_DEBUG=fatal-warnings',
    'VELLUM_TUI_BIN=' + vellum_tui_debug_executable.full_path()
  ]
)


