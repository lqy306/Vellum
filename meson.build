project(
  'vellum',
  'c',
  meson_version : '>= 1.3.0',
  default_options : [
    'c_std=c11',
    'warning_level=3',
    'werror=false',
    'buildtype=debugoptimized'
  ]
)

# 构建日期版本（YYYY.MM.DD），仅用于“关于 → 检查更新”的比较，不对外展示。
date_cmd = run_command('date', '+%Y.%m.%d', check : false)
vellum_version = date_cmd.stdout().strip()
if vellum_version == ''
  vellum_version = '0'
endif
vellum_gettext_package = 'vellum'
vellum_datadir = join_paths(get_option('prefix'), get_option('datadir'))
vellum_localedir = join_paths(vellum_datadir, 'locale')
vellum_plugin_dir = join_paths(get_option('prefix'), get_option('libdir'), 'vellum', 'plugins')

add_project_arguments(
  '-DGETTEXT_PACKAGE="' + vellum_gettext_package + '"',
  '-DVELLUM_VERSION="' + vellum_version + '"',
  '-DLOCALEDIR="' + vellum_localedir + '"',
  '-DVELLUM_PLUGIN_DIR="' + vellum_plugin_dir + '"',
  language : 'c'
)

subdir('src')
subdir('tests')
subdir('data')
subdir('po')

summary({
  'Version' : vellum_version,
  'Prefix' : get_option('prefix'),
  'Plugin directory' : vellum_plugin_dir
}, section : 'Vellum')
