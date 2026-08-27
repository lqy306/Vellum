# GNOME Text Editor 主题研究记录

研究日期：2026-08-26

| 来源 | 关键观察 | 对 Vellum 的处理 |
|---|---|---|
| GNOME Text Editor 官方 GitLab 项目 | 项目使用 GNU GPLv3；其视觉方向建立于 GTK/Libadwaita 与 GtkSourceView 之上。 | 仅研究界面原则和资源使用方式；不复制其 GPLv3 源码、CSS 或资源，以保持 Vellum 的 BSD-2-Clause 许可证边界。 |
| GNOME GtkSourceView 官方源码的 `data/styles` 目录 | 官方提供 `Adwaita.xml` 与 `Adwaita-dark.xml` 两个语法方案；维护记录表明深色方案包含当前行等细节优化。 | 不再用自定义的深色文字/背景 CSS 覆盖语法 token 颜色；使用系统 GtkSourceView 的 Adwaita/Adwaita-dark 方案渲染代码。 |

重做目标：让 Libadwaita 负责窗口、标题栏、标签和控件表面；让 GtkSourceView 的 Adwaita-dark 负责编辑器纸面、当前行、选择和语法 token；Vellum 只保留必要的单色字体尺寸 CSS，以避免截图中出现的大片非 Adwaita 紫黑色覆盖层。

参考地址：

1. https://gitlab.gnome.org/GNOME/gnome-text-editor
2. https://github.com/GNOME/gtksourceview/tree/master/data/styles

参考项目的 `src/style.css` 还显示出如下不复制代码的设计原则：主题选择控件只使用 Libadwaita 语义颜色（边框、选中背景/前景）；状态栏使用 `@borders` 形成细分隔线；搜索工具按钮保持紧凑；编辑视图本身不被大面积固定颜色覆写。Vellum 将以自己的简短 CSS 实现相同原则：移除固定紫黑背景和白色文本覆盖，让 `Adwaita-dark` 完整提供编辑器背景与 token 色；只对状态栏使用语义化边框和间距；让标题栏、菜单、标签继续完全遵循 Libadwaita。
