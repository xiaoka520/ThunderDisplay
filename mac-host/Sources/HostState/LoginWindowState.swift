import Foundation
import Darwin
import Security
import SystemConfiguration

public enum ConsoleSession {
    public static var uid: uid_t? {
        var uid: uid_t = 0, gid: gid_t = 0
        guard SCDynamicStoreCopyConsoleUser(nil, &uid, &gid) != nil else { return nil }; return uid
    }
    public static var preLogin: Bool {
        var uid: uid_t = 0, gid: gid_t = 0
        guard let name = SCDynamicStoreCopyConsoleUser(nil, &uid, &gid) as String? else { return false }
        return name == "loginwindow" && uid < 500
    }
    public static func ownsDesktop(_ processUID: uid_t) -> Bool { processUID >= 500 && uid == processUID }
    // launchctl describes the inherited bootstrap manager. Security's graphics
    // flag describes a different context and can remain false after bsexec.
    public static var managerName: String {
        let process = Process(), pipe = Pipe()
        process.executableURL = URL(fileURLWithPath: "/bin/launchctl")
        process.arguments = ["managername"]; process.standardOutput = pipe; process.standardError = FileHandle.nullDevice
        do {
            try process.run(); let data = pipe.fileHandleForReading.readDataToEndOfFile(); process.waitUntilExit()
            guard process.terminationStatus == 0, data.count < 256 else { return "unknown" }
            return String(decoding: data, as: UTF8.self).trimmingCharacters(in: .whitespacesAndNewlines)
        } catch { return "unknown" }
    }
    public static var loggedIn: Bool {
        var uid: uid_t = 0, gid: gid_t = 0
        guard let name = SCDynamicStoreCopyConsoleUser(nil, &uid, &gid) as String? else { return false }
        return uid >= 500 && name != "loginwindow" && name != "_mbsetupuser"
    }
    public static var graphicsAvailable: Bool {
        var session: SecuritySessionId = 0
        var flags = SessionAttributeBits(rawValue: 0)
        return SessionGetInfo(callerSecuritySession, &session, &flags) == errSecSuccess && flags.contains(.sessionHasGraphicAccess)
    }
}

/// A root-owned heartbeat, not a privileged command endpoint. Never stores input,
/// credentials, images, pairing secrets, or a user-selected executable path.
public struct LoginWindowState: Codable, Equatable {
    public enum Phase: String, Codable { case checking, claiming, listening, blocked, stopped }
    public static let directory = "/Library/Application Support/ThunderDisplay"
    public static let path = directory + "/loginwindow-state.json"
    public let version: Int
    public let pid: Int32
    public let uptime: TimeInterval
    public let phase: Phase
    public let detail: String
    public let port: UInt16
    public let captureChecked: Bool
    public let inputChecked: Bool
    public let lastFailure: String?
    public init(pid: Int32, uptime: TimeInterval, phase: Phase, detail: String = "", port: UInt16 = 47990, captureChecked: Bool = false, inputChecked: Bool = false, lastFailure: String? = nil) {
        version = 1; self.pid = pid; self.uptime = uptime; self.phase = phase
        self.port = port
        self.captureChecked = captureChecked; self.inputChecked = inputChecked
        self.lastFailure = lastFailure.map { String($0.prefix(256)) }
        self.detail = String(detail.prefix(256))
    }
    public func fresh(at now: TimeInterval, maximumAge: TimeInterval = 12) -> Bool {
        version == 1 && pid > 0 && port > 0 && uptime.isFinite && now.isFinite && maximumAge.isFinite && maximumAge > 0 && uptime >= 0 && now >= uptime && now - uptime < maximumAge && detail.utf8.count <= 1024 && (lastFailure?.utf8.count ?? 0) <= 1024
    }
    public func ownsPort(at now: TimeInterval) -> Bool {
        fresh(at: now) && (phase == .claiming || phase == .listening)
    }
    public static func read(maximumAge: TimeInterval = 12) -> Self? {
        // Reject symlinks and unprivileged writers, including a replaced directory.
        var directoryStat = stat()
        guard lstat(directory, &directoryStat) == 0, directoryStat.st_uid == 0,
              directoryStat.st_mode & S_IFMT == S_IFDIR, directoryStat.st_mode & 0o022 == 0 else { return nil }
        let fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)
        guard fd >= 0 else { return nil }; defer { close(fd) }
        var info = stat()
        guard fstat(fd, &info) == 0, info.st_uid == 0, info.st_mode & S_IFMT == S_IFREG,
              info.st_mode & 0o022 == 0, info.st_size > 0, info.st_size <= 4096 else { return nil }
        var bytes = [UInt8](repeating: 0, count: Int(info.st_size))
        guard Darwin.read(fd, &bytes, bytes.count) == bytes.count,
              let state = try? JSONDecoder().decode(Self.self, from: Data(bytes)),
              state.fresh(at: ProcessInfo.processInfo.systemUptime, maximumAge: maximumAge) else { return nil }
        return state
    }
    public func write() throws {
        var info = stat()
        guard geteuid() == 0, lstat(Self.directory, &info) == 0, info.st_uid == 0,
              info.st_mode & S_IFMT == S_IFDIR, info.st_mode & 0o022 == 0 else {
            throw NSError(domain: "ThunderDisplay.LoginWindow", code: 1, userInfo: [NSLocalizedDescriptionKey: "LoginWindow state directory must be owned by root"])
        }
        try JSONEncoder().encode(self).write(to: URL(fileURLWithPath: Self.path), options: .atomic)
        guard chmod(Self.path, 0o644) == 0 else { throw POSIXError(.EACCES) }
    }
}

public struct LoginWindowConfiguration: Codable, Equatable {
    public static let path = LoginWindowState.directory + "/loginwindow-config.plist"
    public let port: UInt16
    public let requirePairing: Bool
    public let token: String?
    public init(port: UInt16, requirePairing: Bool, token: String?) {
        self.port = port; self.requirePairing = requirePairing; self.token = requirePairing ? token : nil
    }
    public var valid: Bool {
        port > 0 && (!requirePairing || (token?.utf8.count == 32 && token!.allSatisfy { $0.isASCII && $0.isHexDigit }))
    }
    public static func read() throws -> Self {
        let fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)
        guard fd >= 0 else { throw POSIXError(.ENOENT) }; defer { close(fd) }
        var info = stat()
        guard fstat(fd, &info) == 0, info.st_uid == 0, info.st_mode & S_IFMT == S_IFREG,
              info.st_mode & 0o077 == 0, info.st_size > 0, info.st_size <= 4096 else { throw POSIXError(.EACCES) }
        var bytes = [UInt8](repeating: 0, count: Int(info.st_size))
        guard Darwin.read(fd, &bytes, bytes.count) == bytes.count else { throw POSIXError(.EIO) }
        let result = try PropertyListDecoder().decode(Self.self, from: Data(bytes))
        guard result.valid else { throw POSIXError(.EINVAL) }; return result
    }
}
