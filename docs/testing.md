# 测试说明

建议优先使用共享 CLI 做 round-trip 回归。默认测试会动态生成 synthetic `.fol`，避免在仓库中提交第三方游戏资源。运行产物放在 `tmp/`。

## 手工流程

1. 生成一个 synthetic `.fol`，或选择一个你有权使用的 `.fol` 解包到目标目录（文件直接解到该目录下，无 `assets/` 等额外子目录）。
2. 修改解包出的任意文件内容。
3. 选中同一目录直接重新打包为新的 `.fol`。
4. 把新生成的 `.fol` 再次解包到第二个目录。
5. 对比修改文件在两个目录中的字节内容，确认完全一致。

文件在归档内按路径排序写入；游戏读取不依赖文件顺序，因此无需保留解包时的原始顺序。

## 自动化脚本

Windows PowerShell 可直接运行：

```powershell
pwsh -File tests/run-roundtrip.ps1 -CliPath bin\x64\Release\FolToolCli.exe
```

脚本会自动完成以下动作：

- 生成 synthetic `.fol`，或解包 `-InputFol` 指定的外部样例；
- 选择一个已提取文件并追加测试标记；
- 重新打包；
- 再次解包；
- 逐字节校验修改结果。

GitHub Actions 会分别对 Windows x64 与 x86 Release CLI 执行同一 synthetic round-trip，同时验证 macOS CLI、SwiftUI 前端和 Python CLI 能够构建或启动。
