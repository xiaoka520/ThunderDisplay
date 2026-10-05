import AppKit
import ScreenCaptureKit
import HostState

struct FreshPermissions: Codable, Sendable { let screen: Bool, access: Bool; let axTrusted: Bool? }
private final class PermissionSink: NSObject, SCStreamOutput {
    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer, of type: SCStreamOutputType) {}
}

final class PermissionMonitor {
    private var fresh: FreshPermissions?, verifiedScreen: Bool?, previousPreflight: Bool?
    private var lastProbe = Date.distantPast, verifyAgain = false, probing = false
    private(set) var screen: PermissionState = .notEffective, access: PermissionState = .notEffective
    private(set) var checking = false, detail = ""
    var onChange: (() -> Void)?
    var usable: Bool { screen.usable && access.usable && !checking }
    var needsFreshCheck: Bool { !probing && !usable && Date().timeIntervalSince(lastProbe) > 5 }
    func resetCaptureHealth() {
        // Display loss and sleep are transient capture faults, not TCC denials.
        verifiedScreen = nil; detail = ""; lastProbe = .distantPast
        refreshCurrent()
    }

    func refreshCurrent() {
        let currentScreen = CGPreflightScreenCaptureAccess()
        if let previousPreflight, previousPreflight != currentScreen { verifiedScreen = nil }
        previousPreflight = currentScreen
        screen = .evaluate(current: currentScreen, fresh: fresh?.screen, verified: verifiedScreen)
        access = .evaluate(current: CGPreflightPostEventAccess(), fresh: fresh?.access)
    }
    func check(verifyCapture: Bool) {
        if probing { verifyAgain = verifyAgain || verifyCapture; return }
        probing = true; lastProbe = Date()
        // Periodic read-only probes keep the previous presentation and error text.
        // Only an explicit capture check displays a busy state.
        if verifyCapture { checking = true; detail = ""; onChange?() }
        Task { @MainActor [weak self] in
            let fresh = await Task.detached { Self.readFresh() }.value
            guard let self else { return }
            if fresh?.screen == true && self.fresh?.screen != true { self.verifiedScreen = nil }
            if let fresh { self.fresh = fresh }
            if verifyCapture {
                do { try await Self.verifyScreenCapture(); self.verifiedScreen = true }
                catch {
                    self.verifiedScreen = false
                    self.detail = ui("屏幕捕获检查失败：", "Screen capture check failed: ") + error.localizedDescription
                }
            }
            self.probing = false; self.checking = false; self.refreshCurrent(); self.onChange?()
            if self.verifyAgain { self.verifyAgain = false; self.check(verifyCapture: true) }
        }
    }
    func captureFailed(_ reason: String) {
        verifiedScreen = nil; detail = ui("捕获未能启动：", "Capture did not start: ") + reason
        refreshCurrent(); onChange?()
    }
    private static func readFresh() -> FreshPermissions? {
        guard let binary = Bundle.main.executableURL else { return nil }
        let process = Process(), output = Pipe(), completed = DispatchSemaphore(value: 0)
        process.executableURL = binary; process.arguments = ["--permissions-json"]
        process.standardOutput = output; process.standardError = FileHandle.nullDevice
        process.terminationHandler = { _ in completed.signal() }
        do {
            try process.run()
            guard completed.wait(timeout: .now() + 3) == .success else { process.terminate(); return nil }
            guard process.terminationStatus == 0 else { return nil }
            return try JSONDecoder().decode(FreshPermissions.self, from: output.fileHandleForReading.readDataToEndOfFile())
        } catch { return nil }
    }
    private static func verifyScreenCapture() async throws {
        // Check the capture API used by the host, not just CoreGraphics' cached preflight.
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        guard let display = content.displays.first else { return }
        let config = SCStreamConfiguration()
        config.width = 64; config.height = 64; config.queueDepth = 3
        config.minimumFrameInterval = CMTime(value: 1, timescale: 60)
        config.showsCursor = false; config.capturesAudio = false
        let stream = SCStream(filter: SCContentFilter(display: display, excludingWindows: []), configuration: config, delegate: nil)
        let sink = PermissionSink()
        try stream.addStreamOutput(sink, type: .screen, sampleHandlerQueue: DispatchQueue(label: "ThunderDisplay.permissionCheck"))
        try await stream.startCapture()
        try await stream.stopCapture()
    }
}
