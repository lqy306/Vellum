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
  files('test-vut-package.c', '../src/mt-vut-package.c'),
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

test_build_run_markup = executable(
  'test-build-run-markup',
  files('test-build-run-markup.c'),
  include_directories : include_directories('../src'),
  dependencies : [
    dependency('gtk4', version : '>= 4.10'),
    dependency('libadwaita-1', version : '>= 1.4'),
    dependency('gmodule-2.0')
  ]
)

test(
  'build-run-markup',
  test_build_run_markup,
  suite : 'unit',
  env : [
    'GTK_A11Y=none',
    'GDK_DISABLE=gl',
    'G_DEBUG=fatal-warnings',
    'VELLUM_BUILD_RUN_TEST_PLUGIN=' + join_paths(meson.project_build_root(), 'src', 'plugins', 'build-run-plugin.so')
  ]
)

test_theme_chooser = executable(
  'test-theme-chooser',
  files(
    'test-theme-chooser.c',
    '../src/mt-application.c',
    '../src/mt-window.c',
    '../src/mt-window-shortcuts.c',
    '../src/mt-window-preferences.c',
    '../src/mt-overview-map.c',
    '../src/mt-document.c',
    '../src/mt-plugin-manager.c',
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


test_ai_completion_request = executable(
  'test-ai-completion-request',
  files('test-ai-completion-request.c'),
  include_directories : include_directories('../src'),
  dependencies : [
    dependency('gtk4', version : '>= 4.10'),
    dependency('libadwaita-1', version : '>= 1.4'),
    dependency('gio-2.0'),
    dependency('gmodule-2.0'),
    dependency('libsoup-3.0')
  ]
)

test(
  'ai-completion-request',
  test_ai_completion_request,
  suite : 'unit',
  env : [
    'GTK_A11Y=none',
    'GDK_DISABLE=gl',
    'G_DEBUG=fatal-warnings',
    'VELLUM_AI_TEST_PLUGIN=' + join_paths(meson.project_build_root(), 'src', 'plugins', 'ai-completion-plugin.so')
  ]
)

test_ai_completion_parsing = executable(
  'test-ai-completion-parsing',
  files('test-ai-completion-parsing.c', '../src/plugins/ai-code-summary.c'),
  include_directories : include_directories('../src'),
  dependencies : [
    dependency('gtk4', version : '>= 4.10'),
    dependency('libadwaita-1', version : '>= 1.4'),
    dependency('gio-2.0'),
    dependency('gmodule-2.0'),
    dependency('libsoup-3.0'),
    dependency('json-glib-1.0')
  ]
)

test(
  'ai-completion-parsing',
  test_ai_completion_parsing,
  suite : 'unit',
  env : [
    'G_DEBUG=fatal-warnings'
  ]
)


test_properties_panel = executable(
  'test-properties-panel',
  files(
    'test-properties-panel.c',
    '../src/mt-application.c',
    '../src/mt-window.c',
    '../src/mt-window-shortcuts.c',
    '../src/mt-window-preferences.c',
    '../src/mt-overview-map.c',
    '../src/mt-document.c',
    '../src/mt-plugin-manager.c',
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
    'GDK_DISABLE=gl'
  ]
)



test_screenshot_plugin = executable(
  'test-screenshot-plugin',
  files(
    'test-screenshot-plugin.c',
    '../src/mt-application.c',
    '../src/mt-window.c',
    '../src/mt-window-shortcuts.c',
    '../src/mt-window-preferences.c',
    '../src/mt-overview-map.c',
    '../src/mt-document.c',
    '../src/mt-plugin-manager.c',
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
  'screenshot-plugin',
  test_screenshot_plugin,
  suite : 'unit',
  env : [
    'GTK_A11Y=none',
    'GDK_DISABLE=gl',
    'VELLUM_SCREENSHOT_TEST_PLUGIN=' + join_paths(meson.project_build_root(), 'src', 'plugins', 'screenshot-plugin.so')
  ]
)


test(
  'tui-debug',
  find_program('test-tui-debug.sh'),
  suite : 'unit',
  env : [
    'GTK_A11Y=none',
    'GDK_DISABLE=gl',
    'G_DEBUG=fatal-warnings',
    'VELLUM_TUI_BIN=' + vellum_tui_debug_executable.full_path()
  ]
)


