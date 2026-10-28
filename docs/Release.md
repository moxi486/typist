# 发布与打包

Typist 的发布包由 `.github/workflows/release.yml` 在打 `v*` 标签时构建，三个平台作业并行跑，最后由 `release` 作业汇总到 GitHub Releases。

## 产物
- Windows：`Typist-win64.zip`，`cmake --install` 的暂存树经 `windeployqt` 补齐 Qt 运行时 DLL（Qt6Core/Gui/Widgets、platforms、styles、imageformats），整个目录打包。
- Linux：`Typist-*.deb`、`Typist-*.rpm`、`Typist-*.tar.gz`，由 CPack 生成（生成器与安装前缀在根 `CMakeLists.txt` 里设定）。
- macOS：`Typist-*.dmg`、`Typist-*.tar.gz`，由 CPack 生成。
- 每个包都有 `checksums.txt`（SHA-256），`release` 作业还会写 `latest.json`（版本号、发布时间、资产 URL），作为更新源的清单。
- Windows 还有 NSIS 安装器：runner 上 `choco install nsis`，脚本 `packaging/windows/typist.nsi` 把 staging 树装进 `$PROGRAMFILES64\Typist`，写卸载与 App Paths 注册表项、开始菜单与桌面快捷方式。
- Linux 还有 AppImage：runner 下载 linuxdeploy 与其 Qt 插件的 continuous 版，用 `packaging/linux/typist.desktop` 与 `packaging/linux/typist.png` 打包 staging 树。
- 本机没有 NSIS、linuxdeploy、AppImage 工具链（按既有习惯不装依赖），这两类包只能在 runner 上验证；图标是手写 SVG 与 bundled Pillow 画的 PNG。
- 安装树内容：`bin/typist`、`share/typist/themes/default.qss`、`share/typist/translations/*.qm`（由作业里的 `lrelease` 编译，本机没装 Linguist 时该文件缺失是预期）、`share/typist/docs/`、`share/typist/LICENSE`、`share/typist/NOTICE`。

## 签名
- Windows：配了 `WINDOWS_CODESIGN_PFX` 与 `SIGNTOOL_PATH` 时用 `signtool` 签 `stage/bin/typist.exe`（SHA-256 摘要）；没配就发布未签名版本，作业会明确打印 `shipping unsigned`。
- macOS：暂不做签名与公证；需要时在本机用开发者证书处理，CI 不伪造签名。
- Linux：暂不做签名；包内容完整性由 SHA-256 保证。
- 签名证书只走正规渠道（自购证书或开源免费证书），本仓库不提供任何绕过签名的做法。

## 自动更新
- 更新通道是 GitHub Releases：`latest.json` 给出版本与资产地址。应用内已实现检查（Ctrl+Shift+U：抓 `latest.json`、比对版本、状态栏提示有新版本或已是最新，超时 5 秒），但不自动下载与安装；下载后必须先核对 `checksums.txt` 里的 SHA-256 再安装。
- 默认更新源写死为 `https://github.com/moxi486/typist/releases/latest/download/latest.json`（真实上游仓库），fork 用环境变量 `TYPIST_UPDATE_URL` 覆盖自己的发布通道。

## 本机验证到什么程度
- 本机（Windows + MSYS2 + MinGW）跑通了 `cmake --install` 暂存树、`windeployqt`、逐文件 SHA-256 与 tar 打包（脚本 `.deps/pkg_stage.sh`），实测暂存 24 个文件、包约 12.97MB。
- CPack 的 ZIP 生成器在本机验证过；deb/rpm 与 dmg 只能在对应平台的 runner 上验证，本机没有 `dpkg-deb`、`rpmbuild`、`hdiutil`。
- `release.yml` 的 YAML 语法只能在 runner 上确认；本机只做缩进与制表符静态检查。
