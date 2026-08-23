# Changelog

本项目所有重要变更记录于此文件。版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)，发布 tag 形如 `v1.0.0`，推送后由 GitHub Actions 自动构建并发布。

## Unreleased

### Added

- 共享 C ABI 核心 `core/`：解包/打包协议逻辑唯一实现，统一错误码、日志级别与进度回调
- Windows CLI（`FolToolCli`）与 Win32 GUI（`FolToolG`），根级解决方案 `pixel-fol-tool.sln` 一次构建，产物统一输出到 `bin/<Platform>/<Configuration>/`
- macOS SwiftUI GUI（`FolToolMac`），通过子进程调用共享 CLI，缺失时自动构建
- 独立 Python 实现 `python/fol_tool.py`（与 C/C++ 核心不同步，保留为脚本化参考）
- Round-trip 回归测试脚本 `tests/run-roundtrip.ps1`：动态生成 synthetic 样例，不依赖第三方游戏资源
- GitHub Actions CI：PR 与分支推送执行双平台构建检查；推送 `v*` tag 自动构建、打包并创建 GitHub Release

### Changed

- 归档协议与打包规则统一收拢到 `core/`，各前端只做参数与展示适配
- 日志与界面本地化，字符集切换为 Unicode
- 发布脚本 `scripts/publish.ps1` 统一收集 `bin\x64\Release\` 产物到 `release\`

## v0.0.1

- FOL 归档格式逆向分析与实现（多项式加密、索引/密钥表布局）
- Python 独立实现 `python/fol_tool.py`
- C 版本 CLI 初始实现
