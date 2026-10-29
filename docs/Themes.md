# 主题与配置

- `ui::Theme` 提供 light / dark / system 三套，`Ctrl+Shift+D` 循环；偏好写入 `platform::Settings`（Windows 注册表 `HKCU\Software\Typist\Typist`，Linux/macOS 走各自原生存储）。
- **设置 > 外观** 可以直接选择浅色、深色或跟随系统。界面采用 Typora 风格的简洁菜单和居中正文，常用操作归入菜单，底部提供侧栏、源码切换和字符数；文件与大纲在同一侧栏中切换。
- 编辑区保持约 860 逻辑像素的居中栏宽，标题分级字号、正文行距、引用竖线、代码底色和链接颜色共同呈现阅读层次。显示装饰不改写 Markdown，也不占用撤销历史。
- 首次启动窗口按屏幕大小设为最大 1180 × 820，关闭时记住尺寸和位置。编辑区留白以控件外部宽度计算并保护尺寸事件重入，避免宽窗口打开长文档或侧栏切换时崩溃。
- `system` 的判定顺序：Windows 读 `CurrentVersion/Themes/Personalize/AppsUseLightTheme`；Linux 读 `GTK_THEME` 或 `~/.config/gtk-3.0/settings.ini`；macOS 读 `AppleInterfaceStyle`；都读不到时回退窗口调色板亮度。macOS 与 Linux 分支未在本地实机验证。
- 编辑器样式来自主题 QSS，导出的 HTML 内嵌同一份样式，所以导出结果与屏幕一致。
- 配置项：主题、PDF 纸张（a4/letter）、页边距 mm、界面语言（system/en/zh）、窗口尺寸与位置。PDF 页脚中的 `#` 替换为页码。
- 设置菜单提供自动保存与括号/引号自动配对开关；视图菜单提供专注模式、打字机模式、全屏与缩放。专注与打字机模式保留选择；这些显示模式不修改 Markdown 文档或撤销历史。
- PDF 使用独立白底文档、11pt 正文和 Qt 原生分页；页眉、正文、页脚有独立裁剪范围。短代码块整体换页，表头跨页重复。DOCX 使用 11pt 中英文字体、分级标题、1.5 倍行距、段距和表格内边距。
- 崩溃草稿目录默认 `~/.typist/recovery`，可用环境变量 `TYPIST_RECOVERY_DIR` 覆盖；最近文件列表最多 8 条，存在原生存储里。
