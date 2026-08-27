#!/bin/sh
# 为 Vellum 内置扩展生成源码 .vut 包。
# 这些包面向 GNU/Linux：导入后由目标机器使用 make 与 cc 重新构建本机模块。

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
output_dir=${1:-"$project_root/dist/vut-source"}
temp_root=$(mktemp -d)
trap 'rm -rf "$temp_root"' EXIT INT TERM

mkdir -p "$output_dir"

package_plugin() {
    package_name=$1
    plugin_id=$2
    display_name=$3
    source_file=$4
    package_list=$5

    package_dir="$temp_root/$package_name"
    source_dir="$package_dir/source"
    mkdir -p "$source_dir/include"
    cp "$project_root/src/plugins/$source_file" "$source_dir/"
    cp "$project_root/src/mt-plugin.h" "$source_dir/include/"

    cat >"$source_dir/Makefile" <<'EOF'
CC ?= cc
CFLAGS ?= -O2 -fPIC -std=c11
PKGS = @PKGS@

plugin:
	mkdir -p build
	@echo "Building @NAME@ for the local Vellum runtime"
	$(CC) $(CFLAGS) -Iinclude $(shell pkg-config --cflags $(PKGS)) -shared @SOURCE@ -o build/module.so $(shell pkg-config --libs $(PKGS))
EOF
    sed -i "s|@PKGS@|$package_list|; s|@NAME@|$package_name|; s|@SOURCE@|$source_file|" "$source_dir/Makefile"

    cat >"$package_dir/vellum-extension.ini" <<EOF
[Vellum Extension]
format-version=1
id=$plugin_id
name=$display_name
version=0
plugin-api=2
license=BSD-2-Clause
payload=source

[Target]
os=linux
architecture=any
abi=any

[Source]
source-root=source
build-tool=make
build-arguments=plugin
output-module=build/module.so
required-tools=make;cc;pkg-config
EOF

    rm -f "$output_dir/$package_name-linux-source.vut"
    (cd "$package_dir" && zip -q -r "$output_dir/$package_name-linux-source.vut" vellum-extension.ini source)
}

package_plugin "timestamp" "io.github.vellum.timestamp" "Timestamp" "timestamp-plugin.c" "gio-2.0 gmodule-2.0"
package_plugin "document-statistics" "io.github.vellum.document-statistics" "Document Statistics" "word-count-plugin.c" "gio-2.0 gmodule-2.0"
package_plugin "ai-completion" "io.github.vellum.ai-completion" "AI Completion" "ai-completion-plugin.c" "gtk4 libadwaita-1 gio-2.0 gmodule-2.0 libsoup-3.0 json-glib-1.0"
package_plugin "link-check" "io.github.vellum.link-check" "Test Links" "link-check-plugin.c" "gio-2.0 gmodule-2.0 libsoup-3.0"
package_plugin "project-sidebar" "io.github.vellum.project-sidebar" "Project Sidebar" "project-sidebar-plugin.c" "gtk4 libadwaita-1 gio-2.0 gmodule-2.0"
package_plugin "build-run" "io.github.vellum.build-run" "Build & Run" "build-run-plugin.c" "gtk4 libadwaita-1 gio-2.0 gmodule-2.0"
package_plugin "vim-mode" "io.github.vellum.vim-mode" "Vi Mode" "vim-mode-plugin.c" "gtk4 gio-2.0 gmodule-2.0"

printf 'Generated source .vut packages in %s\n' "$output_dir"
