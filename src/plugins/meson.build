plugin_install_dir = join_paths(get_option('libdir'), 'vellum', 'plugins')

shared_module(
  'timestamp-plugin',
  'timestamp-plugin.c',
  include_directories : include_directories('..'),
  dependencies : [gio_dep, gmodule_dep],
  name_prefix : '',
  install : true,
  install_dir : plugin_install_dir
)

shared_module(
  'welcome-plugin',
  'welcome-plugin.c',
  include_directories : include_directories('..'),
  dependencies : [gtk_dep, adw_dep, gio_dep, gmodule_dep],
  name_prefix : '',
  install : true,
  install_dir : plugin_install_dir
)

shared_module(
  'word-count-plugin',
  'word-count-plugin.c',
  include_directories : include_directories('..'),
  dependencies : [gio_dep, gmodule_dep],
  name_prefix : '',
  install : true,
  install_dir : plugin_install_dir
)

shared_module(
  'ai-completion-plugin',
  'ai-completion-plugin.c',
  include_directories : include_directories('..'),
  dependencies : [gtk_dep, adw_dep, gio_dep, gmodule_dep, soup_dep, json_dep],
  name_prefix : '',
  install : true,
  install_dir : plugin_install_dir
)

shared_module(
  'link-check-plugin',
  'link-check-plugin.c',
  include_directories : include_directories('..'),
  dependencies : [gio_dep, gmodule_dep, soup_dep],
  name_prefix : '',
  install : true,
  install_dir : plugin_install_dir
)

shared_module(
  'project-sidebar-plugin',
  'project-sidebar-plugin.c',
  include_directories : include_directories('..'),
  dependencies : [gtk_dep, adw_dep, gio_dep, gmodule_dep],
  name_prefix : '',
  install : true,
  install_dir : plugin_install_dir
)

shared_module(
  'build-run-plugin',
  'build-run-plugin.c',
  include_directories : include_directories('..'),
  dependencies : [gtk_dep, adw_dep, gio_dep, gmodule_dep],
  name_prefix : '',
  install : true,
  install_dir : plugin_install_dir
)


shared_module(
  'screenshot-plugin',
  'screenshot-plugin.c',
  include_directories : include_directories('..'),
  dependencies : [gtk_dep, gio_dep, gmodule_dep],
  name_prefix : '',
  install : true,
  install_dir : plugin_install_dir
)
shared_module(
  'vim-mode-plugin',
  'vim-mode-plugin.c',
  include_directories : include_directories('..'),
  dependencies : [gtk_dep, gio_dep, gmodule_dep],
  name_prefix : '',
  install : true,
  install_dir : plugin_install_dir
)
