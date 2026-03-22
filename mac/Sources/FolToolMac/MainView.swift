import AppKit
import SwiftUI
import UniformTypeIdentifiers

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
        runPlaceholderTask(
            title: "开始解包",
            details: [
                "输入文件: \(unpackInputPath)",
                "输出目录: \(unpackOutputPath)",
                "占位流程：后续会在这里接入 C 核心的 fol_unpack。",
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
        runPlaceholderTask(
            title: "开始打包",
            details: [
                "工作区目录: \(packInputPath)",
                "输出文件: \(packOutputPath)",
                "占位流程：后续会在这里接入 C 核心的 fol_pack。",
            ]
        )
    }

    private func runPlaceholderTask(title: String, details: [String]) {
        guard !isRunning else {
            return
        }

        isRunning = true
        progress = 0
        appendLog("==========")
        appendLog(title)
        details.forEach(appendLog)

        Task {
            for step in 1...5 {
                try? await Task.sleep(for: .milliseconds(180))
                progress = Double(step) / 5.0
                appendLog("占位任务进度：\(step * 20)%")
            }
            appendLog("占位任务完成。下一步接入真实核心逻辑。")
            isRunning = false
        }
    }

    private func appendLog(_ message: String) {
        logs.append(message)
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
            Text("macOS 图形界面脚手架。当前已打通窗口、表单和日志区域，核心打包/解包逻辑待接入。")
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
