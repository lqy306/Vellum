#!/bin/sh
# 验证受限 TUI 能驱动真实 GTK 核心窗口，不依赖插件或用户配置。

set -eu

if [ -z "${VELLUM_TUI_BIN:-}" ]; then
    echo "VELLUM_TUI_BIN is required" >&2
    exit 2
fi

temporary_directory=$(mktemp -d /tmp/vellum-tui-test-XXXXXX)
result_file="$temporary_directory/result.md"
output_file="$temporary_directory/output.jsonl"

cleanup() {
    rm -rf "$temporary_directory"
}
trap cleanup EXIT HUP INT TERM

printf '%s\n' \
    'STATE' \
    'TEXT # Heading\n- item' \
    'STATE' \
    "SAVE $result_file" \
    'WAIT 1000' \
    'STATE' \
    'QUIT' | "$VELLUM_TUI_BIN" > "$output_file"

grep -F '"event":"ready"' "$output_file" >/dev/null
grep -F '"extensions_loaded":false' "$output_file" >/dev/null
grep -F '"text":"# Heading\n- item"' "$output_file" >/dev/null
grep -F '"saving":false' "$output_file" >/dev/null

expected=$(printf '# Heading\n- item')
[ "$(cat "$result_file")" = "$expected" ]
