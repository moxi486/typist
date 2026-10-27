# 界面翻译

- 源语言是英语，所有 UI 文案用 `tr("...")`；目前只有 `src/ui/MainWindow.cpp` 参与一致性测试，`FindBar` 等控件的文案用 `QStringLiteral`。
- `translations/typist_zh.ts` 必须覆盖 MainWindow 里每一个唯一的 `tr()` 字符串；CI 的 Translation coverage 步骤与本地 spike slot 都用同一个计数比对。
- `.qm` 目录不在仓库里，由 CI 的 `lupdate -tr strict src/ui/*.cpp -ts translations/typist_zh.ts` 与 `lrelease translations/*.ts` 生成并作为 `translations` 产物上传；开发机没装 Qt Linguist，本地跑的是英语源文案。
- 运行时按 locale 找 `typist_<lang>.qm`，依次尝试当前目录 `translations/`、可执行文件旁的 `translations/`、Qt 翻译目录、`~/.typist/translations/`；找不到就回落英语。
- 新增文案的流程：加 `tr()` -> 重新生成 `typist_zh.ts` 的条目（source/translation 成对）-> 跑 spike 的覆盖 slot。
