# Typist 路线图

## 当前功能复刻进度（2026-10-29）

| 功能 | 当前状态 |
| --- | --- |
| Typora 风格布局、浅深主题、中英文菜单 | 已实现；宽窗口打开 README 与侧栏缩放有原生 Windows 回归 |
| 实时 Markdown 编辑 | 标题、列表、任务项、引用、代码围栏输入转换；闭合行内粗斜体、删除线、代码、链接就地渲染；支持段落中间输入 |
| 段落 / 格式命令 | 正文、标题 1–6、三类列表、引用、代码块、分隔线；粗斜体、删除线、行内代码、清除格式、链接、图片 |
| 表格编辑 | 上下文工具条、增删行列、多列对齐、整表 Markdown / TSV 复制、矩形选区复制 / 清空 / 填充、电子表格 TSV 粘贴与自动扩展；Tab 导航及 Ctrl+Enter 退出；对齐、字面文字和多行单元格保留到 Markdown / HTML / PDF / DOCX |
| 搜索替换 | 文档替换 / 全部替换、大小写与 Unicode 正则、捕获组；全部替换为一个撤销步骤 |
| 写作辅助 | F8 专注模式、F9 打字机模式、F11 全屏、缩放、括号引号配对、文档和选区统计 |
| 文件操作 | 新建、新窗口、快速打开、源码切换；自动保存可关闭；未保存文档在新建、切换、关闭前支持保存 / 放弃 / 取消 |
| Markdown 复制粘贴、相对图片 | 支持；粘贴保留插入点之后内容；相对图片按文档目录解析并可内嵌 DOCX |
| 图片资源管理 | 剪贴板截图、本地多图拖入、文档旁资源目录、去重、另存为迁移、HTML 内嵌；保留原文件与撤销后的资源；引用式相对图片迁移需先改为内联链接 |
| 数学公式 | 行内 `$…$` 与独立 `$$` 公式块，离线原生 LaTeX 渲染、双击修改、撤销；PDF / DOCX / HTML 内嵌渲染结果 |
| Mermaid | 原生流程图 `flowchart / graph` 与时序图 `sequenceDiagram`；双击编辑与实时预览；不支持的语法保留为可编辑代码 |
| 脚注 | `[^id]` 引用与多行定义、菜单插入、点击跳转；DOCX 使用 Word 原生脚注；PDF 在文末显示定义 |
| 正文 `[TOC]` | 按标题自动生成并随编辑更新、点击跳转；PDF 长目录可跨页；DOCX 输出可更新的 TOC 域与内部链接 |
| 代码语法高亮 | 常见语言的关键词、字符串、数字与注释，支持多行状态、浅深主题及源码模式；代码语言可修改，HTML / PDF / DOCX 共享着色规则 |
| 代码块写作 | 顶部上下文工具条选择语言 / 复制原文 / 退出；自动缩进、成对括号分行、Tab / Shift+Tab 多行缩进、缩进退格、Home、Ctrl+Enter 退出；粘贴保留代码原文 |
| 段落源码与嵌套行内格式 | 正文和标题可用 Ctrl+Enter 就地编辑 Markdown；应用时整段解析、可撤销；已修复嵌套粗体 / 斜体 / 删除线的保存保真；自动随光标揭示标记尚未实现 |
| 多格式导入、EPUB、站点发布、云端图片上传 | 尚未实现 |

功能验收以当前行为测试为准；下方保留早期里程碑与实现记录，其中部分旧限制已被上述实现取代。Typora 的完整行为仍是后续迭代目标。

表格操作见 [Tables.md](Tables.md)。`table_workflow` 覆盖矩形剪贴板、自动扩展、选区清空、表头保护、工具条操作、字面 Markdown 与 HTML 换行保真、导航、源码切换和导出；原生窗口另验证中英文 / 主题切换与保存。

图片操作与边界见 [Images.md](Images.md)。`image_assets` 检查透明度、去重、原图复制、路径编码、另存为迁移、剪贴板 / 拖放、段落源码、撤销和导出；`tests/corpus/image_paths.md` 覆盖图片 Markdown 的往返保真。

