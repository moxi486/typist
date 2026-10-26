# Typist 路线图

## 已定决策

- 首发形态：原生跨平台桌面（Windows / Linux / macOS），硬约束为高性能与低资源占用，据此排除 Electron 与 Tauri。
- 内核：Qt 6 Widgets + QTextDocument + cmark-gfm；ProseMirror + remark 已被取代（JS 库只能运行在 WebView 内，与原生约束冲突）。
- 插件：不做插件系统，对外只开放主题与配置。
- Qt 基线 6.8 LTS，目标 6.12 LTS；Qt 6.5 已于 2026-04 结束支持。

## 里程碑

| 阶段 | 周期 | 关键任务 | 交付与验收 |
| --- | --- | --- | --- |
| M0 骨架 | 2 周 | 仓库与 CI 成型、Qt 依赖落地、cmark-gfm 解析打通、WYSIWYG 可行性 spike | 三平台可编译可跑的空壳；spike 结论决定表格与代码块路线 |
| MVP 0.1 | 6 周 | 标题/段落/列表/代码块/链接/图片/粗斜体/删除线/任务列表编辑与渲染；撤销重做；文件读写与最近列表；主题与设置；导出 HTML/PDF | 往返保真通过语料测试；四项性能预算达标 |
| Beta 0.2-0.9 | 6 周 | 文件树与全文搜索、自动保存与冲突提示、粘贴 Markdown、目录 TOC、拖放图片、DOCX 导出、中英双语、增量渲染、崩溃恢复 | 全量测试绿；内存与打开耗时不劣化；双语翻译完整 |
| RC 与 1.0 | 4 周 | 三平台打包（msi/dmg/AppImage/deb/rpm）、签名、自动更新、无障碍、文档站、许可合规 | GitHub Release 可下载可自动更新；许可声明齐备 |

## 进行中假设

- MVP 导出只含 HTML 与 PDF；DOCX 归 Beta。
- MVP 为单文件 + 最近文件列表；文件树与全文搜索归 Beta。
- 数学公式与插件协议不进首发。
- UI 中英双语走 Qt Linguist 的 .ts 文件。

## 开放项（M0 内必须核实）

- Windows 构建路线已在本机核实：MSYS2 mingw64（gcc 16.2、CMake 4.4.4、Qt 6.12.0 来自 pacman 的 mingw-w64-x86_64-qt6-base）。cmark-gfm 需从 github/cmark-gfm 的 tag 0.29.0.gfm.13 源码构建并安装到 /usr/local，MSYS2 仓库只有不带 GFM 扩展的 cmark。ci.yml 的 windows job 已改为常规触发（msys2/setup-msys2@v2）。
- cmark-gfm 的围栏语言标签已核实：0.29.0.gfm.13 没有信息串读取接口（cmark_node_get_string_content 对围栏块返回空），改由节点起始行扫描围栏行取得标签（src/core/markdown.cpp fenceInfo），文档侧用块属性 kCodeInfoProperty 把标签带到首块再输出，语料 tests/corpus/code_info.md 在 core 与文档往返均覆盖。Qt 的 emit 关键字会占用同名标识符，局部变量不能叫 emit。
- 表格双向保真已打通：QTextTable API 核实为 insertTable + QTextTableFormat::setColumns + cellAt().firstCursorPosition()（无 cellTextCursor、无 QTextDocument::findTable、QTextBlockFormat 无 list()，改用 QTextBlock::textList()）；文档侧序列化靠 QTextFrame 的 firstPosition/lastPosition 定位表格（表格起止块不带 TableObject 类型，只有单元格块带 TableCellObject），单元格内容用 firstPosition 到 lastPosition 的选区读取（QTextTableCell 没有 text()），Qt 6 的 MoveMode 只有 MoveAnchor/KeepAnchor（无 KeepSelection）。tests/editor 现断言文档侧输出的表格与 AST 往返等值。单元格转义已实现：'|' 输出为 \|，换行输出为 <br>（inline html 在 flattenInline 里按字面保留，这也是多行单元格的唯一可行写法），合并单元格只保留左上格内容、被覆盖的格子留空；语料 tests/corpus/gfm/table_escapes.md 覆盖，core 与文档侧往返等值。仍开放：Markdown 语法本身没有合并单元格，编辑器里的合并操作需显式降级为普通表格。
- CMake 的 set_tests_properties ENVIRONMENT 用 ';' 分隔键值对：PATH 内的多个目录必须写成 \;，其它变量用普通 ';'；写错会让 Windows 侧测试在加载 DLL 时退到 0xc0000135。Qt 与 GCC 运行时目录从 Qt6_DIR 推导，cmark 运行时目录由 FindCmarkGfm 给出，不依赖 shell 的 PATH。
- 自定义 QTextFormat 属性 id 必须高于 QTextFormat::UserProperty（0x100000），旧的 0x4001 区间会撞 Qt 内部枚举；已改为 0x100001 起。
- 嵌套列表缩进按标记宽度输出；语料 tests/corpus/nested_wide.md（9. 与 10. 混排）在 core 与文档往返均通过：文档侧序号按连续计数重编，astEqual 不比较列表起始序号，缩进按标记宽度计算。
- MSVC 路线仍未核实：windows-msvc preset 保留，本机与 CI 都未验证过。
