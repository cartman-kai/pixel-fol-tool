# Pixel Fol Tool (PFT)

**免责声明本工具仅供学习研究和技术交流使用。请勿用于任何商业用途或侵犯版权的行为。**

一个用于提取和打包 Pixel Software（像素软件）游戏引擎 `.fol` 资源文件的命令行工具。该工具基于逆向工程分析编写，实现了完整的加密与解密流程。

## 功能特性

* **跨平台**: 兼容 Windows, macOS, Linux。
* **解包 (Unpack)**: 自动识别密钥，完整还原文件名、目录结构和文件内容。
* **打包 (Pack)**: 将修改后的资源目录重新打包为游戏可识别的 `.fol` 文件，并自动生成新的加密密钥。
* **算法还原**: 完美复刻了原版的索引加密 (Cubic Polynomial) 和内容加密 (Quadratic Polynomial) 算法。

## 使用方法

确保你安装了 Python 3.6 或更高版本。

### 1. 解包文件 (Unpack)

将 `.fol` 文件提取到文件夹中。

```bash
# 基础用法 (默认输出到 slr_fol 文件夹)
python3 fol_tool.py unpack slr.fol

# 指定输出目录
python3 fol_tool.py unpack slr.fol -o ./extracted_data
```

### 2. 打包文件 (Pack)


将修改后的文件夹打包回 .fol 文件。

```bash
# 基础用法 (默认输出为 output.fol)

python fol_tool.py pack ./extracted_data

# 指定输出文件名
python fol_tool.py pack ./extracted_data -o New_data.fol
```

### 3. 查看帮助

```
python fol_tool.py -h
```

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
