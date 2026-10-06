import AppKit
import ScreenCaptureKit
import HostState
import OSLog
import InputSupport

/// Runs only in launchd's pre-login graphical session. It never presents settings,
/// uses a user's pasteboard, or exposes filesystem / process-control commands.
final class LoginWindowHost: NSObject, NSApplicationDelegate {
    private let logger = Logger(subsystem: "dev.thunderdisplay.host", category: "loginwindow")
    private var timer: Timer?, sessionTimer: Timer?, server: HostServer?, probe: LoginWindowFrameProbe?
    private var signals: [DispatchSourceSignal] = []
    private var phase = LoginWindowState.Phase.checking, detail = "", busy = false
    private var nextCheck: TimeInterval = 0
    private var selectedIP = ""
    private var attemptedWithoutScreenAccess = false
    private var watchdog: PreparationWatchdog?
    private var input: LoginWindowInput?
    private var configuration: LoginWindowConfiguration?
    private var handingOver = false
    private var captureChecked = false, inputChecked = false
    private var lastFailure: String?
    /// Snapshot taken the first time this agent publishes `stopped`, so the reason
    /// it never started capture survives the login handover in the heartbeat file
    /// instead of being replaced by a fixed "agent stopped" message.
    private var stoppingPhase: LoginWindowState.Phase?
    private var stoppingDetail: String?
    private var options: Options
    init(options: Options) { self.options = options }
    func applicationDidFinishLaunching(_ notification: Notification) {
        guard geteuid() == 0, !ConsoleSession.loggedIn else {
            logger.error("Refusing pre-login host outside root LoginWindow graphics session")
            NSApp.terminate(nil); return
        }
        do {
            let config = try LoginWindowConfiguration.read()
            configuration = config
            options.port = config.port; options.requirePairing = config.requirePairing; options.token = config.token
        } catch {
            logger.error("Invalid / missing root-owned LoginWindow configuration")
            publish(.blocked, "LoginWindow configuration unavailable; reinstall startup components")
            NSApp.terminate(nil); return
        }
        logger.notice("LoginWindow graphics agent started; checking real capture and event-post access")
        let logger = self.logger, port = options.port
        watchdog = PreparationWatchdog { stage in
            let reason = "LoginWindow preparation timed out: " + stage + "; restarting agent"
            logger.error("\(reason, privacy: .public)")
            let last = LoginWindowState.readLastRecorded()
            try? LoginWindowState(pid: getpid(), uptime: ProcessInfo.processInfo.systemUptime, phase: .blocked,
                detail: reason, port: port, captureChecked: last?.captureChecked ?? false,
                inputChecked: false, lastFailure: reason).write()
            // This watchdog is armed only before any client input is accepted.
            // A blocked system call cannot be cancelled on the AppKit run loop.
            _exit(1)
        }
        for signalNumber in [SIGINT, SIGTERM] {
            signal(signalNumber, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: signalNumber, queue: .main)
            source.setEventHandler { NSApp.terminate(nil) }; source.resume(); signals.append(source)
        }
        timer = Timer.scheduledTimer(withTimeInterval: 1, repeats: true) { [weak self] _ in self?.refresh() }
        // Capture/service health needs only the ordinary heartbeat. Detect the
        // login transition promptly without rewriting its status file at 10 Hz.
        sessionTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            guard let self, ConsoleSession.loggedIn else { return }
            self.refresh()
        }
        refresh()
    }
    private func publish(_ value: LoginWindowState.Phase, _ reason: String = "") {
        if value == .stopped, stoppingPhase == nil, phase != .stopped {
            stoppingPhase = phase; stoppingDetail = detail
        }
        if phase != value || detail != reason {
            logger.notice("LoginWindow phase: \(value.rawValue, privacy: .public); \(reason, privacy: .public)")
        }
        phase = value; detail = reason
        do { try LoginWindowState(pid: getpid(), uptime: ProcessInfo.processInfo.systemUptime, phase: value, detail: reason, port: options.port, captureChecked: captureChecked, inputChecked: inputChecked, lastFailure: lastFailure, previousPhase: stoppingPhase, previousDetail: stoppingDetail).write() }
        catch { logger.error("Cannot publish LoginWindow state: \(error.localizedDescription, privacy: .public)") }
    }
    private func refresh() {
        guard !handingOver else { return }
        guard !ConsoleSession.loggedIn else {
            // New input is gated per packet; pending key / button releases are cleaned up.
            handOverToDesktop(); return
        }
        publish(phase, detail)
        // The bootstrap is validated at entry; the real capture/input checks
        // below establish readiness without another console-dictionary gate.
        guard !busy else { return }
        if server != nil {
            if bridgeAddress() != selectedIP || input?.isAuthorized != true { fail("Display / bridge or input access changed") }
            return
        }
        guard ProcessInfo.processInfo.systemUptime >= nextCheck else { return }
        if !CGPreflightScreenCaptureAccess() && attemptedWithoutScreenAccess {
            nextCheck = ProcessInfo.processInfo.systemUptime + 3; return
        }
        guard let ip = bridgeAddress() else { publish(.blocked, "Thunderbolt Bridge unavailable"); nextCheck = ProcessInfo.processInfo.systemUptime + 3; return }
        busy = true; publish(.checking, "Checking a real ScreenCaptureKit frame")
        watchdog?.arm("ScreenCaptureKit frame", timeout: 60)
        attemptedWithoutScreenAccess = !CGPreflightScreenCaptureAccess()
        Task { @MainActor [self] in
            let probe = LoginWindowFrameProbe(); self.probe = probe
            do {
                let display = try await probe.check()
                attemptedWithoutScreenAccess = false // A valid stream overrides a false ordinary preflight.
                guard !ConsoleSession.loggedIn else { stop(); busy = false; return }
                captureChecked = true
                publish(.checking, "Screen frame verified; checking pre-login graphics marker and input authorization")
                watchdog?.arm("Pre-login Quartz input", timeout: 15)
                let input = try LoginWindowInput()
                self.input = input
                logger.notice("LoginWindow input: pre-login executable marker present; authorized Quartz session; delivery awaits client verification")
                inputChecked = true; lastFailure = nil
                let token = options.requirePairing ? try pairingCode(options.token) : ""
                options.bind = ip; options.display = display
                // Tell the nobody discovery daemon to relinquish its port. Its
                // fallback resumes if this process exits or its heartbeat expires.
                publish(.claiming, "Valid frame and " + LoginWindowInput.backendName + " input authorization checked")
                guard !ConsoleSession.loggedIn else { stop(); busy = false; return }
                let host = HostServer(ip: ip, options: options, token: token)
                host.allowClipboard = false; host.restrictToLocalSubnet = true
                host.handoverOnSessionEnd = true
                host.inputAllowed = { ConsoleSession.preLogin }
                let inputLogger = self.logger
                host.onKeyboardPacket = {
                    inputLogger.notice("First remote keyboard packet received in LoginWindow; no key codes or text recorded")
                }
                host.makeInputInjector = { [weak self, weak host] id, size, rect in
                    QueuedInputController(allowed: { ConsoleSession.preLogin }, factory: {
                        InputInjector(display: id, captureSize: size, contentRect: rect,
                            authorized: { input.isAuthorized }, post: input.post, eventSource: nil)
                    }, onFailure: { reason in
                        guard let self, let host, self.server === host else { return }
                        self.fail("System input: " + reason)
                    })
                }
                host.onInputFailure = { [weak self, weak host] reason in DispatchQueue.main.async {
                    guard let self, let host, self.server === host else { return }
                    self.fail("System input: " + reason)
                } }
                // The loginwindow session can hold a different display mode than the
                // user session, and this session's mode is what the client negotiates
                // its stream size against. Record both so a wrong pre-login aspect can
                // be attributed to the advertised mode instead of guessed at.
                let advertised = hostDisplays().first(where: { $0.id == display })
                logger.notice("Pre-login display: \(advertised?.name ?? "unknown", privacy: .public) render \(advertised?.renderText ?? "?", privacy: .public) logical \(advertised?.logicalWidth ?? 0, privacy: .public)x\(advertised?.logicalHeight ?? 0, privacy: .public) hz \(advertised?.captureHz ?? 0, privacy: .public)")
                host.displayCapabilities = advertised?.payload
                host.onCaptureFailure = { [weak self, weak host] reason in DispatchQueue.main.async {
                    guard let self, let host, self.server === host else { return }; self.fail(reason)
                } }
                host.onStatus = { [weak self, weak host] value in DispatchQueue.main.async {
                    guard let self, let host, self.server === host else { return }
                    // Never log keyboard data, credentials, or a captured frame. The
                    // negotiated stream size is what the client asked for, and it is
                    // the other half of any aspect mismatch.
                    self.logger.notice("LoginWindow client negotiated: \(value, privacy: .public)")
                } }
                watchdog?.disarm()
                let claimDeadline = ProcessInfo.processInfo.systemUptime + 2
                while true {
                    guard !ConsoleSession.loggedIn else { stop(); busy = false; return }
                    do { try host.start(); break }
                    catch let error as HostBindError where error.addressInUse && ProcessInfo.processInfo.systemUptime < claimDeadline {
                        try await Task.sleep(nanoseconds: 50_000_000)
                    }
                }
                server = host; selectedIP = ip
                publish(.listening, "Screen ready; " + LoginWindowInput.backendName + " input authorized; waiting for client")
            } catch {
                guard !handingOver, !ConsoleSession.loggedIn else {
                    busy = false; self.probe = nil; return
                }
                let systemError = error as NSError
                // Retry transient display / service failures even when ordinary
                // preflight is false; only an actual consent refusal suppresses
                // another automatic request in the same graphical session.
                let refused = systemError.domain == SCStreamErrorDomain && systemError.code == SCStreamError.Code.userDeclined.rawValue
                attemptedWithoutScreenAccess = !CGPreflightScreenCaptureAccess() && refused
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
    private func handOverToDesktop() {
        handingOver = true; timer?.invalidate(); sessionTimer?.invalidate(); watchdog?.disarm()
        // RunAtLoad can remain pending while the new GUI domain is in
        // on-demand-only mode. Demand the existing job, without killing an app
        // that is already running or spawning a root process in the user's GUI.
        var request = requestDesktopStartup()
        stop(); publish(.stopped, "User session active; desktop host takes over")
        Task { @MainActor [self] in
            let deadline = ProcessInfo.processInfo.systemUptime + 2
            while let process = request {
                while process.isRunning && ProcessInfo.processInfo.systemUptime < deadline {
                    try? await Task.sleep(nanoseconds: 20_000_000)
                }
                if process.isRunning {
                    process.terminate() // Only our launchctl child, never the desktop job.
                    logger.error("Desktop startup request timed out; ordinary login startup remains available")
                    break
                }
                if process.terminationStatus == 0 {
                    logger.notice("Desktop startup job requested; waiting for its real first frame")
                    break
                }
                guard ProcessInfo.processInfo.systemUptime < deadline else {
                    logger.error("Desktop startup job not yet available; ordinary login startup remains available")
                    break
                }
                try? await Task.sleep(nanoseconds: 100_000_000)
                request = requestDesktopStartup()
            }
            NSApp.terminate(nil)
        }
    }
    private func requestDesktopStartup() -> Process? {
        guard geteuid() == 0, ConsoleSession.loggedIn,
              let target = configuration?.desktopStartupTarget(consoleUID: ConsoleSession.uid) else { return nil }
        let process = Process(); process.executableURL = URL(fileURLWithPath: "/bin/launchctl")
        process.arguments = ["kickstart", target]
        process.standardOutput = FileHandle.nullDevice; process.standardError = FileHandle.nullDevice
        do { try process.run(); return process }
        catch { logger.error("Cannot request the configured desktop startup job"); return nil }
    }
    private func stop() { probe?.cancel(); server?.stop(handover: ConsoleSession.loggedIn); watchdog?.disarm(); server = nil; input = nil; selectedIP = "" }
    func applicationWillTerminate(_ notification: Notification) {
        // `publish` snapshots the phase and reason it is leaving, so the login
        // handover keeps the evidence of why pre-login capture never started.
        timer?.invalidate(); sessionTimer?.invalidate(); stop(); publish(.stopped, "LoginWindow agent stopped")
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
