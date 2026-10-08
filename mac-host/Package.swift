// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "ThunderDisplay",
    platforms: [.macOS(.v13)],
    products: [.executable(name: "ThunderDisplayHost", targets: ["ThunderDisplayHost"]), .executable(name: "ThunderDisplayBoot", targets: ["ThunderDisplayBoot"])],
    targets: [
        // Pixel loops favor throughput over SwiftPM's default -Os C release
        // setting. Keep other targets and debug diagnostics on their defaults.
        .target(name: "RawPixelSupport", cSettings: [.unsafeFlags(["-O3"], .when(configuration: .release))]),
        .target(name: "Wire", dependencies: ["RawPixelSupport"]),
        .target(name: "HostState", linkerSettings: [.linkedFramework("Security"), .linkedFramework("SystemConfiguration")]),
        .target(name: "CursorSupport", linkerSettings: [.linkedFramework("AppKit")]),
        .target(name: "HIDBridge", linkerSettings: [.linkedFramework("IOKit"), .linkedFramework("Carbon"), .linkedFramework("CoreGraphics")]),
        .target(name: "InputSupport", dependencies: ["Wire", "HIDBridge"], linkerSettings: [.linkedFramework("AppKit")]),
        .executableTarget(name: "ThunderDisplayBoot", dependencies: ["Wire", "HostState"], linkerSettings: [.linkedFramework("SystemConfiguration")]),
        .executableTarget(name: "ThunderDisplayHost", dependencies: ["Wire", "HostState", "CursorSupport", "InputSupport"],
                          linkerSettings: [.linkedFramework("AppKit"), .linkedFramework("ScreenCaptureKit"),
                                           .linkedFramework("VideoToolbox"), .linkedFramework("SystemConfiguration"), .linkedFramework("ServiceManagement"), .linkedFramework("IOKit")]),
        .testTarget(name: "WireTests", dependencies: ["Wire"]),
        .testTarget(name: "HostStateTests", dependencies: ["HostState"]),
        .testTarget(name: "CursorSupportTests", dependencies: ["CursorSupport"]),
        .testTarget(name: "InputSupportTests", dependencies: ["InputSupport", "HIDBridge"])
    ]
)
