# Windows x86_64 交叉构建

Vellum 的业务代码采用 C 语言，构建系统已包含 `x86_64-w64-mingw32` Meson 交叉文件。Windows 端的 GTK4、Libadwaita 与 GtkSourceView 5 必须来自**同一个 MinGW-w64 前缀**；仅安装 Ubuntu 的 MinGW 编译器不足以构建 GTK 应用。

## 建议环境

推荐在 MSYS2 的 `MINGW64` 终端安装 MinGW 运行时和开发包，再在同一终端执行 Meson。若在 Linux CI 中交叉编译，需要预先准备等价的 `x86_64-w64-mingw32` sysroot 和 `pkg-config` 元数据。

| 组件 | 需要内容 |
|---|---|
| 编译器 | `x86_64-w64-mingw32-gcc`、`ar`、`strip` |
| 图形栈 | GTK4、Libadwaita、GtkSourceView 5 的头文件、库与 DLL |
| GLib | GLib、GIO、GModule、Gettext 运行时 |
| AI 扩展 | libsoup 3、JSON-GLib 的头文件、库与 DLL |
| 打包 | 编辑器 `.exe`、插件 `.dll`、依赖 DLL、`share/locale`、图标和桌面元数据 |

## 配置与编译

在项目根目录运行：

```bash
meson setup build-windows \
  --cross-file packaging/windows/x86_64-w64-mingw32.ini \
  --prefix /opt/vellum-win64
meson compile -C build-windows
meson install -C build-windows --destdir "$PWD/dist/windows-stage"
```

若 `pkg-config` 位于自定义 sysroot，请先正确设置 `PKG_CONFIG_LIBDIR` 与 `PKG_CONFIG_SYSROOT_DIR`。可使用下列命令确认交叉依赖解析：

```bash
x86_64-w64-mingw32-pkg-config --modversion gtk4 libadwaita-1 gtksourceview-5 libsoup-3.0 json-glib-1.0
```

## 便携启动器

发行目录应设置下列环境变量，使移动后的程序仍能找到翻译和插件：

```bat
set VELLUM_LOCALEDIR=%~dp0share\locale
set VELLUM_PLUGIN_DIR=%~dp0lib\vellum\plugins
vellum.exe
```

当前阶段**仅配置 Windows 交叉构建**，未在本 Linux 环境生成或签名 Windows 二进制；原因是该环境没有一套可验证的 Windows GTK4/Libadwaita/GtkSourceView/libsoup/JSON-GLib MinGW sysroot。Linux AppImage 是本阶段的实际测试目标。
