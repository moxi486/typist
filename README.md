# Typist

开源的 Typora 式所见即所得 Markdown 编辑器：单栏编辑、无独立预览区，源码与渲染在同一视图切换。原生跨平台桌面应用（Windows / Linux / macOS）。

独立实现，与 Typora 官方无任何隶属关系，不使用 Typora 的资源、图标或名称混用。

## 当前可用功能

- 打开 / 保存 Markdown（原子写入）
- 撤销 / 重做，历史上限 200 步
- 同一视图内源码与渲染切换（Ctrl+/），两种视图各留一份撤销历史；内容语义相同则切换不重建文档
- 文档内查找（Ctrl+F 打开查找条，Enter 下一个、Shift+Enter 上一个，F3 / Shift+F3 步进，Esc 关闭；显示匹配数，可切换区分大小写，两端循环，表格单元格也能命中；查找条打开时高亮全部匹配，最多 1000 处，关闭查找条即清除高亮）
- 文件侧栏（Ctrl+Shift+L 开关左侧目录树，Ctrl+Shift+O 打开文件夹；右键菜单可新建/重命名/删除 Markdown 文件，重命名保留正文内容，删除当前编辑的文件会把缓冲区变成未命名文档，双击打开文件；侧栏打开时监听目录变化并在 250ms 去抖后自动刷新；重建后保留目录的展开/折叠状态，首次建树默认全展开）
- 拖拽打开：把 .md 文件拖到窗口即打开（多个文件取第一个 .md，非本地 URL 忽略）
- 目录内搜索（Ctrl+Shift+F 搜索当前文件所在目录，结果落在底部 dock，双击打开命中文件；跳过隐藏目录与 .git/node_modules）
- 目录搜索在后台线程扫描并可取消（新的搜索会取消上一轮，关闭窗口时取消），扫描提前结束时会给出已找到的部分结果
- 导出 HTML（Ctrl+Shift+E，走 cmark-gfm 的 GFM 扩展）
- 导出 PDF（Ctrl+Shift+P，走 QtGui 的 QPdfWriter；纸张 a4/letter、页边距 mm、页眉与页脚（页脚里的 # 替换为页码）可从配置控制，默认 a4 + 20mm + 页眉文件名 + 页脚 Page #）
- PDF 分页：长段落按行边界拆页（不会把一行切成两半）并带防孤行（段末单行不会单独留在下一页、续页不会只以一行开头），长表格按行分页并在续页顶部重画表头（默认开启，可用 `PdfPageOptions.repeatTableHeader=false` 关闭）
- 导出 DOCX（Ctrl+Shift+X，由 platform::DocxWriter 直接写 OOXML 包：标题、段落、列表、任务列表、代码块、引用、分隔线、表格；A4 20mm 边距与 PDF 默认一致）
- 导出 EPUB 3（文件 → 导出 EPUB）：设置书名、作者和语言，生成阅读器目录与脚注跳转，内嵌本地图片、公式和图表；使用边界见 [docs/EPUB.md](docs/EPUB.md)
- DOCX 表格列宽按每列最长内容分配（总宽约 9638 twips，等于 A4 减两侧 20mm），任务项用 ☑/☐ 字形（能否显示取决于阅读器字体）
- DOCX 链接写成 OOXML 超链接对象（w:hyperlink + 外部关系，同一 URL 共用一条关系）；图片内嵌为 OOXML drawing 对象（图像本体写进 `word/media/imageN.png`，内部关系从 rId1001 起，超过 640×480 按比例缩小，EMU 尺寸 = 像素 × 9525）；图文件读不到时退回带下划线的文字链接
- 深嵌套列表：五级嵌套（每级缩进两空格）在 Markdown 往返与富文本往返两侧都保真，深度不是上限
- 检查更新（Ctrl+Shift+U，Help > Check for Updates）：抓取更新源的 `latest.json`，比对版本号后在状态栏提示新版本；只提示，下载与安装仍手动
- 任务项显示真实勾选字形：渲染视图里任务列表不再画 Qt 圆点，条目开头直接画 ☑/☐（勾选状态存在块格式属性里，字形只是呈现）；点字形区域翻转状态，序列化时剥掉字形前缀，源码仍是 `- [x]` / `- [ ]`（往返保真不变）
- 渲染视图内联显示图片本体：`![alt](path)` 指向的本地图会直接画在文档里（超过 640×480 按比例缩小），序列化时仍还原成原来的 Markdown 链接（alt 与目标存在对象运行格式的属性里）；图不存在时退回带下划线的文字链接
- 主题 light / dark / system（Ctrl+Shift+D 循环，偏好写入 QSettings；导出的 HTML 内嵌同一份主题样式）
- system 主题的判定顺序：Windows 读注册表 AppsUseLightTheme、Linux 读 GTK_THEME 或 ~/.config/gtk-3.0/settings.ini、macOS 读 AppleInterfaceStyle，读不到时回退到窗口调色板亮度
- 界面语言跟随系统 locale：按语言取 typist_<lang>.qm 目录（依次找当前目录 translations/、可执行文件旁 translations/、Qt 翻译目录、~/.typist/translations/），英语是源语言无需目录；仓库带 translations/typist_zh.ts，编译产物 .qm 由 CI 的 lrelease 生成（本机未装 Qt Linguist，所以本地跑的是英语源文案）
- 自动保存（最后一次编辑后 2 秒落盘；未命名文件不自动保存；切换源码/渲染视图与关闭窗口时立即落盘）
- 最近打开的文件（File > Recent Files，最多 8 条；菜单打开时剔除已不存在的路径）
- 崩溃恢复草稿（有未保存编辑时写入草稿目录；File > Recover Draft（Ctrl+Shift+R）把最新草稿恢复到未保存缓冲区；启动时若存在草稿会在状态栏提示条数）
- 外部修改冲突（磁盘内容变化且缓冲区未保存时询问是否重新载入，默认保留你的编辑；无对话框场景与 bench 模式一律保留编辑）
- 大纲面板（Ctrl+Shift+T 开关；按标题层级缩进，点击跳转；只在渲染视图生效，源码视图会提示）
- 查找支持正则（查找栏 Regex 复选框；按块匹配，锚点 ^/$ 作用于每个文本块；无效表达式视为无匹配）
- 无障碍标注（编辑器、文件列表、搜索结果、大纲与草稿 dock 提供 accessibleName，菜单动作提供含快捷键的 tooltip）
- 行内样式保真：粗体、斜体、删除线、行内代码、链接、图片在 parse → serialize → parse 里完整往返，标题里的行内样式同样进序列化；渲染视图用字重/斜体/删除线/等宽字体/锚点呈现，链接显示为带下划线的锚点文本
- 粘贴为 Markdown（Ctrl+Shift+V）：把剪贴板里的 Markdown 解析成块插到光标处，整个粘贴是一步撤销；源码视图下按纯文本粘贴
- 拖拽图片：把图片文件拖到窗口会在光标处插入 ![文件名](路径) 链接（源码视图直接插入该文本）
- 往返保真测试：parse → serialize → parse 产生相同 AST
- 文档：`docs/index.md`（首页）、`docs/Build.md`（依赖与三平台构建）、`docs/Keyboard.md`（快捷键）、`docs/Themes.md`（主题与配置）、`docs/Translations.md`（翻译与 `.qm`）、`docs/Release.md`（发布打包、签名与更新通道）、`docs/ROADMAP.md`（路线图与实现事实）；第三方许可声明见仓库根目录的 `NOTICE`
- 文档站：`docs/` 经 `pages.yml` 用 Jekyll 发布到 GitHub Pages（首页是 `docs/index.md`，主题 minimal）
- 性能基线：`perf_baseline` 与 `repaint_baseline`（ctest 项）打印打开耗时与打字到重绘耗时；本机 Windows Release 实测冷启动 295ms、常驻工作集 21.7MB、1MB 文档 realistic 158ms、tableheavy 268–297ms（各取三次尝试的最优值；波动来自本机并发负载）；强制全量排版实测 0ms——排版随插入增量完成，首屏不需要额外实现、打字到重绘 0–1ms

