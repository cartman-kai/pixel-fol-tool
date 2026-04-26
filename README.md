# Pixel Fol Tool

**免责声明：本工具仅供学习研究和技术交流使用。请确保你对处理的资源文件拥有合法权利，并遵守适用法律与游戏资源授权。**

用于提取、修改并重新打包 Pixel Software 游戏中的 `.fol` 资源文件。当前仓库采用“一套共享核心，多套前端”的结构：Windows Terminal、Windows GUI、macOS CLI 共用同一套 C/C++ 核心实现；Python 版本保留为独立脚本实现。

## 项目结构

- `core/`: 共享 FOL 核心，提供稳定的 C ABI，实际逻辑由 C++ 实现。
- `c/`: 基于共享核心的 CLI 入口，以及 macOS 本地构建用 `makefile`。
- `gui/`: Windows Win32 GUI，负责界面、线程和日志展示。
- `mac/`: SwiftUI macOS GUI，通过 `Process` 调用 `c/` 中构建出的 CLI。
- `python/`: 独立的 Python 实现。
- `tests/`: 回归测试脚本。测试默认动态生成 synthetic `.fol`，不提交游戏资源样例。
- `docs/`: 架构说明与测试说明。

## 构建

### Windows GUI

使用 Visual Studio 2022 或 Developer PowerShell:

```powershell
msbuild gui/FolToolG.sln /p:Configuration=Release /p:Platform=x64
```

### Windows Terminal CLI

```powershell
msbuild c/FolToolCli.vcxproj /p:Configuration=Release /p:Platform=x64
```

### macOS 共享 CLI

```bash
cd c
make mac
```

生成物为 `c/fol_tool_mac`。`mac/` 图形界面会优先复用它，不存在时自动执行一次本地构建。

### Python

```bash
cd python
python fol_tool.py -h
```

## Round-Trip 测试

推荐使用共享 CLI 做本地回归：

1. 生成或选择一个你有权使用的 `.fol`
2. 修改 `assets/` 下任意文件
3. 重新打包
4. 再次解包到新目录
5. 校验修改后的文件字节内容一致

Windows PowerShell 可直接运行：

```powershell
pwsh -File tests/run-roundtrip.ps1 -CliPath c\x64\Release\FolToolCli.exe
```

脚本会先用 synthetic 工作区生成一个最小 `.fol`，再执行完整 round-trip。也可以传入你有权使用的外部样例：

```powershell
pwsh -File tests/run-roundtrip.ps1 -CliPath c\x64\Release\FolToolCli.exe -InputFol path\to\sample.fol
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
