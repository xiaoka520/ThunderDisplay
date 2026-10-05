import AppKit
import ScreenCaptureKit
import HostState
import OSLog

/// Runs only in launchd's pre-login graphical session. It never presents settings,
/// uses a user's pasteboard, or exposes filesystem / process-control commands.
final class LoginWindowHost: NSObject, NSApplicationDelegate {
    private let logger = Logger(subsystem: "dev.thunderdisplay.host", category: "loginwindow")
    private var timer: Timer?, server: HostServer?, probe: LoginWindowFrameProbe?
    private var signals: [DispatchSourceSignal] = []
    private var phase = LoginWindowState.Phase.checking, detail = "", busy = false
    private var nextCheck: TimeInterval = 0
    private var selectedIP = ""
    private var attemptedWithoutScreenAccess = false
    private var checkStarted: TimeInterval = 0
    private var captureChecked = false, inputChecked = false
    private var lastFailure: String?
    private var options: Options
    init(options: Options) { self.options = options }
    func applicationDidFinishLaunching(_ notification: Notification) {
        guard geteuid() == 0, !ConsoleSession.loggedIn else {
            logger.error("Refusing pre-login host outside root LoginWindow graphics session")
            NSApp.terminate(nil); return
        }
        do {
            let config = try LoginWindowConfiguration.read()
            options.port = config.port; options.requirePairing = config.requirePairing; options.token = config.token
        } catch {
            logger.error("Invalid / missing root-owned LoginWindow configuration")
            publish(.blocked, "LoginWindow configuration unavailable; reinstall startup components")
            NSApp.terminate(nil); return
        }
        logger.notice("LoginWindow graphics agent started; checking real capture and event-post access")
        for signalNumber in [SIGINT, SIGTERM] {
            signal(signalNumber, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: signalNumber, queue: .main)
            source.setEventHandler { NSApp.terminate(nil) }; source.resume(); signals.append(source)
        }
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in self?.refresh() }
        refresh()
    }
    private func publish(_ value: LoginWindowState.Phase, _ reason: String = "") {
        if phase != value || detail != reason {
            logger.notice("LoginWindow phase: \(value.rawValue, privacy: .public); \(reason, privacy: .public)")
        }
        phase = value; detail = reason
        do { try LoginWindowState(pid: getpid(), uptime: ProcessInfo.processInfo.systemUptime, phase: value, detail: reason, port: options.port, captureChecked: captureChecked, inputChecked: inputChecked, lastFailure: lastFailure).write() }
        catch { logger.error("Cannot publish LoginWindow state: \(error.localizedDescription, privacy: .public)") }
    }
    private func refresh() {
        guard !ConsoleSession.loggedIn else {
            // New input is gated per packet; pending key / button releases are cleaned up.
            stop(); publish(.stopped, "User session active; desktop host takes over")
            NSApp.terminate(nil); return
        }
        publish(phase, detail)
        guard ConsoleSession.preLogin else {
            if server != nil { stop() }
            publish(.blocked, "Waiting for a confirmed LoginWindow console session"); return
        }
        if busy && ProcessInfo.processInfo.systemUptime - checkStarted >= 60 {
            probe?.cancel(); lastFailure = "LoginWindow capture service timed out; restarting agent"
            publish(.blocked, lastFailure!)
            // No client / injected input exists during this check. launchd's
            // unsuccessful-exit policy recreates the hung graphical component.
            exit(1)
        }
        guard !busy else { return }
        if server != nil {
            if bridgeAddress() != selectedIP || !CGPreflightPostEventAccess() { fail("Display / bridge or input access changed") }
            return
        }
        guard ProcessInfo.processInfo.systemUptime >= nextCheck else { return }
        if !CGPreflightScreenCaptureAccess() && attemptedWithoutScreenAccess {
            nextCheck = ProcessInfo.processInfo.systemUptime + 3; return
        }
        guard let ip = bridgeAddress() else { publish(.blocked, "Thunderbolt Bridge unavailable"); nextCheck = ProcessInfo.processInfo.systemUptime + 3; return }
        guard CGSessionCopyCurrentDictionary() != nil else { publish(.blocked, "Waiting for LoginWindow graphics session"); nextCheck = ProcessInfo.processInfo.systemUptime + 3; return }
        busy = true; publish(.checking, "Checking a real ScreenCaptureKit frame")
        checkStarted = ProcessInfo.processInfo.systemUptime
        attemptedWithoutScreenAccess = !CGPreflightScreenCaptureAccess()
        Task { @MainActor [self] in
            let probe = LoginWindowFrameProbe(); self.probe = probe
            do {
                let display = try await probe.check()
                attemptedWithoutScreenAccess = false // A valid stream overrides a false ordinary preflight.
                guard !ConsoleSession.loggedIn else { stop(); busy = false; return }
                captureChecked = true
                guard CGPreflightPostEventAccess() else { throw HostError("LoginWindow event-post permission unavailable") }
                inputChecked = true; lastFailure = nil
                let token = options.requirePairing ? try pairingCode(options.token) : ""
                options.bind = ip; options.display = display
                // Tell the nobody discovery daemon to relinquish its port. Its
                // fallback resumes if this process exits or its heartbeat expires.
                publish(.claiming, "Valid frame and input authorization verified")
                try await Task.sleep(nanoseconds: 300_000_000)
                guard !ConsoleSession.loggedIn else { stop(); busy = false; return }
                let host = HostServer(ip: ip, options: options, token: token)
                host.allowClipboard = false; host.restrictToLocalSubnet = true
                host.inputAllowed = { ConsoleSession.preLogin && CGPreflightPostEventAccess() }
                host.displayCapabilities = hostDisplays().first(where: { $0.id == display })?.payload
                host.onCaptureFailure = { [weak self, weak host] reason in DispatchQueue.main.async {
                    guard let self, let host, self.server === host else { return }; self.fail(reason)
                } }
                host.onStatus = { [weak self, weak host] _ in DispatchQueue.main.async {
                    guard let self, let host, self.server === host else { return }
                    // Never log keyboard data, credentials, or a captured frame.
                    self.logger.notice("LoginWindow client session negotiated")
                } }
                try host.start(); server = host; selectedIP = ip
                publish(.listening, "Capture and input checked; waiting for client")
            } catch {
                let systemError = error as NSError
                // Retry transient display / service failures even when ordinary
                // preflight is false; only an actual consent refusal suppresses
                // another automatic request in the same graphical session.
                attemptedWithoutScreenAccess = !CGPreflightScreenCaptureAccess() && systemError.domain == SCStreamErrorDomain && systemError.code == SCStreamError.Code.userDeclined.rawValue
                fail(error.localizedDescription)
            }
            busy = false; self.probe = nil
        }
    }
    private func fail(_ reason: String) {
        lastFailure = reason; inputChecked = false
        stop(); publish(.blocked, reason)
        // Do not repeatedly prompt from loginwindow; retry after permission /
        // display recovery, with a bounded frequency and no interactive UI.
        nextCheck = ProcessInfo.processInfo.systemUptime + 30
    }
    private func stop() { probe?.cancel(); server?.stop(); server = nil; selectedIP = "" }
    func applicationWillTerminate(_ notification: Notification) {
        timer?.invalidate(); stop(); publish(.stopped, "LoginWindow agent stopped")
    }
}

