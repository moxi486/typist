# 构建与测试

## 依赖
- Qt 6（Qt6Core/Qt6Gui/Qt6Widgets；导出 PDF 只用 QtGui 的 QPdfWriter，不依赖 PrintSupport）
- cmark-gfm 0.29.0.gfm.13（含 table、tasklist、strikethrough、autolink 扩展）
- CMake >= 3.21 与 Ninja；C++17

## 各平台
- Windows（MSYS2 mingw64）：装 `mingw-w64-x86_64-{gcc,cmake,ninja,pkg-config,qt6-base}`，cmark-gfm 从源码装到 `/usr/local`，然后 `cmake -S . -B build/mingw -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=/mingw64;/usr/local"`，`cmake --build build/mingw`，`ctest --test-dir build/mingw`。
- Linux：`apt-get install libcmark-gfm-dev libcmark-gfm-extensions-dev qt6-base-dev`，`cmake --preset linux-gcc`。
- macOS：`brew install cmark-gfm qt@6`，`export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"`，`cmake --preset macos-clang`。
- Windows MSVC：`cmake --preset windows-msvc`（仅在 CI 里验证，开发机没有 cl.exe）。

## 测试与测量
- `ctest` 覆盖往返保真、幂等、视图切换、主题判定、翻译覆盖与性能基线。
- `tests/editor/roundtrip_spike.exe` 是逐 slot 的富断言测试（当前 78 slot），跑之前必须先构建，脚本不会替你构建。
- 性能：`typist --bench-startup --bench-repaint --bench-view-switch --bench-search --bench-file-search --bench-tree --bench-docx`；`tests/perf/perf_bench.exe` 打印 1MB 文档的 parse/build/serialize/total 毫秒与 `fidelity=ok`。
- CI 的 Performance gate 门限：realistic `total_ms` <= 250，tableheavy <= 300，且必须出现 `fidelity=ok`。
