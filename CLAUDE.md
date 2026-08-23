# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

Pixel Fol Tool 用于解包、修改并重新打包 Pixel Software 游戏中的 `.fol` 资源归档。仓库结构是"一套共享核心、多套前端"：归档协议逻辑只在 `core/` 实现一次（对外暴露稳定的 C ABI），各前端只做薄适配：

- `c/`：共享核心的 CLI 入口（`fol_tool.cpp` 内含 Windows `wmain` 与 POSIX `main` 两套入口），也是 macOS GUI 的后端。
- `gui/`：Windows Win32 GUI，直接链接共享核心，把核心日志与错误码翻译成中文展示。
- `mac/`：SwiftUI macOS GUI，通过 `Process` 子进程调用 `c/` 构建出的 CLI，不直接链接核心。
- `python/`：**独立**的 Python 实现，与 C/C++ 核心不同步——协议改动必须手动镜像到 `python/fol_tool.py`。
- `docs/`：架构与测试说明；`tests/`：round-trip 回归脚本；`scripts/`：发布脚本。

## 构建与测试

```powershell
# Windows：根级解决方案一次构建 GUI + CLI（VS2022 / Developer PowerShell）
msbuild pixel-fol-tool.sln /p:Configuration=Release /p:Platform=x64
# 产物统一在 bin\<Platform>\<Configuration>\（如 bin\x64\Release\FolToolG.exe、FolToolCli.exe）

# 也可只构建单个工程
msbuild c/FolToolCli.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild gui/FolToolG.vcxproj /p:Configuration=Release /p:Platform=x64
```

```bash
# macOS CLI（及 mingw-w64 交叉编译 Windows / Linux）
cd c && make mac        # 产物 bin/mac/fol_tool_mac
cd c && make windows    # 需要 x86_64-w64-mingw32-g++
```

```powershell
# 发布集合：拷贝 bin\x64\Release\ 到 release\
pwsh scripts/publish.ps1

# Round-trip 回归测试（默认生成 synthetic .fol，不提交游戏资源样例）
pwsh -File tests/run-roundtrip.ps1 -CliPath bin\x64\Release\FolToolCli.exe
# 也可指定外部样例：-InputFol path\to\sample.fol
```

```bash
# Python 独立版
cd python && python fol_tool.py unpack mb.fol -o extracted_folder
```

测试运行产物一律放 `tmp/`，不提交。macOS GUI 运行方式：`cd mac && swift run FolToolMac`（首次运行会自动构建缺失的 CLI）。

## 架构要点

### C ABI 契约（`core/include/fol_core.h`）

核心只暴露 3 个函数：`fol_unpack`、`fol_pack`、`fol_result_message`。约定：

- 路径参数一律 **UTF-8**（Windows 前端负责宽字符↔UTF-8 转换）。
- 日志/进度经 `FolLogCallback(progress, level, message, user_data)` 上报，`progress` 为 0–100 整数，`message` 是 UTF-8 文本，带固定英文前缀（如 `Starting unpack: `、`Extracted: `、`Unpack completed`、`Packed: `、`Pack completed: `）。
- 错误码为 0–8 的整数枚举（`FOL_SUCCESS` 到 `FOL_ERROR_FILESYSTEM`），`fol_result_message` 返回英文描述。

### FOL 归档格式（`core/src/fol_core.cpp`，小端序）

- **Header** `int32`：Bit 31 为加密标志（必须为 1），低 31 位是文件数 `count`。
- **Index table** `count * 136` 字节：每项 = `path[128]`（Windows 反斜杠路径）+ `offset`(4) + `size`(4)，按 34 个 `uint32` 加密：`v ±= key ± 9*i³`。
- **Data block**：每个文件内容独立加密，按 `uint32` 流：`v ±= key ± 99*i²`；尾部不足 4 字节的原样保留不参与运算。
- **Key table**：`count * 4` 字节，位于 `SEEK_END - 4*(97+count)`；其后固定 97 个 `uint32` 零填充（`kPaddingCount`）。
- 打包时每个文件随机生成 32 位密钥（同一密钥加密该文件的索引项与内容），条目按 `game_path` 排序写入（游戏读取不依赖顺序）。

