# Pixel Fol Tool

> A tool for extracting, modifying, and repacking `.fol` resource archives from Pixel Software games. One shared C/C++ core with multiple frontends: Windows CLI, Windows GUI, and macOS GUI. A standalone Python implementation is also included for scripting and reference. See [FOL 文件格式分析](#fol-文件格式分析) for the archive format details.

**免责声明：本工具仅供学习研究和技术交流使用。请确保你对处理的资源文件拥有合法权利，并遵守适用法律与游戏资源授权。**

## 功能特性

- 解包 / 修改 / 重新打包 `.fol` 归档，多项式加密自动处理
- 一套共享核心（`core/`，稳定 C ABI），多套前端：Windows Terminal CLI、Win32 GUI、macOS SwiftUI GUI
- 独立 Python 实现，适合跨平台脚本自动化
- Round-trip 回归测试：动态生成 synthetic 样例，不依赖第三方游戏资源

## 项目结构

- `core/`: 共享 FOL 核心，提供稳定的 C ABI，实际逻辑由 C++ 实现。
- `c/`: 基于共享核心的 CLI 入口，以及 macOS 本地构建用 `makefile`。
- `gui/`: Windows Win32 GUI，负责界面、线程和日志展示。
- `mac/`: SwiftUI macOS GUI，通过 `Process` 调用 `c/` 中构建出的 CLI。
- `python/`: 独立的 Python 实现。
- `tests/`: 回归测试脚本。测试默认动态生成 synthetic `.fol`，不提交游戏资源样例。
- `scripts/`: 发布与 CI 打包脚本。
- `docs/`: 架构说明与测试说明。
- `CHANGELOG.md`: 变更记录，发布 tag 形如 `v1.0.1`。

## 环境要求

- Windows：Visual Studio 2022（MSBuild，Developer PowerShell）
- macOS：Xcode Command Line Tools（`clang++`，C++20）
- Python（可选）：3.6+

## 构建

### Windows（GUI + CLI）

使用 Visual Studio 2022 或 Developer PowerShell，根级解决方案一次构建全部产物：

```powershell
msbuild pixel-fol-tool.sln /p:Configuration=Release /p:Platform=x64
```

也可单独构建某个工程（输出目录不变）：

```powershell
msbuild c/FolToolCli.vcxproj /p:Configuration=Release /p:Platform=x64
msbuild gui/FolToolG.vcxproj /p:Configuration=Release /p:Platform=x64
```

构建输出统一在 `bin\<Platform>\<Configuration>\`（如 `bin\x64\Release\FolToolG.exe`、`bin\x64\Release\FolToolCli.exe`），中间产物收拢在 `build\obj\`。

### macOS 共享 CLI

```bash
cd c
make mac
```

生成物为 `bin/mac/fol_tool_mac`。`mac/` 图形界面会优先复用它，不存在时自动执行一次本地构建。另提供 `make windows`（需 `mingw-w64`）与 `make linux` 交叉编译目标。

### 本地发布集合

```powershell
pwsh scripts/publish.ps1
```

将 `bin\x64\Release\` 下的可执行文件拷贝到 `release\`，作为对外发布的稳定集合。调试符号（PDB）不会进入发布集合。

### Python

```bash
cd python
python fol_tool.py -h
```

## GitHub 发布（CI）

发布 PR 合并到 `main` 后，GitHub Actions 会验证 Windows x64/x86、macOS 与 Python 前端，运行 synthetic round-trip，并在版本 tag 尚不存在时自动创建对应 tag 和 GitHub Release。

发布包由 `scripts/prepare_release_package.ps1` 生成：从 `gui/FolToolWin.rc` 读取版本号，从 `CHANGELOG.md` 提取对应 tag 章节作为 Release 说明。当前 Release 提供 Windows x64/x86 的 Release EXE，不包含 PDB；macOS 用户可按上文说明从源码构建。

## Round-Trip 测试

推荐使用共享 CLI 做本地回归：

1. 生成或选择一个你有权使用的 `.fol`
2. 解包到目标目录（文件直接解到该目录下，无额外子目录）
3. 修改任意解包出的文件
4. 重新打包（直接选中该目录）
5. 再次解包到新目录
6. 校验修改后的文件字节内容一致

Windows PowerShell 可直接运行：

```powershell
pwsh -File tests/run-roundtrip.ps1 -CliPath bin\x64\Release\FolToolCli.exe
```

脚本会先用 synthetic 工作区生成一个最小 `.fol`，再执行完整 round-trip。也可以传入你有权使用的外部样例：

```powershell
pwsh -File tests/run-roundtrip.ps1 -CliPath bin\x64\Release\FolToolCli.exe -InputFol path\to\sample.fol
```

更详细的结构与测试说明见 [docs/architecture.md](docs/architecture.md) 和 [docs/testing.md](docs/testing.md)。

---

## FOL 文件格式分析

.fol 格式是像素软件用于存储游戏资源的归档格式，具有自定义的头部结构和多项式加密保护。

文件采用小端序（Little-Endian）存储。

| 区域          | 大小                | 描述                            |
|-------------|-------------------|-------------------------------|
| Header      | 4 Bytes           | 包含文件数量与加密标志位。 |
| Index Table | Count * 136 Bytes | 加密索引表。每项 136 字节，包含文件名、偏移量与大小。 |
| Data Block  | Variable          | 文件数据区。每个文件的数据内容单独加密。 |
| Key Table   | Count * 4 Bytes   | 每个文件对应一个 32 位密钥。 |
| Padding     | 388 Bytes         | 固定为 97 个 `uint32` 的零填充。 |

### Header（文件头）

- 类型：`int32`
- 逻辑：
  - 最高位（Bit 31）为 `1` 表示文件已加密。
  - 低 31 位表示文件总数 `FileCount`。
  - 解析公式：`RealCount = Header & 0x7FFFFFFF`

### Index Entry（索引项）

每个索引项固定 136 字节，可按 34 个 `uint32` 处理：

- `0..127`：文件路径，最多 128 字节，通常使用 Windows 风格反斜杠
- `128..131`：偏移量 `offset`
- `132..135`：文件大小 `size`

实现时需要注意：

- 一些样本中的 `offset` 直接指向数据区绝对偏移；
- 也存在样本把 `offset` 记录成相对 `Data Block` 起点的写法；
- 兼容实现可在 `offset < data_base_offset` 时补上 `data_base_offset`。

### 加密算法

该格式使用基于多项式的逐 `uint32` 运算。

索引项加密：

- 加密：`v = raw + key + 9 * i^3`
- 解密：`v = raw - key - 9 * i^3`
- `i` 为当前 `uint32` 在索引项中的位置，范围 `0..33`

文件内容加密：

- 加密：`v = raw + key + 99 * i^2`
- 解密：`v = raw - key - 99 * i^2`
- `i` 为当前 `uint32` 在文件流中的位置

如果文件长度不是 4 的倍数，尾部不足 4 字节的剩余字节保持原样，不参与运算。

### Footer（尾部）

读取密钥表时可按以下偏移定位：

`SEEK_END - 4 * (97 + FileCount)`

因此写入流程需要在 `Key Table` 后追加 97 个 `uint32` 零填充。

---

## 开源许可与贡献

本项目基于 [GPL-3.0](LICENSE) 开源。欢迎以 Issue 报告问题、以 Pull Request 提交改进：

- 涉及归档协议或打包规则的改动请放在 `core/`，前端只做参数与展示适配；
- 提交前运行 `tests/run-roundtrip.ps1` 确认回归通过；
- GUI 改动请在 PR 中附截图，说明影响的平台与验证步骤；
- 完整变更记录见 [CHANGELOG.md](CHANGELOG.md)。
