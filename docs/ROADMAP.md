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
- 撤销/重做已接入：QTextDocument 默认 undoRedoEnabled 为 false，需显式开启；载入后必须 clearUndoRedoStacks(UndoAndRedoStacks)，否则撤销会越过载入基线把文档清空；脏标记由「序列化结果是否等于已保存快照」派生，撤销回到已保存内容即转干净。Qt 6.12 的 QTextDocument 没有 setMaximumUndoHistory，只有 availableUndoSteps 与 clearUndoRedoStacks，长文档的撤销历史内存上限需自己实现。
- 源码/渲染同视图切换已实现：View 菜单 Ctrl+/ 切换；源码模式写 toPlainText 并关闭 acceptRichText，切回时解析重建富文本，解析失败保留源码文本不丢字。切换会 clearUndoRedoStacks（切换不是编辑，撤销不能跨越），因此跨模式的撤销历史丢失，列为开放项。
- typing 路径已去掉序列化开销：contentsChanged 只置脏标记（近似判定），精确比较发生在保存、撤销、重做与视图切换；代价是打字改回原内容后仍显示未保存，撤销会纠正。
- 撤销历史上限已实现：editor::kUndoStepLimit=200，contentsChanged 时 availableUndoSteps() 超限就 clearUndoRedoStacks(UndoStack)；Qt 没有部分裁剪，只能整栈丢弃，超限后撤销不可用。
- HTML 导出走 cmark-gfm 的 GFM 扩展：core::toHtml 用 cmark_render_html(root, CMARK_OPT_UNSAFE, extensions)，扩展列表要与解析侧一致（cmark_markdown_to_html 不带扩展，表格/任务列表会退化），CMARK_OPT_UNSAFE 让 raw inline html（表格里的 <br>）保留；core::wrapHtmlDocument 加 UTF-8 charset 与中性默认样式。导出的是作者本机文档的 HTML，保留的原始 HTML 会执行脚本——只用于本地打开，不做网络服务。
- PDF 导出走 QPrinter 的 PdfFormat：platform::exportPdf(path, document, error) 打印传入的文档并校验 %PDF 头，失败时删除半成品；主窗口导出前用 editor::loadMarkdown 造一份 detached 渲染文档，因此 PDF 始终反映 Markdown 语义，不受当前视图模式影响。Qt 的 QTextDocument 排版不做分页控制（无页眉页脚、无自定义分页），要精细版式需换成 QPrintEngine 或自定义排版，列为后续项。
- MSYS2 的 Qt6 包其实带 PrintSupport（headers、Qt6PrintSupport.dll、lib/cmake/Qt6PrintSupport 都有），缺的只是 PrintTest；find_package 的 found 变量名是 Qt6PrintSupport_FOUND（Qt6 与组件名之间没有下划线），写成 Qt6_PrintSupport_FOUND 会恒为 false 并把实现降级成 stub。
- 主题层已实现：ui::Theme（Light/Dark）+ resolveTheme(preference, systemPrefersDark)，偏好 system/light/dark 存在 platform::Settings（QSettings("Typist","Typist")：Windows 走注册表，Linux/macOS 走 ini）；Ctrl+Shift+D 循环 system→light→dark 并写回配置。样式两份：QSS 给编辑器控件，QPalette 覆盖 Base/Text/Highlight（仅靠 QSS 不改文档内文本颜色），htmlStyle 贯穿到导出的 HTML，导出与编辑器观感一致。
- system 判定是启发式：Qt 没有显式 dark-mode API，用 QGuiApplication::palette().color(QPalette::Window).lightness() < 128 判断，并监听 paletteChanged 重新解析。三平台的原生 dark 检测（Windows 注册表值、macOS AppleInterfaceStyle）属后续项。
- 构造顺序教训：applyTheme() 会写 m_themeAction 的文字，必须在 action 创建之后调用（否则空指针崩溃）；保留 nullptr 守卫。
- MSVC 路线仍未核实：windows-msvc preset 保留，本机与 CI 都未验证过。
