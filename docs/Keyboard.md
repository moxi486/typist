# 快捷键

| 操作 | 快捷键 |
| --- | --- |
| 新建 / 新建窗口 / 关闭 | Ctrl+N / Ctrl+Shift+N / Ctrl+W |
| 快速打开当前目录文件 | Ctrl+P |
| 打开 / 保存 / 另存为 | Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| 导出 HTML / PDF / DOCX | Ctrl+Shift+E / Ctrl+Shift+P / Ctrl+Shift+X |
| 撤销 / 重做 | Ctrl+Z / Ctrl+Y（macOS Ctrl+Shift+Z） |
| 查找 / 下一个 / 上一个 / 关闭 | Ctrl+F / Enter / Shift+Enter / Esc（F3 与 Shift+F3 也能步进） |
| 源码与渲染视图切换 | Ctrl+/ |
| 粘贴为 Markdown | Ctrl+Shift+V |
| 复制为 Markdown / 粘贴为纯文本 | Ctrl+Shift+C / Ctrl+Alt+Shift+V |
| 替换（支持正则捕获组 `$1`、`\1`） | Ctrl+H |
| 标题 1–6 / 正文 | Ctrl+1…6 / Ctrl+0 |
| 加粗 / 斜体 / 删除线 | Ctrl+B / Ctrl+I / Alt+Shift+5 |
| 行内代码 / 清除格式 | Ctrl+Shift+反引号 / Ctrl+反斜杠 |
| 超链接 / 图片 | Ctrl+K / Ctrl+Shift+I |
| 打开渲染视图中的链接 | Ctrl+单击（macOS Cmd+单击） |
| 代码块 / 引用 | Ctrl+Shift+K / Ctrl+Shift+Q |
| 代码块缩进 / 取消缩进（支持多行选区） | Tab / Shift+Tab |
| 退出代码块、继续正文 | Ctrl+Enter |
| 有序列表 / 无序列表 | Ctrl+Shift+[ / Ctrl+Shift+] |
| 列表缩进 / 取消缩进 | Tab / Shift+Tab（也可 Ctrl+[ / Ctrl+]） |
| 切换当前 / 所选任务状态 | Ctrl+Shift+Space |
| 列表项内换行 | Shift+Enter |
| 插入表格 | Ctrl+T |
| 表格下一格 / 上一格 | Tab / Shift+Tab；末行最后一格 Tab 新增一行 |
| 退出表格，继续正文 | Ctrl+Enter |
| 矩形表格选区复制 / 剪切 / 清空 | Ctrl+C / Ctrl+X / Delete（或 Backspace） |
| 专注模式 / 打字机模式 / 全屏 | F8 / F9 / F11 |
| 放大 / 缩小 / 实际大小 | Ctrl+Shift+= / Ctrl+Shift+- / Ctrl+Shift+0 |
| 主题循环（light/dark/system） | Ctrl+Shift+D |
| 文件侧栏开关 / 打开文件夹 | Ctrl+Shift+L / Ctrl+Shift+O |
| 目录内搜索 | Ctrl+Shift+F |
| 大纲面板开关 / 文件树 | Ctrl+Shift+T 或 Ctrl+Shift+1 / Ctrl+Shift+3 |
| 恢复崩溃草稿 | Ctrl+Shift+R（菜单 File > Recover Draft） |
| 检查更新 | Ctrl+Shift+U（菜单 Help > Check for Updates） |

导出 HTML 占用 Ctrl+Shift+E，恢复草稿用 Ctrl+Shift+R，两者不再冲突（此前它们共用 Ctrl+Shift+E，后注册的那个会吃掉前一个的按键）。

渲染视图中输入 `# `、`## `、`- `、`1. `、`> `、`- [ ] ` 会转换为对应段落。输入三个反引号和可选语言名后按 Enter 创建代码块；空代码行再次 Enter 可退出，Shift+Enter 保留空代码行。嵌套列表空项 Enter 退一级，顶层空项 Enter 退出列表；Tab / Shift+Tab 携带子项调整层级。详见 [Lists.md](Lists.md)。

闭合的 `**粗体**`、`*斜体*`、`~~删除线~~`、行内代码和链接会就地渲染，转换可以撤销。普通粘贴自动解析多行 Markdown；纯文本粘贴保留输入原文。底部字符数可点击打开文档或选区统计。

`Ctrl+Shift+M` 插入独立数学公式；「格式 → 行内公式」插入行内公式。闭合的 `$…$` 与 `[^id]` 引用可就地渲染；`[TOC]` 后按 Enter 创建自动目录。双击公式或 Mermaid 图表打开源码与实时预览。其他语法边界见 [Advanced-Markdown.md](Advanced-Markdown.md)。

`Ctrl+Enter` 编辑当前正文段落或标题的 Markdown；在就地源码框中再次 `Ctrl+Enter` 应用、`Esc` 取消；在代码块或表格中则退出当前块并继续正文。代码块工具条可选择语言、复制整块原文和退出；语言也可在「段落 → 代码语言」修改，高亮开关在「设置 → 语法高亮」。代码块支持 Enter 自动缩进、Tab / Shift+Tab 多行缩进、缩进 Backspace 和智能 Home。详见 [Writing-Experience.md](Writing-Experience.md)；表格选区及电子表格粘贴见 [Tables.md](Tables.md)。

链接编辑、相对文件跳转与导出锚点见 [Links.md](Links.md)。