本批高级语法的使用方式与边界见 [Advanced-Markdown.md](Advanced-Markdown.md)。`advanced_markdown` 覆盖公式字形、图表解析与源码回退、往返保存、撤销、主题、源码切换及三种导出；长目录另作 PDF 分页检查。数学字体与解析器随应用打包，不要求联网或安装 TeX。

代码与段落编辑见 [Writing-Experience.md](Writing-Experience.md)。`syntax_and_paragraph` 覆盖多行词法状态、显示层着色、代码语言修改、嵌套格式、段落应用 / 取消 / 撤销、只读草稿快照及着色导出；窗口回归另检查手动保存、自动保存、源码切换与取消导航时的草稿保留。

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
- 嵌套深度实测：`- one / - two / - three / - four / - five` 五级嵌套（每级缩进 2 空格）在 core 序列化往返与 QTextDocument 往返两侧 astEqual 均通过（spike slot nestedListsRoundTripAtDepthFour），深度不是限制项；渲染侧靠 Qt 列表 indent 逐级递增，序列化按 indent-1 与 depth 匹配。
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
- 侧栏展开状态事实：FileTree::setRoot 先收集已展开目录路径（kDirRole+kPathRole），重建后按集合逐项 setExpanded；用 `m_hasExpansionState` 区分"首次建树（全展开）"与"用户把目录全折叠（空集合，保持全折叠）"——只判断集合为空会把折叠状态又打开（实测第一轮失败）。
- 目录自动刷新走 FileTree::refreshIfChanged()：signature() 用相对路径 + size + birthTime 拼成签名，签名相同就不重建（保留当前选中项）；QFileSystemWatcher 只监视目录，目录变动经 250ms 单发 QTimer 去抖后刷新并重新 addPaths（目录改名后 watcher 会丢掉旧路径）。
- Qt6 的 QList 没有 filter(predicate)（error: no matching function for call to 'QList<QString>::filter(lambda)'）：过滤要写成显式循环。
- MainWindow 里用 event->mimeData()->hasUrls() 需要 #include <QMimeData>，否则 invalid use of incomplete type 'const class QMimeData'；拖拽判定逻辑抽到 platform::firstMarkdownPath（取第一个本地 .md URL，https URL 与 .txt 都返回空）以便 headless 测试。
- 监听只在侧栏可见时开启（toggleSidebar 显示时 setWatchEnabled(true)、隐藏时 false），bench 与 headless 测试不启用 watcher；QFileSystemWatcher 与拖拽的 UI 路径不在测试覆盖内，已在 README/ROADMAP 标注。
- 全部匹配高亮走 SearchController::allMatches（上限 1000）+ QTextEdit::ExtraSelection 的 format.setBackground（取 QPalette::Highlight 加 alpha 110），关闭查找条时把 pattern 置空以清掉高亮；const 的 bench seam 不能调 setCaseSensitive（discards qualifiers），要拷贝一份 SearchController 再用。
- DOCX 导出用 platform::DocxWriter 自己写 ZIP（store 不压缩）+ OOXML：ZIP 的 central directory size 必须在追加 end-of-central-directory 记录之前算出（先写 EOCD 头再算会把 EOCD 自身算进 size，zipfile 报 Bad magic number for central directory）；EOCD 总记录数是 2 字节字段（不是 4 字节）；crc32 必须是 table[(crc ^ byte) & 0xff] ^ (crc >> 8)（写成 table[crc & 0xff] 会让 python 报 Bad CRC-32）。
- OOXML 的样式名是子元素 <w:name w:val="heading 1"/>，不是属性 w:name=（用属性时 python-docx 的 paragraph.style.name 为 None）；页面尺寸用 twips（A4 = 11906x16838，20mm 边距 = 1134 twips）。
- DOCX 包的外部验证：用捆绑 python 的 python-docx 重新打开 .deps/probe.docx（段落 9、表格 1、样式 Heading 1 / List Paragraph、页面 210x297mm、边距 20mm）；ctest 侧只测 zip 本地头遍历（localEntryNames + EOCD 条目数）与 docx_baseline（docx_bytes >= 400）。
- 独立探针链项目静态库时，改过 src/platform 后必须先 cmake --build --target typist_platform 再重新链接探针，否则探针会链到旧的 .a 并给出与源码不符的字节数。
- 原生 dark 检测落在 platform::SystemTheme：Windows 用 QSettings(NativeFormat, UserScope, "Microsoft", "Windows") 且键路径相对于 Software/<org>/<app>（写成 Software/Microsoft/Windows/... 会重复前缀并读不到值，hint 恒为 Unknown）；Linux 走 GTK_THEME（含 dark 即 Dark）否则解析 settings.ini 的 gtk-theme-name；macOS 用 QProcess::execute("defaults", {"read","-g","AppleInterfaceStyle"})，非零退出表示浅色（本机未验证）；MainWindow 的 systemPrefersDark() 优先用原生 hint，Unknown 时回退窗口调色板亮度。解析规则拆成 darkHintFromAppsUseLightTheme/darkHintFromGtkTheme 纯函数以便 headless 测试。
- 多语言：UI 文案改为 MainWindow 的 tr()（context 为 MainWindow），translations/typist_zh.ts 手写 34 条；typist::ui::Localization 负责候选目录（cwd translations/、appdir translations/、QLibraryInfo::TranslationsPath、~/.typist/translations/）与 installTranslatorForLocale，英语直接返回空候选（源语言）。本机没装 lupdate/lrelease（pacman 的 mingw-w64-x86_64-qt6-base 不提供 Linguist 工具），所以本地不产 .qm、UI 走英语；CI 里用 `lupdate -tr strict src/ui/*.cpp -ts translations/typist_zh.ts` 与 `lrelease translations/*.ts` 补齐（不要手改 source 文本，避免与代码漂移）。
- 翻译一致性用 headless 测试兜住：editor_spike 读 TYPIST_TS_FILE 与 TYPIST_UI_SOURCE，逐条断言 .ts 的 source 在 MainWindow.cpp 里有对应的 tr("...")、translation 非空、XML 合法；教训：QXmlStreamReader 的 text() 在 EndElement 上是空的，元素正文要靠在 StartElement 与 EndElement 之间捕获 Characters token（配合 isWhitespace() 过滤缩进）取得。
- 构建时序教训：用 node fs.writeFileSync 改测试源码后 ninja 可能报 no work to do（对象 mtime 比源文件新），要 utimesSync 把源文件时间戳推到当前再 cmake --build，否则测的还是旧二进制。
- 目录搜索改为后台线程：platform::FolderSearchRunner（QThread 子类 ScanThread + 原子 cancel 标志），FileSearch 拆出 scanFiles(request, error, const std::atomic<bool>*, bool* wasCancelled)，包装成 searchFiles 与 searchFilesCancellable，取消时返回部分命中并置 wasCancelled；MainWindow 用 resultsReady(hits, cancelled, error) 填 dock，closeEvent 先 cancel()。platform 静态库因为有 QObject 信号需要 AUTOMOC ON。
- 持久化三件套：platform::recentFiles/addRecentFile/removeRecentFile（键 recentFiles，最多 8 条，菜单打开时剔除不存在的路径）；ui::RecoveryStore（草稿目录 TYPIST_RECOVERY_DIR 覆盖 ~/.typist/recovery，键为路径 SHA1 十六进制，未命名缓冲区共用 untitled 槽）；ui::decideConflict(dirty, diskDiffers, canPrompt) 纯函数——干净缓冲区跟随磁盘，脏缓冲区在无对话框时绝不重载；FileTree 新增 directoryChanged 信号（去抖刷新后发出），MainWindow::checkCurrentFileConflict 读盘比较已保存快照并按纯函数决定，QMessageBox 默认 No 保留编辑；恢复草稿落在未命名缓冲区并删除草稿文件。
- 大纲：ui::outlineFromDocument 靠 QTextBlockFormat::headingLevel() 扫块（渲染文档里标题标记是样式不是文本），条目带 level/text/blockNumber/position；MainWindow 的 Outline dock（Ctrl+Shift+T）按 level-1 个空格缩进，UserRole 存 position，点击设光标并 ensureCursorVisible，源码模式下拒绝跳转并提示。
- 正则查找：SearchController::regexRanges 对每个块 globalMatch，绝对位置 = block.position()+capturedStart/End，空匹配跳过（否则同一位置堆叠）；因此 ^/$ 是块内锚点，渲染视图里 ^# 匹配不到标题（# 不在文本里），源码视图（setPlainText 每行一块）能匹配 ^#。无效表达式返回空集合（Qt 6.12 已去掉 patternError()，改用 QRegularExpression::isValid()）。
- 行内样式：core::InlineRun（text/url/bold/italic/strike/code/link/image）与 Block.runs；cmark-gfm 的强调节点类型字符串是 **emph** 不是 emphasis（用 nodeprobe 实测确认），分支要同时接受两者；嵌套靠标志位叠加（bold+italic 序列化为 ***x*** 再解析回两个标志）。serializeMarkdown 按 run 重新加标记；astEqual 现在也比较 runs。
- 渲染侧：editor 用 QTextCharFormat 落地字重/斜体/删除线（setFontStrikeOut）/行内代码（fontFamilies Consolas+Courier New）/链接（setAnchor + anchorHref + TextUnderlineStyle 属性，不是已废弃的 setFontUnderline）/图片（自定义属性 kImageProperty=0x100006 存目标 URL，同时是锚点）。序列化回 Markdown 靠 QTextBlock::textFormats()（QTextLayout::FormatRange，块内相对坐标）分类，未样式块走 fast path（textFormats 为空直接返回文本）；QTextCharFormat::fontFamilies() 返回 QVariant，要 .toStringList()。
- 已修缺口：标题序列化走 inlineFromBlock（行内样式保真）；渲染视图画出图片本体；DOCX 图片内嵌成 drawing 对象；编辑器渲染视图用 ☑/☐ 字形代替列表圆点。仍存缺口：☑/☐ 字形依赖阅读器字体、DOCX 表格不跨页、表格跨页不重复表头。
- 粘贴：editor::insertMarkdownAtCursor(source, cursor, error) 用 beginEditBlock/endEditBlock 一次插入全部块（一步撤销），先 cursor.insertBlock() 让粘贴从新行开始；MainWindow Ctrl+Shift+V 走它，源码视图走 m_editor->paste() 保纯文本。拖拽图片用 platform::firstImagePath（png/jpg/jpeg/gif/webp/svg/bmp）生成 ![name](path)。
- 快捷键冲突事实：同一个 `QShortcut` 上下文里后注册的 `QAction` 会吃掉相同按键，导出 HTML 与恢复草稿曾都用 Ctrl+Shift+E，恢复草稿改为 Ctrl+Shift+R；新增快捷键前先 grep `Ctrl+Shift` 确认未占用。
- CI 现在有三个本地跑不到的门：`i18n`（ubuntu，装 `qt6-l10n-tools`，`lupdate -tr strict` + `lrelease` 产出 `.qm` 并上传 `translations` 产物）、windows 作业里的 Performance gate（realistic `total_ms` <= 250、tableheavy <= 300、必须 `fidelity=ok`）与 Translation coverage（比较 MainWindow 唯一 `tr()` 数与 `.ts` 里 `<source>` 数）、`windows-msvc`（Bahrdot/setup-qt6 + cmark-gfm 源码构建，本机无 cl.exe，只在 CI 验证）。
- 许可合规：仓库根目录 `NOTICE` 记 Qt LGPLv3 动态链接与可重链接步骤、cmark-gfm BSD-2、无内嵌字体/模板；文档页面 `docs/Build.md`、`docs/Keyboard.md`、`docs/Themes.md`、`docs/Translations.md` 与 README 链接。
- 性能事实：行内分类给 1MB realistic 的 serialize 加了约 3ms（157ms 总）、tableheavy 262ms；fast path（无 textFormats 的块直接返回文本）是必要的，否则 tableheavy 多出约 50ms。
- PDF 分页事实：`setPageSize()` 不会重新排版，必须调 `layout->documentSize()` 才有真实块高与每块的行布局；`QTextLine::rect()` 是**块内相对坐标**，分页偏移要加上 `blockBoundingRect(block).y()`；软换行的段落是**一个块**（块内可含 \n），所以长段落确实会高于一页，但换行后行数比源行数少得多（600 源行 ≈ 49 排版行 ≈ 638px，需要更长的内容才会触发分页路径）。
- DOCX 事实：成员 XML 用 `toUtf8()` 写入且未压缩，可直接在字节里 grep 标签与字形；非 ASCII 字形别用 `QStringLiteral("\uXXXX")`（本轮字节字面量断言失败而 `QString(QChar(0x2611)).toUtf8()` 命中），用 QChar 显式构造再转 UTF-8；表格列宽按每列最长文本权重分配，总宽 9638 twips。
- 内联图片事实：`cursor.insertImage(QTextImageFormat)` + `document->addResource(QVariant::Url, url, QImage)` 才会真的画出图；**打完图后不能用新建的 QTextCharFormat 去 setCharFormat 标记对象字符——那会抹掉 ImageName/ImageWidth/Height，块高变成 0**，必须读回 `cursor.charFormat()` 再追加自定义属性（实测：新建格式 h=0，复用格式后 h≥18）。序列化读 `kImageProperty`/`kImageAltProperty` 还原 `![alt](url)`；块文本里那个对象字符是 U+FFFC，不能让它漏进序列化。Qt6 没有 `setImageSizeLimit`，缩放靠显式 width/height。
- 打包事实：CPack 的生成器在根 `CMakeLists.txt` 里按平台选（Windows ZIP、macOS TGZ+DMG、Linux TGZ，CI 显式 `-G "DEB;RPM;TGZ"` / `-G "DMG;TGZ"`）；`cmake --install` 只装 exe 与数据，Windows 的 Qt 运行时 DLL 必须靠 `windeployqt` 补（实测暂存树 24 文件、包 12.97MB）。本机没有 `dpkg-deb`/`rpmbuild`/`hdiutil`，deb/rpm/dmg 只能在 runner 上验证。
- 更新检查事实：`QJsonDocument::fromJson` 的 `QJsonParseError` 必须是非 const（传 const 指针编译报错）；版本比较按点号分段整数比大小，预发布后缀（`-beta1`）永远不比基础版本新；网络模块走 `OPTIONAL_COMPONENTS Network` + `#ifdef TYPIST_HAS_NETWORK` 守卫（菜单动作与槽都在守卫内，缺模块时提示"此构建不支持更新检查"）；版本号靠 `target_compile_definitions(... TYPIST_VERSION="${PROJECT_VERSION}")` 传入。
- 文档站事实：`pages.yml` 用官方 `actions/jekyll-build-pages@v1`（source: docs、destination: _site）构建后由 `upload-pages-artifact` + `deploy-pages` 部署；仓库的 Pages 来源必须在 GitHub 设置里选 "GitHub Actions"，这一步只能在 GitHub 上确认，本机只能做 YAML 静态检查（偶数缩进、无制表符）。
- 性能测量事实：本机并发任务（后台构建）会让单次计时膨胀到 2 倍（realistic 175→290、tableheavy 277→664），因此 perf_bench 取三次尝试的每阶段最优值输出（attempts=3）；干净跑下 realistic 177ms、tableheavy 278ms，仍在 CI 门限 250/300 内。标题序列化走 inlineFromBlock（保留粗体/强调/行内代码/链接），实测比纯文本路径慢约 20ms（serialize 90 vs 70）。
- 增量排版事实：buildDocument 后调用 documentLayout()->documentSize() 实测 0ms（realistic 与 tableheavy 都是 0），说明排版随每次插入增量完成、没有一次性全量成本，因此 Beta 的"增量渲染"不需要另做实现；导出与 PDF 分页仍要显式 documentSize() 才有真实块高。

