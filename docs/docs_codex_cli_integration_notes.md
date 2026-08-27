# Codex CLI 集成参考

来源：<https://learn.chatgpt.com/docs/codex/cli>（2026-08-27 访问）。

该官方文档将 Codex CLI 定义为可在终端中检查、编辑并运行代码的本地编码代理。文档明确列出三个与 Vellum 扩展相关的场景：在代码仓库中进行“检查—编辑—运行”的集中循环；在脚本或 CI 中执行非交互命令；在提交或创建拉取请求前进行本地代码审查。文档导航还列出了“Automate with codex exec”“Permissions”“Codex MCP”“Codex cloud”和“Codex IDE extension”等能力入口。

Vellum 的兼容扩展不应硬编码用户凭据或自行调用云端 API。应优先提供本地已登录 CLI 的显式命令适配、清晰的项目工作目录、命令预览和用户主动触发机制；涉及写入或执行的操作必须可被用户审阅和中止。
