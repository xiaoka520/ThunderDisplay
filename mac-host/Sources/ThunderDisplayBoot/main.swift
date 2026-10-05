import Foundation
import Darwin
import SystemConfiguration
import OSLog
import Wire
import HostState

// The boot helper has no capture, input injection or privilege-changing API.
// It advertises only on a bridge while no console user is logged in. The Aqua
// Host takes over after login. FileVault preboot runs before any launchd service.
var listener: Int32 = -1, discovery: Int32 = -1
var selectedIP = ""
let port = UInt16(ProcessInfo.processInfo.environment["TD_BOOT_PORT"] ?? "47990") ?? 47990
func closeSockets() {
    if listener >= 0 { close(listener); listener = -1 }
    if discovery >= 0 { close(discovery); discovery = -1 }
    selectedIP = ""
}
func consoleLoggedIn() -> Bool {
    var uid: uid_t = 0, gid: gid_t = 0
    guard let user = SCDynamicStoreCopyConsoleUser(nil, &uid, &gid) as String? else { return false }
    return uid >= 500 && user != "loginwindow" && user != "_mbsetupuser"
}
func bridge() -> (String, UInt32)? {
    var first: UnsafeMutablePointer<ifaddrs>?
    guard getifaddrs(&first) == 0 else { return nil }; defer { freeifaddrs(first) }
    var next = first
    while let p = next {
        defer { next = p.pointee.ifa_next }
        guard String(cString: p.pointee.ifa_name).hasPrefix("bridge"),
            let address = p.pointee.ifa_addr, address.pointee.sa_family == UInt8(AF_INET), let netmask = p.pointee.ifa_netmask else { continue }
        let ip = UnsafeRawPointer(address).assumingMemoryBound(to: sockaddr_in.self).pointee.sin_addr
        guard ip.s_addr != 0, (p.pointee.ifa_flags & UInt32(IFF_UP)) != 0 else { continue }
        let mask = UnsafeRawPointer(netmask).assumingMemoryBound(to: sockaddr_in.self).pointee.sin_addr.s_addr
        var value = ip, text = [CChar](repeating: 0, count: Int(INET_ADDRSTRLEN))
        guard inet_ntop(AF_INET, &value, &text, socklen_t(text.count)) != nil else { continue }
        return (String(cString: text), mask)
    }
    return nil
}
func makeSocket(_ kind: Int32, ip: String) -> Int32 {
    let fd = socket(AF_INET, kind, 0); guard fd >= 0 else { return -1 }
    var yes: Int32 = 1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, 4); setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, 4)
    _ = fcntl(fd, F_SETFL, O_NONBLOCK)
    var address = sockaddr_in(); address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size); address.sin_family = sa_family_t(AF_INET); address.sin_port = port.bigEndian
    inet_pton(AF_INET, ip, &address.sin_addr)
    let result = withUnsafePointer(to: &address) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { Darwin.bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) } }
    guard result == 0 else { close(fd); return -1 }
    return fd
}
if CommandLine.arguments.contains("--check") {
    print("Boot helper: no capture or input; console logged in: \(consoleLoggedIn()); bridge: \(bridge()?.0 ?? "unavailable"); port \(port)")
    exit(0)
}
let logger = Logger(subsystem: "dev.thunderdisplay.host", category: "boot")
logger.notice("System boot helper started")
var lastConsoleState: Bool?
while true {
    let loggedIn = consoleLoggedIn()
    if lastConsoleState != loggedIn {
        logger.notice("Console logged in: \(loggedIn, privacy: .public); pre-login helper does not capture the desktop")
        lastConsoleState = loggedIn
    }
    guard !loggedIn, let (ip, mask) = bridge() else { closeSockets(); Thread.sleep(forTimeInterval: 1); continue }
    let agentState = LoginWindowState.read()
    if agentState?.ownsPort(at: ProcessInfo.processInfo.systemUptime) == true {
        closeSockets(); Thread.sleep(forTimeInterval: 0.05); continue
    }
    if selectedIP != ip || listener < 0 {
        closeSockets(); listener = makeSocket(SOCK_STREAM, ip: ip)
        if listener >= 0 && listen(listener, 4) == 0 {
            discovery = makeSocket(SOCK_DGRAM, ip: "0.0.0.0"); selectedIP = ip
            logger.notice("Pre-login bridge fallback ready on port \(port, privacy: .public)")
        }
        else { closeSockets(); Thread.sleep(forTimeInterval: 1); continue }
    }
    var local = in_addr(); inet_pton(AF_INET, ip, &local)
    var sender = sockaddr_in(), length = socklen_t(MemoryLayout<sockaddr_in>.size)
    let fd = withUnsafeMutablePointer(to: &sender) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { accept(listener, $0, &length) } }
    if fd >= 0 {
        if (sender.sin_addr.s_addr & mask) == (local.s_addr & mask) {
            var yes: Int32 = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, 4)
            let reason: String
            if let agentState, agentState.phase == .blocked {
                reason = "PreLoginCaptureUnavailable: " + agentState.detail
            } else if agentState?.phase == .checking {
                reason = "PreLoginStarting: Checking LoginWindow screen capture and input access."
            } else {
                reason = "HostWaitingForLogin: LoginWindow agent unavailable. Log in or check pre-login component installation."
            }
            let message = ProtocolWire.frame(Data([Message.failure.rawValue]) + Data(reason.utf8))
            message.withUnsafeBytes { _ = send(fd, $0.baseAddress, $0.count, 0) }
        }
        close(fd)
    }
    if discovery >= 0 {
        var bytes = [UInt8](repeating: 0, count: 64)
        let n = withUnsafeMutablePointer(to: &sender) { p in p.withMemoryRebound(to: sockaddr.self, capacity: 1) { recvfrom(discovery, &bytes, bytes.count, 0, $0, &length) } }
        if n == 8, Data(bytes.prefix(n)) == Data("TDDISC1?".utf8), (sender.sin_addr.s_addr & mask) == (local.s_addr & mask) {
            let reply = Data("TDHOST1 \(port)".utf8)
            reply.withUnsafeBytes { data in withUnsafePointer(to: &sender) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { _ = sendto(discovery, data.baseAddress, data.count, 0, $0, length) } } }
        }
    }
    Thread.sleep(forTimeInterval: 0.05)
}
