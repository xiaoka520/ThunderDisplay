// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "ThunderDisplay",
    platforms: [.macOS(.v13)],
    products: [.executable(name: "ThunderDisplayHost", targets: ["ThunderDisplayHost"]), .executable(name: "ThunderDisplayBoot", targets: ["ThunderDisplayBoot"])],
    targets: [
        .target(name: "Wire"),
        .target(name: "HostState", linkerSettings: [.linkedFramework("Security"), .linkedFramework("SystemConfiguration")]),
        .target(name: "CursorSupport", linkerSettings: [.linkedFramework("AppKit")]),
        .executableTarget(name: "ThunderDisplayBoot", dependencies: ["Wire", "HostState"], linkerSettings: [.linkedFramework("SystemConfiguration")]),
        .executableTarget(name: "ThunderDisplayHost", dependencies: ["Wire", "HostState", "CursorSupport"],
                          linkerSettings: [.linkedFramework("AppKit"), .linkedFramework("ScreenCaptureKit"),
                                           .linkedFramework("VideoToolbox"), .linkedFramework("SystemConfiguration"), .linkedFramework("ServiceManagement"), .linkedFramework("IOKit")]),
        .testTarget(name: "WireTests", dependencies: ["Wire"]),
        .testTarget(name: "HostStateTests", dependencies: ["HostState"]),
        .testTarget(name: "CursorSupportTests", dependencies: ["CursorSupport"])
    ]
)
