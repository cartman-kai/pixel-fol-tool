# Contributing

感谢你为 Pixel Fol Tool 提交改进。

## 开发约定

- 归档协议、解包和打包规则放在 `core/`，前端只处理参数、交互和展示。
- 不要提交游戏资源、未经授权的 `.fol` 样例或 `tmp/` 下的测试产物。
- 保持现有 C/C++、Python 和 Swift 命名与缩进风格，避免无关的大范围格式化。
- 用户可见变化应同步更新 `CHANGELOG.md` 的 `Unreleased` 章节。

## 提交前验证

Windows Release 构建：

```powershell
msbuild pixel-fol-tool.sln /p:Configuration=Release /p:Platform=x64
msbuild pixel-fol-tool.sln /p:Configuration=Release /p:Platform=x86
pwsh -File tests/run-roundtrip.ps1 -CliPath bin\x64\Release\FolToolCli.exe
```

macOS 变更还应执行：

```bash
make -C c mac
swift build --package-path mac -c release
```

## Pull Request

PR 请说明影响的平台、用户可见变化和验证步骤。一个 PR 尽量只解决一个主题；发布流程变更需要同时说明对 GitHub Actions 的影响。
