import AppKit
import SwiftUI

@main
struct FolToolMacApp: App {
    init() {
        if let iconURL = Bundle.module.url(forResource: "FolG", withExtension: "icns"),
           let icon = NSImage(contentsOf: iconURL) {
            NSApplication.shared.applicationIconImage = icon
        }
    }

    var body: some Scene {
        WindowGroup("Pixel FOL Tool") {
            MainView()
                .frame(minWidth: 900, minHeight: 620)
        }
        .windowResizability(.contentMinSize)
    }
}
