# 构建与测试记录

本文档记录 **Vellum** 的可复现构建路径与本次交付的验证边界。项目源文件主要遵循 ANSI C/C90 的声明和控制流风格，编译时使用 C11 是为了兼容 GTK4/GLib 的现代头文件和 ABI。

## Linux 本机构建

在 Ubuntu/Debian 系统上安装下列依赖后，可使用 Meson/Ninja 构建：

```bash
sudo apt-get install build-essential meson ninja-build pkg-config \
  libgtk-4-dev libadwaita-1-dev libgtksourceview-5-dev \
  libsoup-3.0-dev libjson-glib-dev zlib1g-dev gettext zip unzip

meson setup build --prefix=/usr
meson compile -C build
./build/src/vellum
```

本机（用户目录不含 libadwaita-1-dev / libsoup-3.0-dev / libjson-glib-dev 系统包）将所需开发包解压在项目内的 **`.dev-deps`**（含 `pc/`、`root/`、`debs/`，已加入 `.gitignore` 不上传），构建时需显式提供 pkg-config 搜索路径：

```bash
export PKG_CONFIG_PATH=$PWD/.dev-deps/pc
meson setup --reconfigure --wipe build   # 依赖目录或 .pc 变化后需要
ninja -C build
./build/src/vellum
```

`po/LINGUAS` 声明了 `en` 与 `zh_CN`，因此 Meson 会把独立英语和简体中文翻译均编译为 `vellum.mo` 并安装到 `share/locale`。
直接从构建树运行时（如 `./build/src/vellum`），程序会优先使用 `VELLUM_LOCALEDIR` 环境变量，否则自动从可执行文件相对位置找到 `build/po` 中的翻译目录，因此未安装到系统也能加载界面语言。
界面语言在“首选项”中选择（系统语言、English 或简体中文），设置保存在 `~/.config/vellum/settings.ini` 的 `language` 项，重启后生效；显式选择 `zh_CN` 或 `en` 时，程序会取消 `LC_ALL` 覆盖，避免 `LC_ALL=C.UTF-8` 等环境导致 GNU gettext 忽略 `LANGUAGE` 而使翻译失效。首选项还提供系统字体选择器和字体缩放，以及行号、当前行高亮、概览图、右边界、自动折行、自动缩进、缩进列宽、会话恢复和“启用扩展”总开关。关闭扩展并重启后，应用不创建插件管理器、不会加载任何动态模块。拼写检查通过 **libspelling（GTK4 版）** 实现：当构建能找到 `libspelling-1` 时，“检查拼写”开关可用，拼错的单词显示波浪线，右键提供建议 / 忽略 / 加入词典 / 切换语言；默认语言跟随系统 locale，无对应词典时自动回退英语。构建还会生成 `build/src/vellum-tui-debug`，供在真实 GTK 窗口上执行受限的 JSON 行调试与回归测试。

## AppImage 测试包

先下载官方 `appimagetool`，再运行打包脚本：

```bash
chmod +x packaging/appimage/build-appimage.sh
APPIMAGETOOL=/absolute/path/to/appimagetool-x86_64.AppImage \
APPIMAGE_EXTRACT_AND_RUN=1 \
packaging/appimage/build-appimage.sh
```

