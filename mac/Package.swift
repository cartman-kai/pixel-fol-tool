// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "FolToolMac",
    platforms: [
        .macOS(.v13),
    ],
    products: [
        .executable(
            name: "FolToolMac",
            targets: ["FolToolMac"]
        ),
    ],
    targets: [
        .executableTarget(
            name: "FolToolMac",
            resources: [
                .process("Resources"),
            ]
        ),
    ]
)
