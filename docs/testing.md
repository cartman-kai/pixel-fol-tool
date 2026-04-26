# 测试说明

建议优先使用共享 CLI 做 round-trip 回归。默认测试会动态生成 synthetic `.fol`，避免在仓库中提交第三方游戏资源。运行产物放在 `tmp/`。

## 手工流程

1. 生成一个 synthetic `.fol`，或选择一个你有权使用的 `.fol` 解包到工作区目录。
2. 修改 `assets/` 下的任意文件内容。
3. 将工作区重新打包为新的 `.fol`。
4. 把新生成的 `.fol` 再次解包到第二个目录。
5. 对比修改文件在两个目录中的字节内容，确认完全一致。

## 自动化脚本

Windows PowerShell 可直接运行：

```powershell
pwsh -File tests/run-roundtrip.ps1 -CliPath c\x64\Release\FolToolCli.exe
```

脚本会自动完成以下动作：

- 生成 synthetic `.fol`，或解包 `-InputFol` 指定的外部样例；
- 选择一个已提取文件并追加测试标记；
- 重新打包；
- 再次解包；
- 逐字节校验修改结果。
