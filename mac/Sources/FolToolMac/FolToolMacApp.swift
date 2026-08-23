import SwiftUI

@main
struct FolToolMacApp: App {
    var body: some Scene {
        WindowGroup("Pixel FOL Tool") {
            MainView()
                .frame(minWidth: 900, minHeight: 620)
        }
        .windowResizability(.contentMinSize)
    }
}
