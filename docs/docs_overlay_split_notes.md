# OverlaySplitView 布局参考

来源：<https://gnome.pages.gitlab.gnome.org/libadwaita/doc/1.4/class.OverlaySplitView.html>（2026-08-27 访问）。

`AdwOverlaySplitView` 在未折叠时将侧栏和内容并排显示；`collapsed=TRUE` 时将侧栏覆盖在内容之上。`show-sidebar` 控制侧栏可见性。官方说明建议配合 `AdwBreakpoint`，在小宽度阈值把 `collapsed` 改为 `TRUE`。若 `pin-sidebar=TRUE`，折叠或展开时不会自动隐藏或显示侧栏，因此 Vellum 需要保留该项以维持用户通过“文档属性”按钮作出的显式可见性选择。

默认尺寸语义为 25% 宽度、180sp 最小、280sp 最大。Vellum 本轮将检查器最小和最大宽度都设为 300px，保证视觉边界稳定；并应在可用宽度较小时启用 `collapsed`，令其成为不压缩编辑区的右侧抽屉。侧栏可使用触控边缘滑动显示与隐藏，但桌面鼠标/触控板操作仍以标题栏按钮为主。
