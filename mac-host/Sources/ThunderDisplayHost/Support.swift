import AppKit
import Darwin
import SystemConfiguration
import OSLog
import Wire

struct HostError: LocalizedError, CustomStringConvertible {
    let description: String
    var errorDescription: String? { description }
    init(_ message: String) { description = message }
}
private let hostLogger = Logger(subsystem: "dev.thunderdisplay.host", category: "desktop")
func log(_ message: String) {
    print("[ThunderDisplay] \(message)"); fflush(stdout)
    hostLogger.notice("\(message, privacy: .public)")
}

struct HostBindError: Error, CustomStringConvertible {
    let ip: String, port: UInt16, code: Int32
    var addressInUse: Bool { code == EADDRINUSE }
    var description: String { "Cannot bind \(ip):\(port): \(String(cString: strerror(code)))" }
}
func nonblocking(_ fd: Int32) throws {
    guard fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) == 0 else { throw HostError("fcntl: \(errno)") }
}
func address(_ ip: String, port: UInt16) throws -> sockaddr_in {
    var a = sockaddr_in(); a.sin_len = UInt8(MemoryLayout<sockaddr_in>.size); a.sin_family = sa_family_t(AF_INET); a.sin_port = port.bigEndian
    guard inet_pton(AF_INET, ip, &a.sin_addr) == 1 else { throw HostError("Expected a numeric IPv4 address: \(ip)") }
    return a
}
func withAddress<T>(_ a: inout sockaddr_in, _ body: (UnsafePointer<sockaddr>, socklen_t) -> T) -> T {
    withUnsafePointer(to: &a) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { body($0, socklen_t(MemoryLayout<sockaddr_in>.size)) } }
}
func ipString(_ a: in_addr) -> String {
    var a = a; var buffer = [CChar](repeating: 0, count: Int(INET_ADDRSTRLEN))
    inet_ntop(AF_INET, &a, &buffer, socklen_t(buffer.count)); return String(cString: buffer)
}
func bridgeAddress() -> String? {
    guard let store = SCDynamicStoreCreate(nil, "ThunderDisplay" as CFString, nil, nil),
          let keys = SCDynamicStoreCopyKeyList(store, "Setup:/Network/Service/.*/Interface" as CFString) as? [String] else { return nil }
    for key in keys {
        guard let info = SCDynamicStoreCopyValue(store, key as CFString) as? [String: Any],
              let device = info["DeviceName"] as? String else { continue }
        let service = key.replacingOccurrences(of: "/Interface", with: "")
        let meta = SCDynamicStoreCopyValue(store, service as CFString) as? [String: Any]
        let name = meta?["UserDefinedName"] as? String ?? ""
        let type = info["Type"] as? String ?? ""
        guard type == "Bridge" || name.localizedCaseInsensitiveContains("Thunderbolt") || name.contains("雷雳") else { continue }
        if let state = SCDynamicStoreCopyValue(store, "State:/Network/Interface/\(device)/IPv4" as CFString) as? [String: Any],
           let ips = state["Addresses"] as? [String], let ip = ips.first(where: { $0 != "127.0.0.1" && $0 != "0.0.0.0" }) { return ip }
    }
    return nil
}