- 发布纪律：每个包都要有 SHA-256（`checksums.txt`），`latest.json` 是更新清单；应用内已实现检查更新（Ctrl+Shift+U，超时 5 秒，只提示不下载安装），默认更新源已指向真实上游 `https://github.com/moxi486/typist/releases/latest/download/latest.json`，fork 用 `TYPIST_UPDATE_URL` 覆盖；分支名必须是 `main`（`ci.yml` 只触发 main）；版本 0.1.0 起由 `TYPIST_VERSION` 编译定义注入，改版本号后必须重新 configure；未配签名证书时如实发布未签名版本，不做任何绕过签名的做法。
- 布局惰性事实：`blockBoundingRect().height()` 在没排版时给 0/14，要 `documentLayout()->documentSize()` 强制排版才有真实块高（与 PDF 分页同一条纪律）。
- 任务标记事实：勾选状态存在 **QTextBlockFormat 的属性 kTaskProperty**（不在 QTextCharFormat 里）；渲染视图里条目开头插入 ☑/☐ 字形（带 kTaskMarkProperty 标记）并把列表样式设为 **QTextListFormat::ListStyleUndefined** 去掉圆点——像素探针实测 disc 区域 ink>0、undefined 区域 ink=0，说明该样式确实不画标记（Qt6 没有 ListNone，也没有自定义 bullet 字符 API）。序列化在列表发射处剥掉前两个字符（字形+空格），**不要**在 inlineFromBlock 里逐片段查属性：那样 tableheavy 的 serialize 从 92ms 升到 123ms、total 从 279 升到 330（破 300 门限），发射处剥离后回到 109/279。
- Qt6 API 事实：`QTextCursor(QTextDocument*, int position)` 这个重载不存在，要 `QTextCursor(document->findBlockByNumber(n))` 或先 `QTextCursor(document)` 再移动；点击门限用 `m_editor->cursorRect(cursorAtBlockStart).x()` 比较（标记区在条目文字左边）。
- DOCX 超链接事实：`w:hyperlink r:id` 从 rId3 起（rId1 是 styles、rId2 是 numbering），关系写 `TargetMode="External"`，重复 URL 复用同一 rId；链接文字带 `<w:u w:val="single"/>`。
- DOCX 图片内嵌事实：drawing 命名空间是 wp=`http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing`、a=`.../drawingml/2006/main`、pic=`.../drawingml/2006/picture`、r=`.../officeDocument/2006/relationships`（用 python-docx 的 nsmap 核对，不是 wordprocessingml 下的名字）；图像部件写 `word/media/imageN.png`（zip 成员为 STORED 未压缩，可直接 grep 到 PNG 头），内部关系 id 从 **rId1001** 起以避开与超链接 rId 序号冲突；尺寸用 EMU（像素 × 9525）；验证走 python-docx：`inline_shapes` 数 1、`width/height` 为 228600/171450 EMU、`doc.part.rels` 里 reltype 为 IMAGE 且 `target_ref="media/image1.png"`（`InlineShape` 没有 `extents`、`_Relationship` 没有 `type`，要用 `width/height` 与 `reltype`）。
- 测试纪律：断言里写 `\xE2\x9C\x91` 这类字节字面量容易被源码编码影响，优先用运行时 `QString(QChar(0xXXXX)).toUtf8()` 生成待查字节。
- Qt6 API 事实：QTextCursor::MoveMode 只有 MoveAnchor 与 KeepAnchor（没有 KeepSelection/KeepRightSelection/MoveCursor），扩展选区用 setPosition(start, MoveAnchor) 再 setPosition(end, KeepAnchor)；QTextCursor 构造函数要非 const QTextDocument*，只读匹配路径用 const_cast；QTextBlock 用 isValid() 不是 isNull()；QAction 没有 setAccessibleName（可访问性靠 QWidget::setAccessibleName + QAction::setToolTip）。
- 测试纪律：正则 slot 断言不要拿 findBlockByNumber(0).position() 当首个命中位置（文档首块常是空块，真实偏移是 1），改成用 outlineFromDocument 的 position 比较顺序。
- 测试教训：platform::Settings 用的是原生存储（Windows 注册表 HKCU\Software\Typist\Typist），QSettings::setPath(IniFormat, UserScope, dir) 不影响 NativeFormat；bench/ctest 会往注册表写 recentFiles，所以测试要先 QSettings("Typist","Typist").remove("recentFiles") + sync() 再断言，结尾再清一次。
- spike_run.sh 只跑二进制不构建：改过测试源码后要先 cmake --build build/mingw，否则会看到旧断言的失败信息（这次表现为 QCOMPARE 报 stored.size()=3，而源码里已改成 QVERIFY2）。
- Qt6/MinGW 下跨线程信号的类型注册：Q_DECLARE_METATYPE(SearchHit) 写在 namespace 里会报 explicit specialization of QMetaTypeId outside its namespace must use nested-name-specifier，写在 namespace 外同样不通过；改为在 FolderSearchRunner 构造函数里运行时 `qRegisterMetaType<QVector<SearchHit>>()`（0 参数重载），QSignalSpy 才能取到值。
- PDF 输出的 /MediaBox 以 point 为单位（A4 595x842、Letter 612x792），断言纸张差异要按 point 比较而不是像素；解析时用 marker 长度偏移，硬编码偏移会读错数字（探针实测 offset+13 让宽高错位）。
- 分页实测：单个高于页面内容高度的**段落**块按行边界拆页（QTextLine::rect() 块内坐标）；长**表格**按行块分页——60 行表格导出实测 pages>=2（slot pdfLongTablePaginatesRowByRow），表格跨页不重复表头，防孤行仍待做。
- DOCX 表格分页事实：OOXML 的 `<w:tbl>` 行由阅读器（Word/WPS/LibreOffice）自动跨页分页，导出侧不需要特殊处理；本机没有这三个阅读器，渲染分页只能在装有 Word/WPS 的机器或 CI 上确认。
- PDF 重复表头事实（已实现）：PdfWriter 在续页起始块位于表格内部时，用 `QTextCursor(block).currentTable()` 取表、`cellAt(block.position()).row() > 0` 判定为续页、用首行各单元格的 `cellAt(0, col).firstCursorPosition().block()` 合成表头矩形，把表头画在页顶并从内容高度里扣掉表头高度 + 4px。块→表格只能用 `QTextCursor::currentTable()`：QTextBlock 没有 table()，QTextDocument 没有 findFrame/findTable，QTextTable 没有 cellPosition（是 cellAt(row, col)）。本机验证：分页模拟断言每个位于表格内的续页起始块都拿到表头矩形；导出对比 100 行表格 off/on 都是 3 页但字节数 17300→17450（续页确实多画了表头内容）；重画的字形内容是否在阅读器里正确显示仍要靠阅读器确认。
- 防孤行事实（已实现）：`typist::platform::splitParagraphChunks(lineCount, linesPerPage)` 给出行范围分块——块连续覆盖全部行、每块不超过 linesPerPage，且带防孤行规则（剩余行数恰好为 1 时把上一块缩到 linesPerPage-1，让段末至少两行同页；单行段落保持单块）。PdfWriter 的超高块路径先数出能放进 contentHeight 的行数再分块、逐块 emitPage；Qt 排版没有孤行控制 API，规则在应用侧实现。单测覆盖 60/30、31/30（29+2）、61/30（无单行块）、0/1/40/1 边界。
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