### 跨平台编码陷阱（最容易踩坑的地方）

归档内部路径是 **GB2312（CP936）+ 反斜杠**，而 C ABI 和日志是 UTF-8：

- Windows 上 `game_path_from_relative_path`（打包方向）把 UTF-8 本地路径转成 GB2312 写入索引；`local_path_from_game_path`（解包方向）从 GB2312 转回本地路径。改编码逻辑时必须同时看这两处和 `log_text_from_game_path`。
- 非 Windows 平台路径按 UTF-8 透传，仅做 `/`↔`\` 分隔符转换。
- GUI 的 `gui/FolCore.cpp` 里 `translate_core_message` 按**英文前缀**把核心日志翻译成中文——**改动 core 的日志字符串必须同步更新该翻译表**，否则 GUI 显示原文；错误码翻译在 `translate_result_message`。

### 解包/打包行为约定

- 解包直接把文件写到所选目录（无 `assets/` 子目录，无 manifest 清单）；目录里 `key table` 前的零填充、空文件（size 0）等边界情况都要兼容。
- 存在两种 offset 语义的样本：绝对偏移，以及相对 Data Block 起点的偏移。兼容写法：`offset < data_base && offset > 0` 时补上 `data_base`。
- 路径安全校验 `safe_output_path`：拒绝空路径、绝对路径、`..`、NUL 字节和 Windows 保留名（CON/PRN/AUX/COM1…LPT9）；打包时 `scan_workspace_files` 同样复用该校验。改动路径逻辑时保持此防护。
- 解包跳过 `.DS_Store` 文件；打包扫描递归目录、按路径排序、每个文件独立随机密钥——这保证 round-trip 后字节一致。

### 前端职责

- `c/`：CLI 很薄，只做参数解析（`unpack <input.fol> [dir]` / `pack <dir> [output.fol]`，缺省目录名/文件名逻辑在 `fol_tool.cpp` 里）与日志前缀格式化（`[*]/[!]/[x] + 进度 + 消息`）。macOS GUI 解析 CLI 的 stdout 显示进度，改动 CLI 输出格式会影响 `mac/`。
- `gui/`：Win32 界面 + 线程管理，核心调用本身是同步阻塞的，进度经回调驱动 UI。
- 归档协议或打包规则改动放 `core/`；前端只处理参数、展示与本地化。

### 测试思路

`tests/run-roundtrip.ps1` 的流程是：生成 synthetic 归档（或解包外部样例）→ 解包 → 追加修改标记 → 重新打包 → 再次解包 → 逐字节比对。核心改动后跑一次即可验证加解密、路径、排序的完整性。新增测试沿用同一 round-trip 模式。

## 发布流程

- PR 触发 `.github/workflows/build.yml`：验证 Windows x64/x86、macOS 与 Python，并对两个 Windows 架构执行 synthetic round-trip。合并到 `main` 后，版本 tag 不存在时自动创建 GitHub Release。
- 打包脚本 `scripts/prepare_release_package.ps1` 从 `gui/FolToolWin.rc` 的 `FILEVERSION` 读取语义化版本，并要求 `CHANGELOG.md` 存在完全匹配的 tag 章节；版本或发布文件不一致时直接失败。
- 本地测试样例 `.fol` 不提交仓库，放 `tmp/fols/`（gitignored）下。

## 代码风格与提交规范

遵循 AGENTS.md：C/C++ 4 空格缩进、大括号独占一行、函数 `snake_case`；不引入格式化工具。提交信息用短句式（如 `feat(mac): ...`、`fix(core): ...`），≤72 字符。
