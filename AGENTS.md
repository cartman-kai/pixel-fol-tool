# Repository Guidelines

## 项目结构与模块组织
本仓库围绕一套共享 `.fol` 核心组织，多端只保留薄适配层：

- `core/`：共享 C ABI 与 C++ 实现，负责解包、打包、`manifest.txt`、路径与日志回调。
- `c/`：命令行入口与 macOS 本地构建 `makefile`。
- `gui/`：Windows Win32 图形界面，负责交互、线程、日志与进度展示。
- `mac/`：SwiftUI macOS 界面，通过 `Process` 调用 `c/` 中构建出的 CLI。
- `python/`：独立 Python 实现，保留给脚本化和参考用途。
- `tests/fixtures/fols/`：提交到仓库的测试样例 `.fol` 文件。
- `docs/`：架构与测试说明。

涉及归档协议、清单格式或排序规则的改动应放在 `core/`，前端只处理参数和展示。

## 构建、测试与开发命令
- `cd c && make mac`：构建 macOS CLI，产物为 `c/fol_tool_mac`。
- `cd c && make windows`：在安装 `mingw-w64` 时交叉编译 Windows CLI。
- `msbuild c/FolToolCli.vcxproj /p:Configuration=Release /p:Platform=x64`：构建 Windows Terminal CLI。
- `msbuild gui/FolToolG.sln /p:Configuration=Release /p:Platform=x64`：构建 Windows GUI。
- `cd mac && swift run FolToolMac`：运行 macOS GUI。
- `cd python && python fol_tool.py -h`：查看 Python 版 CLI 帮助。
- `pwsh -File tests/run-roundtrip.ps1 -CliPath c\x64\Release\FolToolCli.exe -InputFol tests/fixtures/fols/slr.fol`：执行解包-修改-重打包-再解包校验。

## 代码风格与命名约定
- C/C++：4 空格缩进，大括号独占一行，函数使用 `snake_case`，宏使用 `UPPER_CASE`。
- Python：4 空格缩进，函数与方法使用 `snake_case`，类使用 `PascalCase`。
- Swift：4 空格缩进，成员使用 `camelCase`，类型使用 `PascalCase`。

优先保持现有风格，避免引入新的格式化工具或大范围重排。

## 测试规范
当前以样例归档的回归测试为主：

- 从 `tests/fixtures/fols/` 选择样例执行解包；
- 修改 `assets/` 下至少一个文件；
- 重新打包并再次解包；
- 校验修改后的文件字节内容完全一致；
- GUI 改动需覆盖成功路径与失败路径。

运行产物放在 `tmp/`，不要提交临时输出目录。

## 提交与 Pull Request 规范
提交信息沿用现有短句风格，常见形式如 `feat(mac): ...`、`fix(core): ...`。建议使用祈使句并控制在 72 字符以内。

Pull Request 需要说明影响的平台、用户可见变化、验证步骤；GUI 改动附截图；涉及发布流程时同步说明 `.github/workflows/build-release.yml` 的影响。
