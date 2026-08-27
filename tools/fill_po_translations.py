#!/usr/bin/env python3
"""Fill Vellum's untranslated English and Simplified Chinese PO entries."""

import ast
import json
from pathlib import Path

ZH = {
    "Vellum Extension Package (.vut)": "Vellum 扩展包（.vut）",
    "Keyboard Shortcuts": "键盘快捷键",
    "Editing": "编辑",
    "Find and replace": "查找和替换",
    "Zoom in / out / reset": "放大 / 缩小 / 重置",
    "Insert timestamp": "插入时间戳",
    "Document statistics": "文档统计",
    "Build / Run / Build and run": "构建 / 运行 / 构建并运行",
    "Vim mode": "Vim 模式",
    "Enable the extension; use i for insert and Escape for normal mode": "启用此扩展；按 i 进入插入模式，按 Escape 返回普通模式",
    "Display": "显示",
    "Show line numbers": "显示行号",
    "Highlight current line": "高亮当前行",
    "Show overview map": "显示概览图",
    "A compact map is displayed beside each document": "在每个文档旁显示紧凑概览图",
    "Check spelling": "检查拼写",
    "Unavailable: this build has no system spelling backend": "不可用：此构建未包含系统拼写后端",
    "Wrapping and Indentation": "折行和缩进",
    "Show right margin": "显示右边界",
    "Right margin column": "右边界列",
    "Indent width": "缩进宽度",
    "Behavior": "行为",
    "Restore previous session": "恢复上一会话",
    "Restore recoverable drafts when Vellum starts": "Vellum 启动时恢复可恢复的草稿",
    "Extension loading will change the next time Vellum starts": "扩展加载状态将在下次启动 Vellum 时变更",
    "Enable extensions": "启用扩展",
    "When disabled, Vellum starts as a compact core editor; restart to apply": "关闭后，Vellum 将以紧凑的核心编辑器启动；重启后生效",
    "Extensions are disabled. Enable them in Preferences and restart Vellum to load or manage extensions.": "扩展已关闭。请在“首选项”中启用扩展并重启 Vellum，之后才能加载或管理扩展。",
    "Extension loading is disabled for this session": "此会话已关闭扩展加载",
    "Extensions can be disabled without deleting them. Native modules and source packages must come from trusted sources.": "扩展可在不删除的情况下禁用。原生模块和源码包必须来自可信来源。",
    "Extensions can be disabled without deleting them. Imported user extensions have a trash button for deletion; built-in extensions can be disabled but remain part of the application. Native modules and source packages must come from trusted sources.": "扩展可在不删除的情况下禁用。用户导入的扩展可用垃圾桶按钮删除；内置扩展可以禁用，但仍属于应用程序的一部分。原生模块和源码包必须来自可信来源。",
    "Command failed to start or finish: %s": "命令无法启动或完成：%s",
    "Command exited with status %d": "命令以状态 %d 退出",
    "Build completed": "构建完成",
    "Program exited successfully": "程序成功退出",
    "A build or run command is already active": "已有构建或运行命令正在执行",
    "Save the current source file before building or running": "请先保存当前源文件，再构建或运行",
    "Configure a build command first": "请先配置构建命令",
    "Configure a run command first": "请先配置运行命令",
    "Building": "正在构建",
    "Running": "正在运行",
    "Command stop requested\n": "已请求停止命令\n",
    "Build & Run": "构建与运行",
    "Build": "构建",
    "Run": "运行",
    "Build + Run": "构建并运行",
    "Stop Active Command": "停止当前命令",
    "Clear Output": "清除输出",
    "Hide Build Output": "隐藏构建输出",
    "Commands use direct argument parsing, not a shell. Available placeholders: ${file}, ${dir}, ${root}.\n": "命令使用直接参数解析而非 shell。可用占位符：${file}、${dir}、${root}。\n",
    "Build & Run Settings": "构建与运行设置",
    "Explicit Commands": "显式命令",
    "Commands run only after F9, F10 or F11. They are parsed into arguments without a shell. Use ${file}, ${dir}, or ${root}; quote placeholders when paths may contain spaces.": "命令只会在按下 F9、F10 或 F11 后执行。命令会被解析为参数而不经过 shell。使用 ${file}、${dir} 或 ${root}；当路径可能包含空格时请给占位符加引号。",
    "Working Directory (optional)": "工作目录（可选）",
    "Build Command": "构建命令",
    "Run Command": "运行命令",
    "[Unclosed code block]": "[未闭合的代码块]",
    "No project folder selected": "尚未选择项目文件夹",
    "Project sidebar is limited to the first 600 items": "项目侧边栏最多显示前 600 项",
    "Choose Project Folder": "选择项目文件夹",
    "Refresh Project Files": "刷新项目文件",
    "Hide Project Sidebar": "隐藏项目侧边栏",
    "Document": "文档",
    "Open files": "打开文件",
    "Close document": "关闭文档",
    "AI completion": "AI 补全",
    "Optional Extensions": "可选扩展",
    "Test links": "测试链接",
    "Automatic word wrap": "自动折行",
    "Automatic indentation": "自动缩进",
    "Unable to change extension state: %s": "无法更改扩展状态：%s",
    "Unable to parse command: %s": "无法解析命令：%s",
    "Unable to start command: %s": "无法启动命令：%s",
    "Build and run settings saved": "构建与运行设置已保存",
    "Unable to save build settings: %s": "无法保存构建设置：%s",
    "Save Build & Run Settings": "保存构建与运行设置",
    "Unable to save project folder: %s": "无法保存项目文件夹：%s",
    "Unable to choose project folder: %s": "无法选择项目文件夹：%s",
    "Project Files": "项目文件",
    "Enable extension": "启用扩展",
    "Export extension": "导出扩展",
    "Configure extension": "配置扩展",
    "Delete extension": "删除扩展",
    "Vim: Normal mode": "Vim：普通模式",
    "Vim: Insert mode": "Vim：插入模式",
    "Vim mode enabled: press i to insert and Escape for normal mode": "Vim 模式已启用：按 i 进入插入模式，按 Escape 返回普通模式",
    "Choose Code Theme": "选择代码主题",
    "Code Themes": "代码主题",
    "Choose a color scheme for source code. The preview uses the same GtkSourceView scheme as the editor.": "选择源代码配色方案。预览使用与编辑器相同的 GtkSourceView 方案。",
    "Theme colors are provided by installed GtkSourceView schemes; Vellum does not copy third-party theme assets.": "主题颜色由已安装的 GtkSourceView 方案提供；Vellum 不复制第三方主题资源。",
    "Saving…": "正在保存…",
    "Saving is already in progress": "保存正在进行中",
    "Zoom reset to 100%": "缩放已重置为 100%",
    "Zoom in": "放大",
    "Zoom out": "缩小",
    "Reset zoom": "重置缩放",
    "Set Keyboard Shortcut": "设置键盘快捷键",
    "Change shortcut": "更改快捷键",
    "Press a new shortcut": "请按下新的快捷键",
    "Press Escape to cancel. The change applies immediately and is saved for the next start.": "按 Escape 取消。更改会立即生效，并在下次启动时保留。",
    "Project sidebar": "项目侧边栏",
    "Build and run": "构建并运行",
    "File changed on disk": "磁盘上的文件已更改",
    "The file was changed by another program.": "文件已被另一个程序更改。",
    "Discard Changes and Reload": "丢弃更改并重新载入",
    "Dismiss external file change notification": "关闭外部文件更改通知",
    "Reloading file from disk…": "正在从磁盘重新载入文件…",
    "AI completion accepted": "已接受 AI 补全",
    "AI completion ready: press Tab to accept or Escape to dismiss": "AI 补全已就绪：按 Tab 接受，按 Escape 取消",
    "Vi: Normal mode": "Vi：普通模式",
    "Vi: Insert mode": "Vi：插入模式",
    "Vi Mode": "Vi 模式",
    "Vi Command": "Vi 命令",
    "w, wq, x, q, q!, help": "w、wq、x、q、q!、help",
    "Vi commands: :w, :wq, :x, :q, :q!, :help": "Vi 命令：:w、:wq、:x、:q、:q!、:help",
    "Unsupported Vi command: :%s": "不支持的 Vi 命令：:%s",
    "Vi mode enabled: press i to insert, Escape for normal mode, and : for commands": "Vi 模式已启用：按 i 进入插入模式，按 Escape 返回普通模式，按 : 输入命令",
    "Save an untitled document with :w before closing": "关闭前请使用 :w 保存未命名文档",
    "Saving before closing…": "正在保存后关闭…",
    "Build + Run": "构建并运行",
}


