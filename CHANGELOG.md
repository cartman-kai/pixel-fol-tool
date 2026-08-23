# Changelog

本项目所有重要变更记录于此文件。版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)，发布 tag 形如 `v1.0.1`。发布 PR 合并到 `main` 后，由 GitHub Actions 自动构建、打 tag 并创建 Release。

## Unreleased

暂无。

## v1.0.1 - 2026-08-23

### Added

- 共享 C ABI 核心 `core/`：解包/打包协议逻辑唯一实现，统一错误码、日志级别与进度回调
- Windows CLI（`FolToolCli`）与 Win32 GUI（`FolToolG`），根级解决方案 `pixel-fol-tool.sln` 一次构建，产物统一输出到 `bin/<Platform>/<Configuration>/`
- macOS SwiftUI GUI（`FolToolMac`），通过子进程调用共享 CLI，缺失时自动构建
- 独立 Python 实现 `python/fol_tool.py`（与 C/C++ 核心不同步，保留为脚本化参考）
- Round-trip 回归测试脚本 `tests/run-roundtrip.ps1`：动态生成 synthetic 样例，不依赖第三方游戏资源
- GitHub Actions CI：PR 验证 Windows x64/x86、macOS 与 Python；发布 PR 合并到 `main` 后自动构建、打包并创建 GitHub Release
- 共享 C ABI 新增第 4 个函数 `fol_list`：列出归档内容（路径 + 大小），GUI 归档预览与 Python `list` 子命令基于此实现

### Changed

- 归档协议与打包规则统一收拢到 `core/`，各前端只做参数与展示适配
- 日志与界面本地化，字符集切换为 Unicode
- 发布脚本 `scripts/publish.ps1` 统一收集 `bin\x64\Release\` 产物到 `release\`
- Windows 发布包仅包含 Release EXE，并附带许可证、说明文档与 SHA-256 校验值，不包含 PDB
- Windows x64/x86 Release 统一静态链接 MSVC 运行库，减少独立运行时依赖
- Win32 GUI 重构为可缩放主窗口：解包/打包 Tab 切换，路径框可手动输入，支持拖拽 `.fol` 文件与目录，归档内容预览（ListView），分级彩色日志（RichEdit），暗色模式跟随系统（Win11 启用 Mica 背景），支持命令行/关联打开 `.fol`

## v0.0.1

- FOL 归档格式逆向分析与实现（多项式加密、索引/密钥表布局）
- Python 独立实现 `python/fol_tool.py`
- C 版本 CLI 初始实现
