# 发布与打包

Typist 的发布包由 `.github/workflows/release.yml` 在打 `v*` 标签时构建，三个平台作业并行跑，最后由 `release` 作业汇总到 GitHub Releases。

修改打包配置的 PR 和手动运行也会构建三平台安装包、检查部署后的程序能否启动，并汇总上传 `typist-release` Artifact；只有 `v*` 标签会执行 GitHub Releases 发布。验证运行的 `latest.json` 使用 `0.0.0-validation` 版本，不作为正式更新源。不要重跑旧标签上的失败工作流来验证代码修复：重跑仍会使用旧提交，应先验证修复分支，再从包含修复的提交创建发布标签。

## 产物
- Windows：`Typist-win64.zip`，暂存树经 `windeployqt` 补齐 Qt DLL 与插件后，`deploy-runtime.cmake` 递归收集程序和插件依赖的 MinGW、ICU 等运行库；启动检查使用只包含发布目录和 Windows 系统目录的 PATH。
- Linux：`Typist-*.deb`、`Typist-*.rpm`、`Typist-*.tar.gz`，由 `cpack --config build/linux/CPackConfig.cmake -G "DEB;RPM;TGZ" -C Release -B dist` 生成。
- macOS：`Typist-*.dmg`、`Typist-*.tar.gz`，`Typist.app` 经 `macdeployqt` 补齐依赖后，由 CPack 的 `DragNDrop` 和 `TGZ` 生成器打包。
- 每个包都有 `checksums.txt`（SHA-256），`release` 作业还会写 `latest.json`（版本号、发布时间、资产 URL），作为更新源的清单。
- Windows 还有 NSIS 安装器：runner 上 `choco install nsis`，脚本 `packaging/windows/typist.nsi` 把 staging 树装进 `$PROGRAMFILES64\Typist`，写卸载与 App Paths 注册表项、开始菜单与桌面快捷方式。
- Linux 还有 AppImage：使用 `DESTDIR` 将程序安装到 AppDir 的 `usr/` 下，再由 linuxdeploy 与 Qt 插件打包；显式使用 Qt 6 的 `qmake6`，以 extract-and-run 模式避开 runner 的 FUSE 依赖。
- 本机没有 NSIS、linuxdeploy、AppImage 工具链（按既有习惯不装依赖），这两类包只能在 runner 上验证；图标是手写 SVG 与 bundled Pillow 画的 PNG。
- 安装树内容：`bin/typist`（macOS 为 `Typist.app`）、`share/typist/themes/default.qss`、`share/typist/translations/*.qm`、`share/typist/docs/`、`share/typist/LICENSE`、`share/typist/NOTICE`。发布作业显式安装 Linguist 并编译 `.qm`，编译失败会中止打包；Ubuntu 使用 `/usr/lib/qt6/bin/lrelease`。
- 三个平台的包统一输出到 `dist/`，下载工具不会被当作发布包上传。`packaging/prepare_release.py` 汇总包文件，生成只含包文件的 SHA-256 清单和使用文件名的下载 URL，并拒绝跨平台重名包、空产物和残留输出。

## 签名
- Windows：配了 `WINDOWS_CODESIGN_PFX` secret 与 `SIGNTOOL_PATH` repository variable 时用 `signtool` 签 `stage/bin/typist.exe`（SHA-256 摘要）；没配就发布未签名版本，作业会明确打印 `shipping unsigned`。
- macOS：暂不做签名与公证；需要时在本机用开发者证书处理，CI 不伪造签名。
- Linux：暂不做签名；包内容完整性由 SHA-256 保证。
- 签名证书只走正规渠道（自购证书或开源免费证书），本仓库不提供任何绕过签名的做法。

## 自动更新
- 更新通道是 GitHub Releases：`latest.json` 给出版本与资产地址。应用内已实现检查（Ctrl+Shift+U：抓 `latest.json`、比对版本、状态栏提示有新版本或已是最新，超时 5 秒），但不自动下载与安装；下载后必须先核对 `checksums.txt` 里的 SHA-256 再安装。
- 默认更新源写死为 `https://github.com/moxi486/typist/releases/latest/download/latest.json`（真实上游仓库），fork 用环境变量 `TYPIST_UPDATE_URL` 覆盖自己的发布通道。

## 本机验证到什么程度
- 本机（Windows + MSYS2 + MinGW）跑通了 `cmake --install` 暂存树、`windeployqt`、逐文件 SHA-256 与 tar 打包（脚本 `.deps/pkg_stage.sh`），实测暂存 24 个文件、包约 12.97MB。
- CPack 的 ZIP 生成器在本机验证过；deb/rpm 与 dmg 只能在对应平台的 runner 上验证，本机没有 `dpkg-deb`、`rpmbuild`、`hdiutil`。
- 工作流语法可用 actionlint 检查；发布清单的回归测试使用 `python3 -m unittest discover -s tests/packaging -v`，覆盖安装器、AppImage、下载 URL、哈希、同名包及残留产物。
