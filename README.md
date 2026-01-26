# Pixel Fol Tool

**免责声明：本工具仅供学习研究和技术交流使用。请勿用于任何商业用途或侵犯版权的行为。**

一个用于提取和打包 Pixel Software（像素软件）游戏引擎 `.fol` 资源文件的工具集。该工具基于逆向工程分析编写，实现了完整的加密与解密流程。

本项目提供了三个版本的实现，以满足不同用户的需求：

## 1. GUI 版本 (`/gui`)
提供了易于使用的 Windows 图形化界面，支持多语言、进度展示和日志查看。
- **构建环境**: Visual Studio 2022 (C++20)。
- **快速开始**: 打开 `gui/FolToolG.sln`，选择 `Release/x64` 进行编译。

## 2. C 语言版本 (`/c`)
纯 C 编写的最基础算法实现，追求极致的执行效率和二进制稳定性。
- **构建说明**:
  ```bash
  cd c
  make
  ```
- **运行**: `./fol_tool` (查看 CLI 帮助)。

## 3. Python 版本 (`/python`)
面向脚本用户和跨平台快速部署，代码清晰，易于二次开发。
- **依赖**: Python 3.6+。
- **说明**: 详见 [python/README.md](./python/README.md)。

---

## FOL 文件格式分析

.fol 格式是像素软件用于存储游戏资源的归档格式，具有自定义的头部结构和多项式加密保护。

文件结构总览
文件采用小端序 (Little-Endian) 存储。

| 区域          | 大小                | 描述                            |
|-------------|-------------------|-------------------------------|
| Header      | 4 Bytes           | 包含文件数量 (Count) 和加密标志位。        |
| Index Table | Count * 136 Bytes | 加密的索引表，包含文件名、偏移量和大小。          |
| Data Block  | Variable          | 实际的文件数据块，每个文件数据也是独立加密的。       |
| Key Table   | Count * 4 Bytes   | 每个文件对应的加密密钥 (32-bit integer)。 |
| Padding     | 388 Bytes         | 尾部填充 (97 个 32位整数，全0)。         |
|             |                   |                               |

### Header (文件头)

* 类型: int32
* 逻辑:
  * 最高位 (Bit 31) 为 1 表示文件已加密。
  * 剩余 31 位表示包含的文件总数 (FileCount)。
  * RealCount = Header & 0x7FFFFFFF

### Encryption Algorithms (加密算法)

该格式使用基于多项式的简单流式加密。加密强度主要依赖于每个文件独立的随机 Key。

索引表加密 (Index Entry Encryption)

每个索引条目为 136 字节。被视为 34 个 uint32 进行加密。

* 加密公式: $v = raw + key + 9 \cdot i^3$
* 解密公式: $v = raw - key - 9 \cdot i^3$
  * 其中 i 为当前整数在条目中的索引 (0-33)


### 内容加密 (Content Encryption)

文件内容被视为 uint32 数组进行处理。

* 加密公式: $v = raw + key + 99 \cdot i^2$
* 解密公式: $v = raw - key - 99 \cdot i^2$
  * 其中 i 为当前整数在文件流中的索引


### 尾部结构 (Footer)

这是一个非常特殊的读取逻辑。游戏引擎通过 fseek 从文件末尾向前跳转来读取密钥表。

偏移量: SEEK_END - 4 * (97 + FileCount)

这解释了为什么写入时需要在密钥表后追加 97 个整数的填充。
