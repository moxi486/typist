# Typist

开源的 Typora 式所见即所得 Markdown 编辑器：单栏编辑、无独立预览区，源码与渲染在同一视图切换。原生跨平台桌面应用（Windows / Linux / macOS）。

独立实现，与 Typora 官方无任何隶属关系，不使用 Typora 的资源、图标或名称混用。

## 性能预算（验收数字）

- 冷启动 < 400ms
- 常驻内存 < 80MB
- 打开 1MB Markdown < 250ms
- 输入到重绘 < 16ms

## 技术栈

Qt 6 Widgets（C++17）+ QTextDocument 富文本模型 + cmark-gfm（GFM 扩展：表格、任务列表、删除线）。基线 Qt 6.8 LTS，目标 6.12 LTS。Markdown 与富文本之间的双向序列化器位于 src/core，往返保真是验收标准。

## 构建（Linux）

    sudo apt install qt6-base-dev libcmark-gfm-dev libcmark-gfm-extensions-dev
    cmake --preset linux-gcc
    cmake --build build/linux-gcc
    ctest --test-dir build/linux-gcc --output-on-failure

macOS：brew install cmark-gfm qt@6，并把 CMAKE_PREFIX_PATH 设为 brew --prefix qt@6。Windows 的 Qt 安装参数尚未核实，见 docs/ROADMAP.md 的开放项。

## 许可

本仓库应用本体为 MIT。Qt 以 LGPLv3 动态链接使用，发布时随附「用户可自行替换或重链接 Qt」说明；cmark-gfm 为 BSD-2，其版权声明在打包时保留。
