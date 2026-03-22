import AppKit
import SwiftUI
import UniformTypeIdentifiers
import Foundation

enum ToolMode: String, CaseIterable, Identifiable {
    case unpack = "解包"
    case pack = "打包"

    var id: String { rawValue }
}

struct AlertState: Identifiable {
    let id = UUID()
    let title: String
    let message: String
}

@MainActor
final class MainViewModel: ObservableObject {
    @Published var selectedMode: ToolMode = .unpack
    @Published var unpackInputPath = ""
    @Published var unpackOutputPath = ""
    @Published var packInputPath = ""
    @Published var packOutputPath = ""
    @Published var logs: [String] = ["准备就绪。"]
    @Published var progress = 0.0
    @Published var isRunning = false
    @Published var activeAlert: AlertState?
    @Published var resultURL: URL?
    @Published private(set) var runningMode: ToolMode?

    private var pendingLogBuffer = ""

    func chooseFolFile() {
        guard let url = Self.openFilePanel(
            title: "选择 .fol 文件",
            allowedContentTypes: [UTType(filenameExtension: "fol")].compactMap { $0 }
        ) else {
            return
        }
        unpackInputPath = url.path
    }

    func chooseWorkspaceFolder(for mode: ToolMode) {
        guard let url = Self.openFolderPanel(title: mode == .unpack ? "选择输出目录" : "选择工作区目录") else {
            return
        }

        if mode == .unpack {
            unpackOutputPath = url.path
        } else {
            packInputPath = url.path
        }
    }

    func chooseOutputFol() {
        guard let url = Self.saveFilePanel(title: "选择输出 .fol 文件", defaultName: "output.fol") else {
            return
        }
        packOutputPath = url.path
    }

    func runCurrentAction() {
        switch selectedMode {
        case .unpack:
            runUnpackTask()
        case .pack:
            runPackTask()
        }
    }

    private func runUnpackTask() {
        guard !unpackInputPath.isEmpty else {
            presentError(title: "无法开始解包", message: "请先选择要解包的 .fol 文件。")
            return
        }
        guard !unpackOutputPath.isEmpty else {
            presentError(title: "无法开始解包", message: "请先选择输出目录。")
            return
        }
        runTool(
            mode: .unpack,
            title: "开始解包",
            arguments: ["unpack", unpackInputPath, unpackOutputPath],
            resultURL: URL(fileURLWithPath: unpackOutputPath),
            details: [
                "输入文件: \(unpackInputPath)",
                "输出目录: \(unpackOutputPath)",
            ]
        )
    }

    private func runPackTask() {
        guard !packInputPath.isEmpty else {
            presentError(title: "无法开始打包", message: "请先选择工作区目录。")
            return
        }
        guard !packOutputPath.isEmpty else {
            presentError(title: "无法开始打包", message: "请先选择输出 .fol 文件。")
            return
        }
        runTool(
            mode: .pack,
            title: "开始打包",
            arguments: ["pack", packInputPath, packOutputPath],
            resultURL: URL(fileURLWithPath: packOutputPath),
            details: [
                "工作区目录: \(packInputPath)",
                "输出文件: \(packOutputPath)",
            ]
        )
    }