func loginWindowFrameCheck() throws {
    let manager = ConsoleSession.managerName
    guard manager == "Aqua" || manager == "LoginWindow" else { throw HostError("Diagnostic must run in a graphical bootstrap session") }
    let probe = LoginWindowFrameProbe()
    var finished = false, failure: Error?
    Task { @MainActor in
        do {
            let display = try await probe.check()
            print("Real ScreenCaptureKit frame verified: display=\(display), uid=\(geteuid()), consoleLoggedIn=\(ConsoleSession.loggedIn), eventPost=\(CGPreflightPostEventAccess()). No pixels saved or transmitted; no input injected.")
        } catch { failure = error }
        finished = true
    }
    let deadline = Date().addingTimeInterval(15)
    while !finished && Date() < deadline { _ = RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.05)) }
    probe.cancel()
    guard finished else { throw HostError("LoginWindow frame diagnostic timed out") }
    if let failure { throw failure }
}

/// Receive a complete frame before advertising a usable pre-login host. This
/// checks metadata / buffer presence only; no image is saved or transmitted.
final class LoginWindowFrameProbe: NSObject, SCStreamOutput, @unchecked Sendable {
    private var stream: SCStream?, complete = false, cancelled = false
    @MainActor func check() async throws -> CGDirectDisplayID {
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        guard !cancelled, let display = content.displays.first(where: { $0.displayID == CGMainDisplayID() }) ?? content.displays.first else {
            throw HostError("LoginWindow display unavailable / capture cancelled")
        }
        let config = SCStreamConfiguration(); config.width = 64; config.height = 64
        config.queueDepth = 3; config.showsCursor = true; config.capturesAudio = false
        config.minimumFrameInterval = CMTime(value: 1, timescale: 10)
        let capture = SCStream(filter: SCContentFilter(display: display, excludingWindows: []), configuration: config, delegate: nil)
        try capture.addStreamOutput(self, type: .screen, sampleHandlerQueue: .main); stream = capture
        do {
            try await capture.startCapture()
            for _ in 0..<100 {
                if complete || cancelled { break }; try await Task.sleep(nanoseconds: 50_000_000)
            }
            try? await capture.stopCapture(); stream = nil
            guard complete, !cancelled else { throw HostError("LoginWindow did not produce a valid screen frame") }
            return display.displayID
        } catch { try? await capture.stopCapture(); stream = nil; throw error }
    }
    nonisolated func stream(_ stream: SCStream, didOutputSampleBuffer buffer: CMSampleBuffer, of type: SCStreamOutputType) {
        // SCStream delivery is explicitly on DispatchQueue.main above.
        MainActor.assumeIsolated {
            guard type == .screen, CMSampleBufferIsValid(buffer), CMSampleBufferGetImageBuffer(buffer) != nil,
                  let attachments = CMSampleBufferGetSampleAttachmentsArray(buffer, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
                  let status = attachments.first?[.status] as? Int, status == SCFrameStatus.complete.rawValue else { return }
            complete = true
        }
    }
    func cancel() { cancelled = true; if let stream { Task { try? await stream.stopCapture() } } }
}