## 性能预算（验收数字）

`perf_baseline` 的 `open_ms` 是同一次测量的 Markdown 解析与文档构建耗时之和，按三次完整测量中打开最快的一次验收。保存时的序列化耗时另列为 `serialize_ms`，每次测量均要求往返保真；表格压力场景保留 1500ms 的解析、构建、序列化总预算。

- 冷启动 < 400ms（本机 Windows Release 实测 295ms；offscreen 的 17ms 只作 proxy）
- 常驻内存 < 80MB（本机实测工作集 21.7MB）
- 打开 1MB Markdown < 250ms（本机实测 realistic 155ms、tableheavy 274ms，取三次尝试最优值）
- 输入到重绘 < 16ms（本机实测 0–1ms，含载入 1MB 文档后；由 `typist --bench-repaint` 测量，空文档形状进 ctest）

## 技术栈

Qt 6 Widgets（C++17）+ QTextDocument 富文本模型 + cmark-gfm（GFM 扩展：表格、任务列表、删除线）。基线 Qt 6.8 LTS，目标 6.12 LTS。Markdown 与富文本之间的双向序列化器位于 src/core，往返保真是验收标准。

## 构建（Linux）

    sudo apt install qt6-base-dev libcmark-gfm-dev libcmark-gfm-extensions-dev
    cmake --preset linux-gcc
    cmake --build build/linux-gcc
    ctest --test-dir build/linux-gcc --output-on-failure

macOS：brew install cmark-gfm qt@6，并把 CMAKE_PREFIX_PATH 设为 brew --prefix qt@6。

## 构建（Windows，MSYS2 mingw64）

本机已验证的组合是 gcc 16.2 + CMake 4.4.4 + Qt 6.12.0（pacman 的 mingw-w64-x86_64-qt6-base）。cmark-gfm 不在 MSYS2 仓库里（只有无 GFM 扩展的 cmark），需从源码构建：

    winget install MSYS2.MSYS2
    pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
      mingw-w64-x86_64-ninja mingw-w64-x86_64-pkg-config mingw-w64-x86_64-qt6-base
    curl -L -o cmark.tar.gz https://github.com/github/cmark-gfm/archive/refs/tags/0.29.0.gfm.13.tar.gz
    tar xzf cmark.tar.gz
    cmake -S cmark-gfm-0.29.0.gfm.13 -B cmark-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    cmake --build cmark-build && cmake --install cmark-build
    cmake --preset windows-mingw
    cmake --build build/windows-mingw
    ctest --test-dir build/windows-mingw --output-on-failure

Qt 的 PrintTest 模块在 MSYS2 包里不存在，已在 CMake 里降级为可选组件；PrintSupport 本身是存在的（found 变量名是 Qt6PrintSupport_FOUND）。

## 许可

本仓库应用本体为 MIT。Qt 以 LGPLv3 动态链接使用，发布时随附「用户可自行替换或重链接 Qt」说明；cmark-gfm 为 BSD-2，其版权声明在打包时保留。
