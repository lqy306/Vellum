gtk_dep = dependency('gtk4', version : '>= 4.10')
# 文档类型选择器使用 AdwDialog（1.5 起提供），检查器侧栏仍采用 1.4 引入的 OverlaySplitView。
adw_dep = dependency('libadwaita-1', version : '>= 1.5')
sourceview_dep = dependency('gtksourceview-5', version : '>= 5.10')
# libspelling 0.2 已提供当前使用的 GTK4 适配器 API；保持较低下限以支持
# Ubuntu 24.04 等稳定发行版，避免将可用的拼写检查错误地判定为不可构建。
spelling_dep = dependency('libspelling-1', version : '>= 0.2')
gio_dep = dependency('gio-2.0')
gmodule_dep = dependency('gmodule-2.0')
soup_dep = dependency('libsoup-3.0')
json_dep = dependency('json-glib-1.0')
zlib_dep = dependency('zlib')

# 两个入口共享同一套核心；TUI 不加载插件，仅用于受控调试和测试。
vellum_core_sources = files(
  'mt-application.c',
  'mt-document.c',
  'mt-plugin-manager.c',
  'mt-settings.c',
  'mt-vut-package.c',
  'mt-overview-map.c',
  'mt-window-shortcuts.c',
  'mt-window-preferences.c',
  'mt-window.c'
)

vellum_executable = executable(
  'vellum',
  files('main.c'),
  vellum_core_sources,
  dependencies : [gtk_dep, adw_dep, sourceview_dep, spelling_dep, gio_dep, gmodule_dep, soup_dep, json_dep, zlib_dep],
  install : true
)

vellum_tui_debug_executable = executable(
  'vellum-tui-debug',
  files('mt-tui-debug.c'),
  vellum_core_sources,
  dependencies : [gtk_dep, adw_dep, sourceview_dep, spelling_dep, gio_dep, gmodule_dep, soup_dep, json_dep, zlib_dep],
  install : true
)
