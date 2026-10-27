# 主题与配置

- `ui::Theme` 提供 light / dark / system 三套，`Ctrl+Shift+D` 循环；偏好写入 `platform::Settings`（Windows 注册表 `HKCU\Software\Typist\Typist`，Linux/macOS 走各自原生存储）。
- `system` 的判定顺序：Windows 读 `CurrentVersion/Themes/Personalize/AppsUseLightTheme`；Linux 读 `GTK_THEME` 或 `~/.config/gtk-3.0/settings.ini`；macOS 读 `AppleInterfaceStyle`；都读不到时回退窗口调色板亮度。macOS 与 Linux 分支未在本地实机验证。
- 编辑器样式来自主题 QSS，导出的 HTML 内嵌同一份样式，所以导出结果与屏幕一致。
- 配置项：主题、自动保存开关、PDF 纸张（a4/letter）、页边距 mm、页眉文本、页脚文本（`#` 替换为页码）、界面语言（跟随 locale，英语为源语言）。
- 崩溃草稿目录默认 `~/.typist/recovery`，可用环境变量 `TYPIST_RECOVERY_DIR` 覆盖；最近文件列表最多 8 条，存在原生存储里。
