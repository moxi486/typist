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
- 源码/渲染切换改为两份文档（ui::ViewSwitch）：渲染文档与纯文本文档各持一份撤销历史，切换时若两侧 AST 相同就不重建，历史因此跨切换保留；AST 不同才重建并清空目标栈（注入内容是新基线，旧撤销命令的位置已失效）。跨模式撤销对方模式的编辑仍不可表示，属已知限制。
- 纯文本文档不能用 serializeDocument 取 Markdown：QTextDocument::setPlainText 把每一行（含空行）切成独立 block，序列化会凭空插入空行，AST 与原文不一致（探针 plain_ast_equal=0）；源码视图的内容一律取 toPlainText()。
- 切换前的判定要按 AST 而非字符串：渲染文档的序列化结果与源文件文本在空行数上本就不相同（1MB 文件 1005000 字节序列化 810559 字节），字符串比较会误判成需要重建从而丢历史。
- bench 模式（--bench-startup / --bench-repaint / --bench-view-switch）会关掉自动保存触发与强制落盘：合成按键否则会把被测文件改写（曾把 1MB 测试文件写成 975003 字节并丢失换行）。，切回时解析重建富文本，解析失败保留源码文本不丢字。切换会 clearUndoRedoStacks（切换不是编辑，撤销不能跨越），因此跨模式的撤销历史丢失，列为开放项。
- typing 路径已去掉序列化开销：contentsChanged 只置脏标记（近似判定），精确比较发生在保存、撤销、重做与视图切换；代价是打字改回原内容后仍显示未保存，撤销会纠正。
- 撤销历史上限已实现：editor::kUndoStepLimit=200，contentsChanged 时 availableUndoSteps() 超限就 clearUndoRedoStacks(UndoStack)；Qt 没有部分裁剪，只能整栈丢弃，超限后撤销不可用。
- HTML 导出走 cmark-gfm 的 GFM 扩展：core::toHtml 用 cmark_render_html(root, CMARK_OPT_UNSAFE, extensions)，扩展列表要与解析侧一致（cmark_markdown_to_html 不带扩展，表格/任务列表会退化），CMARK_OPT_UNSAFE 让 raw inline html（表格里的 <br>）保留；core::wrapHtmlDocument 加 UTF-8 charset 与中性默认样式。导出的是作者本机文档的 HTML，保留的原始 HTML 会执行脚本——只用于本地打开，不做网络服务。
- PDF 导出改用 QtGui 的 QPdfWriter（不再需要 PrintSupport 模块）：setPageSize(QPageSize::A4/Letter) + setPageMargins(QMarginsF, QPageLayout::Millimeter) + pageLayout().paintRectPixels(resolution)，写分辨率固定 96dpi 让文档像素与设备像素一致；分页按 blockBoundingRect 累计高度切页并逐页 writer.newPage()，页眉/页脚各留 18px 带，页脚模板里的 # 替换为页码。
- 文档内查找走 ui::SearchController（QTextDocument::find + FindFlags）：flag 名是 FindCaseSensitively（不是 FindCaseSensitivity）；find 返回选中匹配的游标，找不到返回 null 游标，从文档末尾之后搜索也返回 null（探针：from=27 isNull=1）。
- 查找两端循环语义：向前步进越过最后一个匹配回到第一个；向后步进在第一个匹配之前回到最后一个（QTextCursor::End 位置处向前搜索返回 null，因此需要显式 wrap）。匹配计数用 selectionEnd 递增游标避免重叠匹配死循环。
- 表格单元格可被查找命中（QTextDocument::find 会遍历 frame 树，探针 countMatches("aaa")=1），无需额外处理表格 frame。
- 查找条是内联控件 ui::FindBar（QLineEdit + 区分大小写 + 匹配数 + Prev/Next），Ctrl+F 开关、Esc 关闭；bench 入口 --bench-search 打印 search_matches，环境变量 TYPIST_BENCH_FILE 与 TYPIST_SEARCH_PATTERN 控制被测文件与模式，ctest 项 search_baseline 用它。
- 目录内搜索走 platform::searchFiles（QDirIterator 递归 + *.md，按行给出 path:line 与 120 字符预览，maxHits 默认 200、maxFileBytes 4MB）：QDir 没有 pathParts，要用 root.relativeFilePath(path).split('/') 逐段判断是否以 . 开头或属于 build/node_modules，才能跳过 .git 这类目录。
- Qt6 里不能把 Qt::Case 当类型名写（'Case' in namespace 'Qt' does not name a type），大小写模式要内联传给 contains(pattern, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive)。
- 前向声明 QAction 的类里不能把它当值成员（需要完整类型且 connect 会收到 QAction 而非指针），成员一律用 QAction* 并在构造函数里 new。
- 目录扫描实测：1MB 单文件根目录 file_search_ms=5–6（命中 5000），tests/corpus 根目录 0ms 命中 4；bench 门槛 --bench-file-search 要求 hits>0 且 file_search_ms <= 250，无命中或超预算返回 1（已实测两种失败路径都返回 1）。
- 侧栏文件列表复用 platform::listMarkdownFiles（同一套目录过滤规则），ui::FileTree 是 QTreeWidget 扁平列表（相对路径排序、UserRole 存绝对路径、双击发 entryActivated），没有用 QFileSystemModel：它的填充是异步的，测试与 bench 都需要同步结果。
- 菜单项插入位置受构造函数里的局部变量作用域限制：把 View 菜单的动作接到 Edit 菜单段落里会报 'viewMenu' was not declared in this scope，必须插在 QMenu* viewMenu 声明之后。
- 侧栏实测：401 个 .md（含子目录、含 .hidden 目录里的文件）tree_entries=401，隐藏目录被跳过；tests/corpus 根目录 tree_entries=14。bench 门槛 --bench-tree 要求 tree_entries > 0，否则退出码 1。
- 又一次踩到同一个坑：把新测试实现接在旧函数结尾之后会多出第二份 QTEST_MAIN（redefinition of 'int main'）；改完必须 grep 确认只有一份且在 moc include 之前。
- 侧栏改成层级目录树：FileTree::buildDirectory 递归建条目（目录节点用 kDirRole 标记，文件节点在 kPathRole 存绝对路径），entryCount/entryPath 只数叶子文件，directoryCount 数目录节点；目录过滤与侧栏列表同一规则（以 . 开头、build、node_modules 跳过）。
- 文件操作落在 platform::FileOps（createMarkdownFile/renameFile/deleteFile）：名字没有 .md 后缀就补上，重名直接拒绝且不覆盖，重命名用 QFile::rename 保留正文；UI 侧的对话框（QInputDialog/QMessageBox）只在右键菜单触发，headless 测试与 bench 不会走到。
- Qt6 的 QFile 没有 open(fileName, mode) 重载：读另一个路径要先 QFile file(path) 再 open(QIODevice::ReadOnly)（报 no matching function for call to 'QFile::open(const QString&, OpenModeFlag)'）。
- 层级实测：a.md + sub/b.md + sub/deeper/c.md + .git/g.md → tree_entries=3 tree_dirs=2（.git 被跳过）；删除当前编辑文件后缓冲区变未命名文档（bench 不覆盖这条，属交互路径）。
- 目录自动刷新走 FileTree::refreshIfChanged()：signature() 用相对路径 + size + birthTime 拼成签名，签名相同就不重建（保留当前选中项）；QFileSystemWatcher 只监视目录，目录变动经 250ms 单发 QTimer 去抖后刷新并重新 addPaths（目录改名后 watcher 会丢掉旧路径）。
- Qt6 的 QList 没有 filter(predicate)（error: no matching function for call to 'QList<QString>::filter(lambda)'）：过滤要写成显式循环。
- MainWindow 里用 event->mimeData()->hasUrls() 需要 #include <QMimeData>，否则 invalid use of incomplete type 'const class QMimeData'；拖拽判定逻辑抽到 platform::firstMarkdownPath（取第一个本地 .md URL，https URL 与 .txt 都返回空）以便 headless 测试。
- 监听只在侧栏可见时开启（toggleSidebar 显示时 setWatchEnabled(true)、隐藏时 false），bench 与 headless 测试不启用 watcher；QFileSystemWatcher 与拖拽的 UI 路径不在测试覆盖内，已在 README/ROADMAP 标注。
- 全部匹配高亮走 SearchController::allMatches（上限 1000）+ QTextEdit::ExtraSelection 的 format.setBackground（取 QPalette::Highlight 加 alpha 110），关闭查找条时把 pattern 置空以清掉高亮；const 的 bench seam 不能调 setCaseSensitive（discards qualifiers），要拷贝一份 SearchController 再用。
- PDF 输出的 /MediaBox 以 point 为单位（A4 595x842、Letter 612x792），断言纸张差异要按 point 比较而不是像素；解析时用 marker 长度偏移，硬编码偏移会读错数字（探针实测 offset+13 让宽高错位）。
- 分页限制：单个高于页面内容高度的块会被截断（例如超长表格），后续页从下一个块继续；跨页表格与防孤行留待 Beta。
- 页码/边距配置走 QSettings 键 pdfPaper=a4|letter、pdfMarginMm（限 5–60mm，非法值回退 20mm）。
- 构造顺序教训：applyTheme() 会写 m_themeAction 的文字，必须在 action 创建之后调用（否则空指针崩溃）；保留 nullptr 守卫。
- 性能基线已建立（本机 Windows、Release/NDEBUG）：冷启动 295ms（原生 windows platform；offscreen 17ms 只作 proxy），常驻工作集 21.7MB，1MB Markdown 两种形状——realistic parse 34 / build 67 / serialize 16 = 117ms，tableheavy parse 51 / build 111 / serialize 82 = 244ms，均进入 <250ms 预算。
- 输入到重绘已有测量通道：`typist --bench-repaint` 插入一个字符后同步 `repaint()` 计满三次，超过 16ms 即以退出码 1 失败；空文档形状作为 ctest 项 `repaint_baseline`，载入 1MB 文档的对照走 `TYPIST_BENCH_FILE`（本机实测 0–1ms）。
- 自动保存节奏（按假设落地，待用户改值时回来确认）：最后一次编辑后 2 秒落盘（单次 QTimer 去抖，连续打字只产生一次写入），只在已知路径时触发——未命名缓冲区不会凭空建文件；切换源码/渲染视图与关闭窗口时 flushNow() 强制落盘。写入仍走 writeAtomic，脏标记在落盘点才做精确比较，打字路径不序列化。未提供开关（Typora 行为即始终自动保存），若后续要「关闭自动保存」需加配置键。
- 给 Qt 传文件路径要用原生路径：MSYS2 的 /tmp 路径 Qt 打不开（openFile 失败但不会有明显报错），bench 用 E:/Typist/.deps/typist_1mb.md 并由 documentBlockCount 打印确认确实载入。
- buildDocument 曾为 4615ms，根因不是表格而是逐块变更通知与撤销命令（表格形状只多约 200ms）。批量构建改为单个 beginEditBlock/endEditBlock 并在构建期间关闭 undo（loadMarkdown 本来就在载入后清空撤销栈，语义不变），降到 67ms（realistic）/111ms（tableheavy）。增量渲染因此不再是预算缺口，退为后续优化项。
- perf_baseline 跑两种形状并打印数字：realistic 断言 total < 250ms（就是验收预算），tableheavy 断言 total < 1500ms（只抓大回归，CI 机器抖动不定）。回归按打印值比对。
- 表格与后续内容：Qt 6.12 的 QTextCursor 没有 StartOfTable/EndOfTable（qtextcursor.h 的 MoveOperation 里没有这两个枚举），填完单元格后必须 movePosition(End) 再 insertBlock，同时序列化器跳过表格时不能多推进一个 block；两处叠加曾把长文档塌成一片表格（18590 blocks 变成 4），现由 gfm/table_then_para.md 语料覆盖。
- MSVC 路线仍未核实：windows-msvc preset 保留，本机与 CI 都未验证过。