    private func runTool(mode: ToolMode, title: String, arguments: [String], resultURL: URL, details: [String]) {
        guard !isRunning else {
            return
        }

        isRunning = true
        runningMode = mode
        progress = 0
        self.resultURL = nil
        pendingLogBuffer = ""
        appendLog("==========")
        appendLog(title)
        details.forEach(appendLog)

        let rootURL = Self.repoRootURL
        let binaryURL = Self.cliBinaryURL

        Task.detached(priority: .userInitiated) { [weak self] in
            guard let self else { return }

            do {
                await MainActor.run {
                    self.progress = 0.05
                    self.appendLog("检查 CLI 后端: \(binaryURL.path)")
                }

                if !FileManager.default.fileExists(atPath: binaryURL.path) {
                    await MainActor.run {
                        self.appendLog("未找到 CLI 二进制，开始自动构建...")
                        self.progress = 0.1
                    }
                    try Self.runProcess(
                        executableURL: URL(fileURLWithPath: "/usr/bin/make"),
                        arguments: ["-C", "c", "mac"],
                        workingDirectory: rootURL
                    ) { chunk in
                        Task { @MainActor [weak self] in
                            self?.consumeLogChunk(chunk)
                        }
                    }
                }

                await MainActor.run {
                    self.appendLog("启动命令: \(binaryURL.lastPathComponent) \(arguments.joined(separator: " "))")
                    self.progress = max(self.progress, 0.25)
                }

                try Self.runProcess(
                    executableURL: binaryURL,
                    arguments: arguments,
                    workingDirectory: rootURL
                ) { chunk in
                    Task { @MainActor [weak self] in
                        self?.consumeLogChunk(chunk)
                    }
                }

                await MainActor.run {
                    self.flushPendingLogBuffer()
                    self.progress = 1.0
                    self.resultURL = resultURL
                    self.appendLog("任务完成。")
                    self.isRunning = false
                    self.runningMode = nil
                }
            } catch {
                await MainActor.run {
                    self.flushPendingLogBuffer()
                    self.presentError(title: "任务执行失败", message: error.localizedDescription)
                    self.isRunning = false
                    self.runningMode = nil
                }
            }
        }
    }

    private func appendLog(_ message: String) {
        logs.append(message)
    }

    private func presentError(title: String, message: String) {
        appendLog("错误：\(message)")
        activeAlert = AlertState(title: title, message: message)
    }

    func revealResultInFinder() {
        guard let resultURL else {
            presentError(title: "无法显示结果", message: "当前没有可显示的结果路径。")
            return
        }
        guard FileManager.default.fileExists(atPath: resultURL.path) else {
            presentError(title: "无法显示结果", message: "结果路径不存在：\(resultURL.path)")
            return
        }

        NSWorkspace.shared.activateFileViewerSelecting([resultURL])
    }

    private func consumeLogChunk(_ chunk: String) {
        pendingLogBuffer.append(chunk)

        let normalized = pendingLogBuffer.replacingOccurrences(of: "\r\n", with: "\n")
        let parts = normalized.split(separator: "\n", omittingEmptySubsequences: false)

        if normalized.hasSuffix("\n") {
            pendingLogBuffer = ""
            for part in parts where !part.isEmpty {
                handleLogLine(String(part))
            }
        } else if let last = parts.last {
            pendingLogBuffer = String(last)
            for part in parts.dropLast() where !part.isEmpty {
                handleLogLine(String(part))
            }
        }
    }

    private func flushPendingLogBuffer() {
        guard !pendingLogBuffer.isEmpty else {
            return
        }
        handleLogLine(pendingLogBuffer)
        pendingLogBuffer = ""
    }

    private func handleLogLine(_ line: String) {
        appendLog(line)
        updateProgress(for: line)
    }

    private func updateProgress(for line: String) {
        if line.contains("Compiling for macOS") {
            progress = max(progress, 0.15)
            return
        }
        if line.contains("Output: fol_tool_mac") {
            progress = max(progress, 0.22)
            return
        }

        guard let runningMode else {
            return
        }

        switch runningMode {
        case .unpack:
            if line.contains("[*] 正在解包:") {
                progress = max(progress, 0.35)
            } else if line.contains("[*] 文件数量:") {
                progress = max(progress, 0.45)
            } else if line.contains("[*] 创建工作空间:") {
                progress = max(progress, 0.6)
            } else if line.contains("[ok] 密钥清单已保存") {
                progress = max(progress, 0.85)
            } else if line.contains("[ok] 解包完成") {
                progress = max(progress, 1.0)
            } else if line.hasPrefix("[!]") {
                progress = max(progress, 0.95)
            }
        case .pack:
            if line.contains("[*] 正在分析工作空间:") {
                progress = max(progress, 0.35)
            } else if line.contains("[*] 已加载密钥清单:") {
                progress = max(progress, 0.48)
            } else if line.contains("[*] 扫描到物理文件:") {
                progress = max(progress, 0.62)
            } else if line.contains("[+] 发现新增文件:") {
                progress = min(max(progress + 0.02, 0.62), 0.72)
            } else if line.contains("[*] 正在写入数据...") {
                progress = max(progress, 0.82)
            } else if line.contains("[ok] 打包成功") {
                progress = max(progress, 1.0)
            } else if line.hasPrefix("[!]") {
                progress = max(progress, 0.95)
            }
        }
    }

