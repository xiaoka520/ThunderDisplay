import Foundation
import Darwin
import Security
import SystemConfiguration

public enum ConsoleSession {
    public static var uid: uid_t? {
        var uid: uid_t = 0, gid: gid_t = 0
        guard SCDynamicStoreCopyConsoleUser(nil, &uid, &gid) != nil else { return nil }; return uid
    }
    /// Pure classification, kept separate so the login-window rules stay testable
    /// and so every component shares one definition of who owns the console.
    public static func isLoggedIn(consoleUser name: String?, uid: uid_t) -> Bool {
        guard let name else { return false }
        return uid >= 500 && name != "loginwindow" && name != "_mbsetupuser"
    }
    /// The login window owns the display whenever no user session is confirmed,
    /// including the case where `SCDynamicStoreCopyConsoleUser` reports no user at
    /// all. `ThunderDisplayBoot.consoleLoggedIn()` already reads it that way, but
    /// this predicate used to require a positive "loginwindow" match: on a system
    /// that reports no console user, the graphical agent waited for a session that
    /// had already arrived and never probed capture. Deriving it from
    /// `isLoggedIn` makes the two components unable to disagree.
    public static func isPreLogin(consoleUser name: String?, uid: uid_t) -> Bool {
        !isLoggedIn(consoleUser: name, uid: uid)
    }
    private static var consoleUser: (name: String?, uid: uid_t) {
        var uid: uid_t = 0, gid: gid_t = 0
        return (SCDynamicStoreCopyConsoleUser(nil, &uid, &gid) as String?, uid)
    }
    public static var preLogin: Bool {
        let current = consoleUser
        return isPreLogin(consoleUser: current.name, uid: current.uid)
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
        let current = consoleUser
        return isLoggedIn(consoleUser: current.name, uid: current.uid)
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
    /// The phase and reason in effect when the agent stopped. Login and terminate
    /// both publish `stopped` with a fixed message, which previously erased the
    /// only record of why pre-login capture never started. Additive optionals, so
    /// a v1 reader keeps decoding the same heartbeat.
    public let previousPhase: Phase?
    public let previousDetail: String?
    /// Wall clock of the last write. `uptime` is monotonic within one boot only,
    /// so it cannot order or date records across a restart.
    public let recordedAt: Date?
    public init(pid: Int32, uptime: TimeInterval, phase: Phase, detail: String = "", port: UInt16 = 47990, captureChecked: Bool = false, inputChecked: Bool = false, lastFailure: String? = nil, previousPhase: Phase? = nil, previousDetail: String? = nil, recordedAt: Date? = Date()) {
        version = 1; self.pid = pid; self.uptime = uptime; self.phase = phase
        self.port = port
        self.captureChecked = captureChecked; self.inputChecked = inputChecked
        self.lastFailure = lastFailure.map { String($0.prefix(256)) }
        self.detail = String(detail.prefix(256))
        self.previousPhase = previousPhase
        self.previousDetail = previousDetail.map { String($0.prefix(256)) }
        self.recordedAt = recordedAt
    }
    /// Structural checks shared by every reader. Freshness is deliberately not
    /// part of it: a heartbeat written during a previous boot is still the only
    /// evidence of why pre-login capture failed, and its `uptime` legitimately
    /// compares as "in the future" after a restart.
    public var isWellFormedRecord: Bool {
        version == 1 && pid > 0 && port > 0 && uptime.isFinite && uptime >= 0
            && detail.utf8.count <= 1024 && (lastFailure?.utf8.count ?? 0) <= 1024
            && (previousDetail?.utf8.count ?? 0) <= 1024
            && (recordedAt?.timeIntervalSince1970.isFinite ?? true)
    }
    public func fresh(at now: TimeInterval, maximumAge: TimeInterval = 12) -> Bool {
        isWellFormedRecord && now.isFinite && maximumAge.isFinite && maximumAge > 0 && now >= uptime && now - uptime < maximumAge
    }
    public func ownsPort(at now: TimeInterval) -> Bool {
        fresh(at: now) && (phase == .claiming || phase == .listening)
    }
    /// Reads the root-owned heartbeat through a hardened path. `nil` means the
    /// file is absent, not root-owned, not a regular file, or malformed.
    private static func loadRecord() -> Self? {
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
              state.isWellFormedRecord else { return nil }
        return state
    }
    public static func read(maximumAge: TimeInterval = 12) -> Self? {
        guard let state = loadRecord(), state.fresh(at: ProcessInfo.processInfo.systemUptime, maximumAge: maximumAge) else { return nil }
        return state
    }
    /// The last heartbeat whatever its age, including one written during a
    /// previous boot. This explains a failed login-window startup after a restart;
    /// live port ownership must keep using `read(maximumAge:)` instead.
    public static func readLastRecorded() -> Self? { loadRecord() }
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
    public let desktopUID: UInt32?
    public init(port: UInt16, requirePairing: Bool, token: String?, desktopUID: UInt32? = nil) {
        self.port = port; self.requirePairing = requirePairing; self.token = requirePairing ? token : nil
        self.desktopUID = desktopUID
    }
    public var valid: Bool {
        port > 0 && (desktopUID == nil || desktopUID! >= 500) && (!requirePairing || (token?.utf8.count == 32 && token!.allSatisfy { $0.isASCII && $0.isHexDigit }))
    }
    /// The root login component may start only this installed user's fixed job.
    /// Missing configuration or a different console user leaves startup to macOS.
    public func desktopStartupTarget(consoleUID: UInt32?) -> String? {
        guard let desktopUID, desktopUID >= 500, desktopUID < UInt32.max,
              desktopUID == consoleUID else { return nil }
        return "gui/\(desktopUID)/dev.thunderdisplay.desktop"
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
