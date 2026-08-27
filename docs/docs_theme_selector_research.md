# GNOME Text Editor 多主题研究记录

研究日期：2026-08-26。

| 参考点 | 观察 | Vellum 的独立实现策略 |
|---|---|---|
| `editor-theme-selector` | 主题模式明确分为跟随系统、浅色与深色，并监听系统深色状态变化。 | 保留 Vellum 既有外观偏好，并将代码配色方案独立为可持久化的主题选项；跟随系统时自动选择对应的浅/深变体。 |
| GtkSourceView 样式方案 | GNOME Text Editor 打包了成对的浅色/深色语言高亮方案，GtkSourceView 也提供 Adwaita、Adwaita-dark、Classic、Cobalt、Kate、Oblivion、Solarized 与 Tango 等方案。 | Vellum 只使用运行时由 GtkSourceView 安装和维护的方案 ID，不复制 GPLv3 项目中的主题 XML。 |
| 主题选择器布局 | 以紧凑、互斥的选择控件展示跟随系统/浅色/深色，外观依赖 Libadwaita 语义颜色。 | Vellum 在首选项中使用 Adwaita 原生控件显示“应用外观”和“代码主题”两组设置，不覆盖窗口表面或语法 token 颜色。 |

许可证边界：GNOME Text Editor 源码为 GPLv3。本次仅参考其公开的架构和交互原则；Vellum 不复制其源代码、样式表或主题资源，仍然使用自己的 BSD-2-Clause 许可证与系统 GtkSourceView 主题资产。