    nonisolated private static var repoRootURL: URL {
        URL(fileURLWithPath: #filePath)
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .deletingLastPathComponent()
    }

    nonisolated private static var cliBinaryURL: URL {
        repoRootURL.appendingPathComponent("c/fol_tool_mac")
    }

    nonisolated private static func runProcess(
        executableURL: URL,
        arguments: [String],
        workingDirectory: URL,
        onOutput: @escaping @Sendable (String) -> Void
    ) throws {
        let process = Process()
        let pipe = Pipe()

        process.executableURL = executableURL
        process.arguments = arguments
        process.currentDirectoryURL = workingDirectory
        process.standardOutput = pipe
        process.standardError = pipe

        pipe.fileHandleForReading.readabilityHandler = { handle in
            let data = handle.availableData
            guard !data.isEmpty else {
                return
            }
            let text = String(decoding: data, as: UTF8.self)
            onOutput(text)
        }

        try process.run()
        process.waitUntilExit()
        pipe.fileHandleForReading.readabilityHandler = nil

        if process.terminationStatus != 0 {
            throw NSError(
                domain: "FolToolMac.ProcessError",
                code: Int(process.terminationStatus),
                userInfo: [
                    NSLocalizedDescriptionKey: "命令退出码 \(process.terminationStatus)"
                ]
            )
        }
    }

    private static func openFilePanel(title: String, allowedContentTypes: [UTType]) -> URL? {
        let panel = NSOpenPanel()
        panel.title = title
        panel.allowedContentTypes = allowedContentTypes
        panel.allowsMultipleSelection = false
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        return panel.runModal() == .OK ? panel.url : nil
    }

    private static func openFolderPanel(title: String) -> URL? {
        let panel = NSOpenPanel()
        panel.title = title
        panel.allowsMultipleSelection = false
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        return panel.runModal() == .OK ? panel.url : nil
    }

    private static func saveFilePanel(title: String, defaultName: String) -> URL? {
        let panel = NSSavePanel()
        panel.title = title
        panel.nameFieldStringValue = defaultName
        panel.allowedContentTypes = [UTType(filenameExtension: "fol")].compactMap { $0 }
        return panel.runModal() == .OK ? panel.url : nil
    }
}

struct MainView: View {
    @StateObject private var viewModel = MainViewModel()

    var body: some View {
        NavigationSplitView {
            List(ToolMode.allCases, selection: $viewModel.selectedMode) { mode in
                Label(mode.rawValue, systemImage: mode == .unpack ? "tray.and.arrow.down" : "archivebox")
                    .tag(mode)
            }
            .disabled(viewModel.isRunning)
            .navigationSplitViewColumnWidth(min: 180, ideal: 200)
        } detail: {
            VStack(alignment: .leading, spacing: 20) {
                header
                formSection
                progressSection
                logSection
            }
            .padding(24)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(Color(nsColor: .windowBackgroundColor))
        }
        .alert(item: $viewModel.activeAlert) { alert in
            Alert(
                title: Text(alert.title),
                message: Text(alert.message),
                dismissButton: .default(Text("确定"))
            )
        }
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Pixel FOL Tool")
                .font(.system(size: 28, weight: .semibold))
            Text("macOS 图形界面脚手架。当前已接入 C CLI 后端，可从界面触发现有解包与打包流程。")
                .font(.system(size: 13))
                .foregroundStyle(.secondary)
        }
    }

