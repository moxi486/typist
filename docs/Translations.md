# 界面翻译

- 在 **Settings > Language / 设置 > 语言** 中选择 **简体中文、English、跟随系统**。立即切换菜单、工具栏、状态栏和查找栏，保留编辑内容及撤销历史；重启后保留选择。
- 源语言是英语，界面文案用 `tr("...")`；MainWindow 的字符串覆盖与实际语言切换都有回归测试。
- `translations/typist_zh.ts` 必须覆盖 MainWindow 里每一个唯一的 `tr()` 字符串；CI 的 Translation coverage 步骤与本地 spike slot 都用同一个计数比对。
- 中文目录 `translations/typist_zh.ts` 作为 Qt 资源编入程序，通过 `AppTranslator` 读取；本地构建和便携包无需安装 Qt Linguist 或另放 `.qm` 文件。
- CI 仍会通过 `lrelease` 生成标准 `.qm` 产物。新增控件的 `tr()` 上下文在内置目录中使用类名（例如 `MainWindow`、`FindBar`）。
- 新增文案的流程：加 `tr()` -> 重新生成 `typist_zh.ts` 的条目（source/translation 成对）-> 跑 spike 的覆盖 slot。
