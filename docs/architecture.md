# 架构说明

`core/` 是 `.fol` 解包与打包行为的唯一实现入口。它通过 `core/include/fol_core.h` 暴露稳定的 C ABI，内部使用 C++ 处理文件系统、字符串转换、归档读写和清单逻辑。

各前端职责如下：

- `c/`：共享核心的命令行入口，供 Windows Terminal 和 macOS 本地构建使用。
- `gui/`：Win32 图形界面，将核心日志和进度回调映射到 UI。
- `mac/`：SwiftUI 图形界面，通过子进程调用 CLI。
- `python/`：独立实现，保留给脚本用途与跨语言参考。

共享核心负责以下协议细节：

- 读取与写入 archive header、index、data block、key table、padding；
- 解包直接输出到所选目录（无 `assets/` 子目录），不生成 manifest 清单；
- 打包直接扫描所选目录，每个文件随机生成密钥，按路径排序写入（游戏读取不依赖文件顺序）；
- 统一本地路径与归档内部路径的转换规则；
- 统一错误码、日志级别和进度回调格式。
