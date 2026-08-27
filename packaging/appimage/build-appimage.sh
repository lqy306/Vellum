#!/bin/sh
# build-appimage.sh
# 从 Meson staging 目录创建用于测试的 Linux amd64 AppImage。

set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$PROJECT_ROOT/build"}
WORK_DIR=${WORK_DIR:-"$PROJECT_ROOT/dist/appimage-work"}
APPDIR="$WORK_DIR/Vellum.AppDir"
STAGE_DIR="$WORK_DIR/stage"
VERSION=${VERSION:-"1.0.1"}
OUTPUT=${OUTPUT:-"$PROJECT_ROOT/dist/vellum-$VERSION-amd64.AppImage"}
APPIMAGETOOL=${APPIMAGETOOL:-appimagetool}

if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found: $BUILD_DIR" >&2
    echo "Run: meson setup build --prefix=/usr" >&2
    exit 1
fi

rm -rf "$WORK_DIR"
mkdir -p "$APPDIR" "$STAGE_DIR" "$(dirname -- "$OUTPUT")"

meson compile -C "$BUILD_DIR"
meson install -C "$BUILD_DIR" --destdir "$STAGE_DIR"

# Meson 默认安装前缀为 /usr/local；发行构建也允许调用方配置为 /usr。
if [ -d "$STAGE_DIR/usr/local" ] && [ ! -d "$STAGE_DIR/usr/bin" ]; then
    STAGED_PREFIX="$STAGE_DIR/usr/local"
else
    STAGED_PREFIX="$STAGE_DIR/usr"
fi

mkdir -p "$APPDIR/usr"
cp -a "$STAGED_PREFIX/." "$APPDIR/usr/"
cp "$PROJECT_ROOT/data/io.github.vellum.Vellum.desktop" "$APPDIR/"
cp "$PROJECT_ROOT/data/io.github.vellum.Vellum.svg" "$APPDIR/"
cp "$PROJECT_ROOT/data/icons/hicolor/scalable/apps/io.github.vellum.Vellum.svg" \
   "$APPDIR/io.github.vellum.Vellum.svg"
cp "$PROJECT_ROOT/data/io.github.vellum.Vellum.metainfo.xml" \
   "$APPDIR/usr/share/metainfo/io.github.vellum.Vellum.appdata.xml"

cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
# AppImage 启动器：将随包资源定位到当前挂载的 AppDir。
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PLUGIN_DIR=$(find "$HERE/usr/lib" -type d -path '*/vellum/plugins' -print -quit 2>/dev/null || true)

export VELLUM_LOCALEDIR="$HERE/usr/share/locale"
export VELLUM_PLUGIN_DIR="$PLUGIN_DIR"
export PATH="$HERE/usr/bin:$PATH"

exec "$HERE/usr/bin/vellum" "$@"
EOF
chmod +x "$APPDIR/AppRun"

if ! command -v "$APPIMAGETOOL" >/dev/null 2>&1 && [ ! -x "$APPIMAGETOOL" ]; then
    echo "appimagetool is required. Set APPIMAGETOOL to its executable path." >&2
    exit 1
fi

ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
chmod +x "$OUTPUT"
printf 'Created %s\n' "$OUTPUT"