def quoted_value(line: str, prefix: str) -> str:
    return ast.literal_eval(line[len(prefix):].strip())


def entry_msgid(lines):
    for index, line in enumerate(lines):
        if line.startswith("msgid "):
            value = quoted_value(line, "msgid ")
            index += 1
            while index < len(lines) and lines[index].startswith('"'):
                value += ast.literal_eval(lines[index].strip())
                index += 1
            return value
    return None


def fill(path: Path, mapping, fallback_to_id=False):
    text = path.read_text(encoding="utf-8")
    chunks = text.split("\n\n")
    output = []
    for chunk in chunks:
        lines = chunk.splitlines()
        msgid = entry_msgid(lines)
        value = mapping.get(msgid) if msgid is not None else None
        if value is None and fallback_to_id and msgid:
            value = msgid
        fuzzy = any("fuzzy" in line for line in lines)
        if fuzzy and fallback_to_id and msgid:
            value = msgid
        if value is not None:
            lines = [line for line in lines if line.strip() != "#, fuzzy"]
            for index, line in enumerate(lines):
                if line.startswith("#, fuzzy,"):
                    lines[index] = line.replace("fuzzy, ", "")
                if line.startswith("msgstr "):
                    lines[index] = "msgstr " + json.dumps(value, ensure_ascii=False)
                    break
        output.append("\n".join(lines))
    path.write_text("\n\n".join(output), encoding="utf-8")


root = Path(__file__).resolve().parents[1]
fill(root / "po" / "en.po", {}, fallback_to_id=True)
fill(root / "po" / "zh_CN.po", ZH)
