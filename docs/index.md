# Typist 文档

Typist 是一个开源的所见即所得 Markdown 编辑器：源码与渲染在同一视图切换，没有独立预览区。原生桌面应用，支持 Windows、Linux 与 macOS，追求高性能与低资源占用。

## 页面
- [构建](Build.md)：依赖、三平台构建方式与 CMake presets
- [快捷键](Keyboard.md)：全部键位与冲突说明
- [主题与配置](Themes.md)：light / dark / system 与用户配置项
- [翻译与多语言](Translations.md)：`.ts` 目录、`lupdate`/`lrelease` 与运行时加载
- [发布与打包](Release.md)：三平台产物、SHA-256、签名与更新通道
- [路线图与实现事实](ROADMAP.md)：里程碑、已知限制与排查过的技术事实

## 快速上手
1. 安装 Qt 6（基线 6.8 LTS，目标 6.12 LTS）与 cmark-gfm（含 GFM 扩展：表格、任务列表、删除线）。
2. `cmake --preset windows-mingw`（或 `linux-gcc` / `macos-clang`），然后 `cmake --build build/<preset>`。
3. `ctest --test-dir build/<preset>`：往返保真、性能基线与 UI 接线都在测试里。
4. 运行 `typist`：直接编辑 Markdown，`Ctrl+/` 切换源码视图，`Ctrl+Shift+E/P/X` 导出 HTML/PDF/DOCX；通过文件菜单 [导出 EPUB 电子书](EPUB.md)。
5. 通过文件菜单 [导入 HTML](Import.md) 预览网页内容，或用 `Ctrl+Alt+V` 将网页富文本粘贴为 Markdown。
6. [PDF 页面设置与预览](PDF.md)：导出前检查纸张、方向、页边距、页眉页脚和分页。
7. [快速打开](Quick-Open.md)：`Ctrl+P` 模糊查找工作区和最近文件，使用键盘选择打开。
8. [文件侧栏](File-Browser.md)：文件树 / 列表、自然顺序或修改时间排序、显示偏好与刷新。
9. [工作区全文搜索](Workspace-Search.md)：`Ctrl+Shift+F` 搜索已保存的 Markdown，按文件分组并定位源码行。

许可：应用为 MIT；Qt 以 LGPLv3 动态链接并提供可重链接说明；cmark-gfm 为 BSD-2。第三方许可声明见仓库根目录的 `NOTICE`。