    @ViewBuilder
    private var formSection: some View {
        GroupBox {
            VStack(alignment: .leading, spacing: 16) {
                if viewModel.selectedMode == .unpack {
                    pathRow(
                        title: "FOL 文件",
                        value: viewModel.unpackInputPath,
                        actionTitle: "选择文件",
                        action: viewModel.chooseFolFile
                    )
                    pathRow(
                        title: "输出目录",
                        value: viewModel.unpackOutputPath,
                        actionTitle: "选择目录",
                        action: { viewModel.chooseWorkspaceFolder(for: .unpack) }
                    )
                } else {
                    pathRow(
                        title: "工作区目录",
                        value: viewModel.packInputPath,
                        actionTitle: "选择目录",
                        action: { viewModel.chooseWorkspaceFolder(for: .pack) }
                    )
                    pathRow(
                        title: "输出文件",
                        value: viewModel.packOutputPath,
                        actionTitle: "选择位置",
                        action: viewModel.chooseOutputFol
                    )
                }

                HStack {
                    Button(viewModel.selectedMode == .unpack ? "开始解包" : "开始打包") {
                        viewModel.runCurrentAction()
                    }
                    .keyboardShortcut(.defaultAction)
                    .disabled(viewModel.isRunning)

                    if viewModel.isRunning {
                        ProgressView()
                            .controlSize(.small)
                    }

                    Spacer()

                    if viewModel.resultURL != nil {
                        Button("在 Finder 中显示结果") {
                            viewModel.revealResultInFinder()
                        }
                        .disabled(viewModel.isRunning)
                    }
                }
            }
            .padding(8)
        } label: {
            Text(viewModel.selectedMode == .unpack ? "解包任务" : "打包任务")
                .font(.headline)
        }
    }

    private var progressSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("进度")
                    .font(.headline)
                Spacer()
                Text("\(Int(viewModel.progress * 100))%")
                    .foregroundStyle(.secondary)
            }

            ProgressView(value: viewModel.progress)
                .progressViewStyle(.linear)
        }
    }

    private var logSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("日志")
                .font(.headline)

            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 6) {
                        ForEach(Array(viewModel.logs.enumerated()), id: \.offset) { _, line in
                            Text(line)
                                .font(.system(size: 12, weight: .regular, design: .monospaced))
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }

                        Color.clear
                            .frame(height: 1)
                            .id("log-bottom")
                    }
                    .padding(12)
                }
                .onChange(of: viewModel.logs.count) { _ in
                    proxy.scrollTo("log-bottom", anchor: .bottom)
                }
            }
            .background(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .fill(Color(nsColor: .textBackgroundColor))
            )
            .overlay(
                RoundedRectangle(cornerRadius: 12, style: .continuous)
                    .stroke(Color.black.opacity(0.08), lineWidth: 1)
            )
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
    }

    private func pathRow(title: String, value: String, actionTitle: String, action: @escaping () -> Void) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title)
                .font(.system(size: 13, weight: .medium))
            HStack(spacing: 12) {
                Text(value.isEmpty ? "未选择" : value)
                    .font(.system(size: 12))
                    .foregroundStyle(value.isEmpty ? .secondary : .primary)
                    .textSelection(.enabled)
                    .padding(.horizontal, 12)
                    .padding(.vertical, 10)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(
                        RoundedRectangle(cornerRadius: 10, style: .continuous)
                            .fill(Color(nsColor: .controlBackgroundColor))
                    )

                Button(actionTitle, action: action)
                    .disabled(viewModel.isRunning)
            }
        }
    }
}
