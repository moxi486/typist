# Typist

开源的 Typora 式所见即所得 Markdown 编辑器：单栏编辑、无独立预览区，源码与渲染在同一视图切换。原生跨平台桌面应用（Windows / Linux / macOS）。

独立实现，与 Typora 官方无任何隶属关系，不使用 Typora 的资源、图标或名称混用。

## 当前可用功能

- 打开 / 保存 Markdown（原子写入）
- 撤销 / 重做，历史上限 200 步
- 同一视图内源码与渲染切换（Ctrl+/），两种视图各留一份撤销历史；内容语义相同则切换不重建文档
- 导出 HTML（Ctrl+Shift+E，走 cmark-gfm 的 GFM 扩展）
- 导出 PDF（Ctrl+Shift+P，走 Qt QPrinter 的 PdfFormat）
- 主题 light / dark / system（Ctrl+Shift+D 循环，偏好写入 QSettings；导出的 HTML 内嵌同一份主题样式）
- 自动保存（最后一次编辑后 2 秒落盘；未命名文件不自动保存；切换源码/渲染视图与关闭窗口时立即落盘）
- 往返保真测试：parse → serialize → parse 产生相同 AST
- 性能基线：`perf_baseline` 与 `repaint_baseline`（ctest 项）打印打开耗时与打字到重绘耗时；本机 Windows Release 实测冷启动 295ms、常驻工作集 21.7MB、1MB 文档 realistic 117ms、tableheavy 244ms、打字到重绘 0–1ms

## 性能预算（验收数字）

- 冷启动 < 400ms（本机 Windows Release 实测 295ms；offscreen 的 17ms 只作 proxy）
- 常驻内存 < 80MB（本机实测工作集 21.7MB）
- 打开 1MB Markdown < 250ms（本机实测 realistic 117ms、tableheavy 244ms）
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