产物是 `dist/Vellum-x86_64.AppImage`。如果目标环境无法挂载 FUSE，可在测试时使用：

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./dist/Vellum-x86_64.AppImage
```

| 验证项 | 本次状态 | 说明 |
|---|---:|---|
| Meson 配置与 C 编译 | 通过 | 已链接 GTK4、Libadwaita、GtkSourceView、GIO/GModule、libsoup 3 与 JSON-GLib |
| 英语与简体中文 `.mo` 安装 | 通过 | 检查 `en` 与 `zh_CN` 的独立 Gettext 资源 |
| GNOME 风格暗色重构 | 通过 | 移除固定紫黑表面 CSS，使用 Libadwaita 表面与 GtkSourceView `Adwaita-dark` 代码方案 |
| `.vut` 包管理 | 通过 | 应用内验证 ZIP 中央目录、CRC、清单 OS/架构/ABI/API 兼容性、受限路径与二进制/源码导入导出 |
| 源码 `.vut` 分发 | 通过 | 八个内置扩展均生成源码 ZIP 包；在隔离目录以清单声明的 `make plugin` 成功生成本机模块 |
| 扩展启停 | 通过 | 禁用状态保存于 `XDG_CONFIG_HOME/vellum/plugins.ini`；启动时禁用模块不激活，启用时重新注册其动作和快捷键 |
| 文档行为测试 | 通过 | 在 Xvfb 下验证 Tab 保留 U+0009、显示列宽设置、运行时 Python 内容识别、未闭合括号错误波浪线及代码模式智能配对 |
| AI 补全端到端 | 通过 | 默认使用本地 OpenAI 兼容模拟服务验证异步请求、`/v1` 基础地址自动补全、JSON 响应解析、Tab 接受与停用后的迟到回调；另以一次短 `deepseek-v4-flash` 请求验证 Chat Completions 和非思考模式兼容性。 |
| AI 覆盖层生命周期 | 通过 | 在真实 GtkTextView 窗口中验证候选显示、切换标签自动清理、显式取消和文档销毁路径，在 `G_DEBUG=fatal-warnings` 下无 GTK 警告。 |
| TUI 图形回归 | 通过 | `vellum-tui-debug` 默认不加载扩展；行协议可写入文本、驱动异步保存，并以 JSON 返回文档和面板状态。 |
| 链接测试端到端 | 通过 | 使用回环 HTTP 服务验证 `Ctrl+Shift+L` 扩展可汇总一个可达和一个不可达链接。 |
| AppImage 生成 | 通过 | 已重新生成 0.7.1 Linux x86_64 Type 2 AppImage；解包确认八个运行时插件、English/简体中文 `.mo`、原创 PNG 图标和 `vellum-tui-debug`。 |
| 主题选择器回归测试 | 通过 | 在 Xvfb 与 `G_DEBUG=fatal-warnings` 下打开“首选项 → 代码主题”，验证八张 GtkSourceView 预览卡片、Cobalt 选择及持久化均无 GTK/Adwaita 警告。 |
| 保存回归测试 | 通过 | 验证 GIO 异步替换写入的完成回调、文件内容与修改状态；同时通过实际窗口保存动作验证 Ctrl+S 路径在限定主循环内完成且不阻塞界面。 |
| 图形启动冒烟测试 | 通过 | 0.7.1 AppImage 在 Xvfb 与 `G_DEBUG=fatal-warnings` 下运行 7 秒；八个内置扩展均加载，未出现应用自身 GTK/GLib/Adwaita Critical、Warning 或 CSS 解析错误。Xvfb 的 DRI3/EGL 硬件警告属于虚拟显示环境噪声。 |

> **重要边界：** 此阶段的 AppImage 是可启动的测试包，包含程序、资源、翻译与插件，但为了避免将宿主图形栈硬塞入包内造成兼容性问题，尚未捆绑 GTK4/Libadwaita/GtkSourceView、libsoup 与 JSON-GLib 的全套共享库。它仅在构建环境中验证；若要发布面向不同发行版的完全自包含 AppImage，应在更旧的 Linux 基线中引入 `linuxdeploy`/容器化依赖收集并做跨发行版测试。

## Windows x86_64 交叉构建

交叉文件位于 `packaging/windows/x86_64-w64-mingw32.ini`，详细前置条件和命令见 [`packaging/windows/README.md`](packaging/windows/README.md)。当前环境没有经过验证的 Windows GTK4/Libadwaita/GtkSourceView MinGW sysroot，故本次提供**可审阅的交叉构建配置**，但不宣称已经产出或运行 Windows `.exe`。

## AI 补全安全说明

AI 补全是手动触发的扩展。用户必须在“扩展 → AI Completion → 配置”中填入完整 OpenAI 兼容服务 URL、模型和 API 密钥；扩展不会自动发现、启用或调用任何第三方服务。保存后，密钥存储在当前用户配置目录的 `vellum/ai-completion.ini`，文件权限被设为 `0600`。触发补全时只会发送光标前最多 8,000 个字符。

默认本地端到端测试由 `tests/test-ai-completion-request.c` 启动回环 OpenAI 兼容服务，并使用无效测试密钥；链接测试同样使用回环 HTTP 服务。额外进行了一次由用户明确授权的真实 `deepseek-v4-flash` 短请求兼容性检查：密钥只写入 0600 临时文件、未提交到工程、测试结束后已删除。除该次受控检查外，回归套件不涉及真实账号、真实密钥或外发真实文档内容。

## 自动化验证

在具有 X11/Xvfb 的 Linux 环境，可执行下列最小验证：

```bash
meson setup build --reconfigure
ninja -C build vellum-update-po
python3 tools/fill_po_translations.py
meson compile -C build
xvfb-run -a env GTK_A11Y=none GDK_DISABLE=gl G_DEBUG=fatal-warnings \
  meson test -C build --suite unit --print-errorlogs
```

AI 插件 ABI 回归位于 `tests/test-ai-completion-request.c`，默认由其内嵌回环 OpenAI 兼容服务响应；`tests/test-tui-debug.sh` 通过 `vellum-tui-debug` 驱动真实 GTK 核心窗口。两者在普通回归中均不使用真实网络凭据。

生成内置扩展的**源码**分发包并在本机构建验证：

```bash
packaging/vut/build-source-packages.sh
for package in dist/vut-source/*.vut; do
  work=$(mktemp -d)
  unzip -q "$package" -d "$work"
  (cd "$work/source" && make plugin)
  rm -rf "$work"
done
```

源码 `.vut` 包当前面向 Linux，清单使用 `architecture=any` 与 `abi=any` 以允许同一操作系统上的 x86_64、aarch64 等目标分别按本机环境重建。它们不提供 `.so` 跨 Windows/Linux/BSD 或跨发行版 ABI 的承诺；不同 OS 需要各自的工具链、依赖清单和验证。

## 原生插件安全说明

插件是与主程序同进程执行的本地 C 动态库。只应将受信任的模块放入以下目录：

```text
<安装前缀>/lib/vellum/plugins/
~/.local/share/vellum/plugins/
```

AppImage 会通过其启动器指向自身捆绑的插件目录。第三方插件具有与编辑器相同的用户权限，不应从不可信来源下载或加载。