struct Options {
    var bind: String?, port: UInt16 = 47990, display: UInt32?, token: String?, diagnose = false, encoderCheck = false, captureCheck = false
    var startupStatus = false, repairStartup = false, registerLogin = false, unregisterLogin = false, cursorCheck = false
    var installerConfig = false
    var loginWindow = false, loginWindowCheck = false
    var loginWindowFrameCheck = false
    var inputCheck = false, inputCheckQuartz = false, inputCheckKeyboardOnly = false
    var startupUser: UInt32?
    var recoveryCheck = false
    var previewUI = false, background = false, captureCheck10 = false, captureCheckNative = false
    var encoderCheckBitrate: UInt64 = 10_000_000
    var desktopColorCheck = false
    var captureCheckLocalCursor = false
    var requirePairing = false, pairingOverride: Bool?, permissionProbe = false
    var relaunchParent: Int32?
    init() throws {
        var args = Array(CommandLine.arguments.dropFirst())
        while !args.isEmpty {
            let arg = args.removeFirst()
            if arg == "--login-window" { loginWindow = true; background = true; continue }
            if arg == "--login-window-check" { loginWindowCheck = true; continue }
            if arg == "--login-window-frame-check" { loginWindowFrameCheck = true; continue }
            if arg == "--input-check" { inputCheck = true; continue }
            if arg == "--input-check-quartz" { inputCheck = true; inputCheckQuartz = true; continue }
            if arg == "--input-check-keyboard" { inputCheck = true; inputCheckQuartz = true; inputCheckKeyboardOnly = true; continue }
            if arg == "--recovery-check" { recoveryCheck = true; background = true; bind = "127.0.0.1"; port = 48079; requirePairing = false; pairingOverride = false; continue }
            if arg == "--diagnose" { diagnose = true; continue }
            if arg == "--startup-status" { startupStatus = true; continue }
            if arg == "--cursor-check" { cursorCheck = true; continue }
            if arg == "--repair-startup" { repairStartup = true; continue }
            if arg == "--register-login" { registerLogin = true; continue }
            if arg == "--unregister-login" { unregisterLogin = true; continue }
            if arg == "--installer-config" { installerConfig = true; continue }
            if arg == "--background" { background = true; continue }
            if arg == "--encoder-check" { encoderCheck = true; continue }
            if arg == "--encoder-check-srgb" { encoderCheck = true; desktopColorCheck = true; continue }
            if arg == "--capture-check-srgb" || arg == "--capture-check-srgb-10bit" { captureCheck = true; captureCheckNative = true; desktopColorCheck = true; captureCheck10 = arg.hasSuffix("-10bit"); continue }
            if arg == "--encoder-check-20g" { encoderCheck = true; encoderCheckBitrate = 20_000_000_000; continue }
            if arg == "--encoder-check-gigabit" { encoderCheck = true; encoderCheckBitrate = 1_000_000_000; continue }
            if arg == "--capture-check-local-cursor" { captureCheck = true; captureCheckNative = true; captureCheckLocalCursor = true; continue }
            if arg == "--capture-check-native" || arg == "--capture-check-native-10bit" { captureCheck = true; captureCheckNative = true; captureCheck10 = arg.hasSuffix("-10bit"); continue }
            if arg == "--capture-check-10bit" { captureCheck = true; captureCheck10 = true; continue }
            if arg == "--capture-check" { captureCheck = true; continue }
            if arg == "--preview-ui" { previewUI = true; continue }
            if arg == "--permissions-json" { permissionProbe = true; continue }
            if arg == "--require-pairing" { requirePairing = true; pairingOverride = true; continue }
            if arg == "--no-pairing" { requirePairing = false; pairingOverride = false; continue }
            if arg == "--help" || arg == "-h" {
                print("""
                ThunderDisplayHost [--bind IPv4] [--port 47990] [--display ID] [--require-pairing --token 32-hex-code]
                ThunderDisplayHost --diagnose
                ThunderDisplayHost --encoder-check
                ThunderDisplayHost --capture-check
                Default: bind only to the detected Thunderbolt Bridge IPv4 address.
                The client selects 60 / 120 / 165 / 240 fps, resolution and bitrate.
                --diagnose checks permissions and interfaces without requesting access or starting a server.
                """); exit(0)
            }
            // Finder may supply a process serial number on older macOS releases.
            if arg.hasPrefix("-psn_") { continue }
            guard !args.isEmpty else { throw HostError("Missing value for \(arg)") }
            let value = args.removeFirst()
            switch arg {
            case "--startup-user": guard let n = UInt32(value), n >= 500 else { throw HostError("Invalid startup user") }; startupUser = n
            case "--relaunch-after-pid": guard let n = Int32(value), n > 0 else { throw HostError("Invalid relaunch parent") }; relaunchParent = n
            case "--bind": bind = value
            case "--port": guard let n = UInt16(value), n > 0 else { throw HostError("Invalid port") }; port = n
            case "--display": guard let n = UInt32(value) else { throw HostError("Invalid display ID") }; display = n
            case "--token": token = value.lowercased(); requirePairing = true; pairingOverride = true
            default: throw HostError("Unknown option \(arg)")
            }
        }
        if recoveryCheck { bind = "127.0.0.1"; port = 48079; requirePairing = false; pairingOverride = false; token = nil; background = true }
        if let token, token.count != 32 || !token.allSatisfy({ $0.isHexDigit && $0.isASCII }) { throw HostError("Pairing code must be 32 hex characters") }
        if bind == "0.0.0.0" { throw HostError("Bind to a specific local IPv4 address") }
    }
}

/// Discovery responses are limited to the subnet of the selected streaming interface.
func sameSubnet(_ remote: in_addr, localIP: String) -> Bool {
    var list: UnsafeMutablePointer<ifaddrs>?
    guard getifaddrs(&list) == 0, let first = list else { return false }
    defer { freeifaddrs(first) }
    var cursor: UnsafeMutablePointer<ifaddrs>? = first
    while let item = cursor {
        defer { cursor = item.pointee.ifa_next }
        guard let a = item.pointee.ifa_addr, a.pointee.sa_family == AF_INET, let m = item.pointee.ifa_netmask else { continue }
        let address = UnsafeRawPointer(a).assumingMemoryBound(to: sockaddr_in.self).pointee.sin_addr
        let mask = UnsafeRawPointer(m).assumingMemoryBound(to: sockaddr_in.self).pointee.sin_addr.s_addr
        if ipString(address) == localIP { return (address.s_addr & mask) == (remote.s_addr & mask) }
    }
    return false
}

func pairingCode(_ override: String?) throws -> String {
    if let override { return override }
    let folder = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0].appendingPathComponent("ThunderDisplay")
    let path = folder.appendingPathComponent("pairing-code")
    if let text = try? String(contentsOf: path, encoding: .utf8).trimmingCharacters(in: .whitespacesAndNewlines),
       text.count == 32, text.allSatisfy({ $0.isHexDigit && $0.isASCII }) { return text.lowercased() }
    try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
    let value = (0..<16).map { _ in String(format: "%02x", UInt8.random(in: .min ... .max)) }.joined()
    try value.write(to: path, atomically: true, encoding: .utf8)
    try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: path.path)
    return value
}
