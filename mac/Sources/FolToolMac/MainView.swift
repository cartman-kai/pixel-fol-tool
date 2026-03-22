import AppKit
import SwiftUI
import UniformTypeIdentifiers
import Foundation

enum ToolMode: String, CaseIterable, Identifiable {
    case unpack = "解包"
    case pack = "打包"

    var id: String { rawValue }
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
            runUnpackPlaceholder()
        case .pack:
            runPackPlaceholder()
        }
    }

    private func runUnpackPlaceholder() {
        guard !unpackInputPath.isEmpty else {
            appendLog("错误：请先选择要解包的 .fol 文件。")
            return
        }
        guard !unpackOutputPath.isEmpty else {
            appendLog("错误：请先选择输出目录。")
            return
        }
        runTool(
            title: "开始解包",
            arguments: ["unpack", unpackInputPath, unpackOutputPath],
            details: [
                "输入文件: \(unpackInputPath)",
                "输出目录: \(unpackOutputPath)",
            ]
        )
    }

    private func runPackPlaceholder() {
        guard !packInputPath.isEmpty else {
            appendLog("错误：请先选择工作区目录。")
            return
        }
        guard !packOutputPath.isEmpty else {
            appendLog("错误：请先选择输出 .fol 文件。")
            return
        }
        runTool(
            title: "开始打包",
            arguments: ["pack", packInputPath, packOutputPath],
            details: [
                "工作区目录: \(packInputPath)",
                "输出文件: \(packOutputPath)",
            ]
        )
    }

    private func runTool(title: String, arguments: [String], details: [String]) {
        guard !isRunning else {
            return
        }

        isRunning = true
        progress = 0
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
                            self?.advanceProgress(toAtLeast: 0.2)
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
                        self?.incrementProgressDuringRun()
                    }
                }

                await MainActor.run {
                    self.flushPendingLogBuffer()
                    self.progress = 1.0
                    self.appendLog("任务完成。")
                    self.isRunning = false
                }
            } catch {
                await MainActor.run {
                    self.flushPendingLogBuffer()
                    self.appendLog("任务失败：\(error.localizedDescription)")
                    self.isRunning = false
                }
            }
        }
    }

    private func appendLog(_ message: String) {
        logs.append(message)
    }

    private func consumeLogChunk(_ chunk: String) {
        pendingLogBuffer.append(chunk)

        let normalized = pendingLogBuffer.replacingOccurrences(of: "\r\n", with: "\n")
        let parts = normalized.split(separator: "\n", omittingEmptySubsequences: false)

        if normalized.hasSuffix("\n") {
            pendingLogBuffer = ""
            for part in parts where !part.isEmpty {
                appendLog(String(part))
            }
        } else if let last = parts.last {
            pendingLogBuffer = String(last)
            for part in parts.dropLast() where !part.isEmpty {
                appendLog(String(part))
            }
        }
    }

    private func flushPendingLogBuffer() {
        guard !pendingLogBuffer.isEmpty else {
            return
        }
        appendLog(pendingLogBuffer)
        pendingLogBuffer = ""
    }

    private func advanceProgress(toAtLeast value: Double) {
        progress = max(progress, value)
    }

    private func incrementProgressDuringRun() {
        progress = min(max(progress + 0.03, 0.3), 0.95)
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

            ScrollView {
                LazyVStack(alignment: .leading, spacing: 6) {
                    ForEach(Array(viewModel.logs.enumerated()), id: \.offset) { _, line in
                        Text(line)
                            .font(.system(size: 12, weight: .regular, design: .monospaced))
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                }
                .padding(12)
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
