# 架构说明

`core/` 是 `.fol` 解包与打包行为的唯一实现入口。它通过 `core/include/fol_core.h` 暴露稳定的 C ABI，内部使用 C++ 处理文件系统、字符串转换、归档读写和清单逻辑。

各前端职责如下：

- `c/`：共享核心的命令行入口，供 Windows Terminal 和 macOS 本地构建使用。
- `gui/`：Win32 图形界面，将核心日志和进度回调映射到 UI。
- `mac/`：SwiftUI 图形界面，通过子进程调用 CLI。
- `python/`：独立实现，保留给脚本用途与跨语言参考。

共享核心负责以下协议细节：

- 读取与写入 archive header、index、data block、key table、padding；
- 维护 `manifest.txt` 格式 `Index|Key|GamePath`；
- 保持原有文件顺序，并对新增文件执行确定性排序；
- 统一本地路径与归档内部路径的转换规则；
- 统一错误码、日志级别和进度回调格式。
