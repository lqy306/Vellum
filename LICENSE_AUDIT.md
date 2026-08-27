# Vellum 许可证与来源审计

**审计日期：** 2026-08-26  
**目标许可证：** BSD-2-Clause  
**审计范围：** Vellum 的 `src/`、`data/`、`po/`、`packaging/`、`tests/`、`.vut` 源码扩展包、项目文档，以及 Linux AppImage 的打包脚本。

> 本文档是工程来源记录，不构成法律意见。发布者在引入新的第三方代码、图片、字体、主题 XML 或预编译二进制前，仍应单独核验相应许可证。

## 审计结论

Vellum 的可交付源码、原创 SVG/PNG 图标、翻译文件、构建脚本和测试代码均按 **BSD-2-Clause** 发布。审计未发现任何 GPL 或 AGPL 源代码、主题 XML、CSS、图标或二进制被复制进 Vellum 源树或源码归档。

GNOME Text Editor 与 mini-text 的公开项目均采用 GPL-3.0 [1] [5]。本项目仅在 Vellum 工程树外的隔离参考目录中审阅其模块命名、目录边界和许可文本，并在 `docs/docs_reference_architecture_study.md` 记录了“仅借鉴分层原则”的独立实现约束。参考目录、源码、截图、UI 模板、CSS、主题 XML、图标、翻译和测试均从未进入 Vellum 工程树、源码归档或 AppImage。Vellum 没有复制 GNOME Text Editor 或 mini-text 的函数、UI 模板、CSS、GtkSourceView 样式方案 XML 或图标资源。

| 项目部分 | 来源与处理方式 | 许可证状态 |
|---|---|---|
| `src/` 与 `src/plugins/` | Vellum 独立 C 实现；包括 `.vut` ZIP/CRC 处理、扩展启停和总加载开关、Vi 输入与命令模式、项目/构建扩展、GtkSourceView 首选项、主题预览卡片、GIO 异步保存、外部文件监视、AI 覆盖层、受限 TUI 调试入口与独立 Build & Run 工具窗口；使用 GTK、GIO、Libadwaita、GtkSourceView、libsoup 和 JSON-GLib 的公开 API。 | BSD-2-Clause |
| `data/io.github.vellum.Vellum.svg` 与 `data/icons/` | Vellum 原创的文档、墨线与笔尖图标；新增 PNG 仅作为桌面/AppImage 多分辨率资源。 | BSD-2-Clause |
| `po/` | Vellum 原创英文与简体中文界面翻译。 | BSD-2-Clause |
| `tests/` | Vellum 本地文档行为测试、异步保存及窗口保存动作测试、真实 GtkTextView AI 覆盖层生命周期测试、受限 TUI 保存回归、`.vut` ZIP/源码构建测试、插件 ABI 测试宿主、主题卡片选择/持久化测试与回环 HTTP 模拟服务。 | BSD-2-Clause |
| `packaging/vut/` 与 `dist/vut-source/` | Vellum 自写的源码扩展包生成脚本及由自有插件源码生成的 `.vut` ZIP 归档；不包含第三方模块二进制。 | BSD-2-Clause |
| GNOME Text Editor 与 mini-text | 只进行隔离参考；不进入交付物，不复制源代码、样式表、UI、图标、翻译或主题资源。 | GPL-3.0，未包含 [1] [5] |
| GTK | 运行时动态链接的系统库。 | GNU Library General Public License v2 [2] |
| Libadwaita | 运行时动态链接的系统库。 | LGPL-2.1 [3] |
| GtkSourceView | 运行时动态链接的系统库；主题 XML 不被打包。 | LGPL-2.1 [4] |
| libsoup 与 JSON-GLib | AI/链接插件运行时动态链接的系统库；当前 AppImage 不打包其共享库。 | 各自上游许可证；不属于 Vellum 自有源码 |

## 独立实现与 GPL 隔离措施

Vellum 的“跟随系统 / 浅色 / 深色”外观设置、多套代码主题映射、主题预览卡片、Tab 显示宽度、概览图、运行时语言识别、核心/扩展启动边界、受限 TUI 调试协议、扩展 ABI、`.vut` 包格式、Vi 输入模式、项目/构建扩展、AI 补全、链接测试和代码诊断均由 Vellum 独立实现。主题名称仅是运行时字符串，用于查询 GtkSourceView 在目标系统提供的已安装样式方案；预览在运行时用公开 GtkSourceView API 对示例 C 文本着色，不携带或复制主题文件。

项目不会分发 GNOME Text Editor 的 GPL CSS、UI 模板、源代码、主题 XML、图标、翻译或构建文件。若未来拟引入任何 GPL/AGPL 资产，应在合并前移除或根据独立功能规格重新实现；不得将其作为 BSD-2-Clause Vellum 源码的一部分发布。

## 运行时依赖与分发说明

BSD-2-Clause 适用于 Vellum 自有源码、`.vut` 清单、扩展源码包与生成脚本，并不改变外部库各自的许可证。源码 `.vut` 在目标机器构建扩展时会链接该机器安装的 GTK/GLib 等开发依赖；因此使用者也须遵守这些依赖在其构建和二进制分发场景中的许可证要求。当前测试 AppImage 不捆绑 GTK、Libadwaita、GtkSourceView、libsoup 或 JSON-GLib 的共享库，而是使用宿主系统库。因此，源码归档不包含这些第三方库的代码或主题资源。

若未来发布一个捆绑上述库的完全自包含 AppImage、Windows 安装包或其他二进制发行物，发行者需要遵守被捆绑库的许可证通知、替换/重链接和源码提供义务。对于 LGPL 运行时库，优先保持动态链接、保留独立库文件，并在发行材料中提供相应许可证与获取源码的方式。

## 持续审计清单

| 引入内容 | 发布前要求 |
|---|---|
| 第三方 C/C++/Rust/Python 源码 | 记录原始 URL、作者、许可证和允许的再许可范围。 |
| 主题 XML、CSS、图标、字体、截图 | 验证资产许可证；不得从 GPL 项目复制到 BSD-2-Clause 源树。 |
| 插件样例或模板 | 使用 Vellum 自写示例，或仅采用 BSD/MIT/ISC/Apache-2.0 等兼容来源并保留通知。 |
| 二进制依赖 | 将许可证、版权通知和 LGPL/GPL 源码获取信息随二进制发行物提供。 |

## References

[1]: https://gitlab.gnome.org/GNOME/gnome-text-editor/-/blob/main/COPYING "GNOME Text Editor COPYING — GPL-3.0-only"
[2]: https://gitlab.gnome.org/GNOME/gtk/-/blob/main/COPYING "GTK COPYING — GNU Library General Public License v2"
[3]: https://gitlab.gnome.org/GNOME/libadwaita/-/blob/main/COPYING "Libadwaita COPYING — LGPL-2.1"
[4]: https://github.com/GNOME/gtksourceview/blob/master/COPYING "GtkSourceView COPYING — LGPL-2.1"
[5]: https://github.com/Nokse22/mini-text/blob/master/COPYING "mini-text COPYING — GPL-3.0"
