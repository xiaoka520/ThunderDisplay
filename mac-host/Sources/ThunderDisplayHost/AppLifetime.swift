import AppKit
import Darwin

enum HostLaunchError: Error { case alreadyRunning }

// GUI instances share one lease; diagnostics and permission probes remain independent.
final class HostInstanceLease {
    private let fd: Int32
    init() throws {
        let directory = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
            .appendingPathComponent("ThunderDisplay", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let descriptor = open(directory.appendingPathComponent("host-instance.lock").path, O_CREAT | O_RDWR | O_CLOEXEC, 0o600)
        guard descriptor >= 0 else { throw HostError("Cannot open the host instance lock") }
        guard flock(descriptor, LOCK_EX | LOCK_NB) == 0 else {
            let failure = errno; close(descriptor)
            if failure == EWOULDBLOCK { throw HostLaunchError.alreadyRunning }
            throw HostError("Cannot lock the host instance (\(failure))")
        }
        fd = descriptor
    }
    deinit { close(fd) }
}

func relaunchAfterExit(_ parent: Int32) throws {
    let deadline = Date().addingTimeInterval(10)
    while kill(parent, 0) == 0 {
        guard Date() < deadline else { throw HostError("Previous host did not exit; relaunch cancelled") }
        Thread.sleep(forTimeInterval: 0.05)
    }
    let process = Process(); process.executableURL = URL(fileURLWithPath: "/usr/bin/open")
    // Reuse an instance if LaunchServices / UI inspection has already opened it.
    process.arguments = [Bundle.main.bundlePath, "--args"] + Array(CommandLine.arguments.dropFirst(3))
    try process.run(); process.waitUntilExit()
    guard process.terminationStatus == 0 else { throw HostError("Unable to reopen the host application") }
}
