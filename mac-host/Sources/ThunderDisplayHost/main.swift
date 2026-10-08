import AppKit
import ScreenCaptureKit
import HostState
import InputSupport

final class AppDelegate: NSObject, NSApplicationDelegate {
    private var server: HostServer?, item: NSStatusItem?, setup: SetupWindow?, timer: Timer?
    private var code = "", status = "", boundIP: String?, boundPort: UInt16?
    private var statusIsError = false
    private var signals: [DispatchSourceSignal] = []
    private var handoverRetry: Timer?
    private var startupRetry: Timer?
    private var recovery = HostRecoveryState()
    private var workspaceObservers: [NSObjectProtocol] = []
    private var lastDisplayRefresh: TimeInterval = 0
    private var capturedDisplay: HostDisplay?, capturedBounds: CGRect?, capturedDesktopBounds: [CGRect] = []
    private let power = HostPowerManager()
    private var promptedScreen = false, promptedAccess = false
    private let permissions = PermissionMonitor()
    private var permissionHelper: PermissionHelperWindow?
    private let clipboard = ClipboardBridge()
    private let cursor = NativeCursorMonitor()
    private let startup = StartupServices()
    private var darkIcon: Bool?
    private let options: Options
    init(options: Options) { self.options = options }

    func applicationDidFinishLaunching(_ notification: Notification) {
        log("Desktop app started: uid=\(geteuid()); startup=\(options.background ? "background" : "interactive")")
        let menu = NSMenu(), appItem = NSMenuItem(), editItem = NSMenuItem()
        let appMenu = NSMenu(title: "ThunderDisplay"), editMenu = NSMenu(title: ui("编辑", "Edit"))
        appMenu.addItem(withTitle: ui("设置…", "Settings…"), action: #selector(showSettings), keyEquivalent: ",").target = self
        appMenu.addItem(.separator())
        appMenu.addItem(withTitle: ui("退出 ThunderDisplay", "Quit ThunderDisplay"), action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        for (title, action, key) in [(ui("剪切", "Cut"), "cut:", "x"), (ui("复制", "Copy"), "copy:", "c"), (ui("粘贴", "Paste"), "paste:", "v"), (ui("全选", "Select All"), "selectAll:", "a")] {
            editMenu.addItem(withTitle: title, action: NSSelectorFromString(action), keyEquivalent: key)
        }
        appItem.submenu = appMenu; editItem.submenu = editMenu; menu.addItem(appItem); menu.addItem(editItem); NSApp.mainMenu = menu
        // Build the UI before checking permissions or network availability.
        let setup = SetupWindow(); self.setup = setup
        setup.onLoginStartup = { [weak self] in self?.changeStartup(boot: false) }
        setup.onBootStartup = { [weak self] in self?.changeStartup(boot: true) }
        setup.onStartupSettings = { [weak self] in self?.startup.openSettings() }
        setup.onRepairStartup = { [weak self] in
            guard let self, !self.options.previewUI else { return }
            do { try self.startup.setBoot(true); self.startupError = "" }
            catch { self.startupError = error.localizedDescription }
            self.updateStartup()
        }
        setup.keepAwake.state = !options.recoveryCheck && UserDefaults.standard.object(forKey: "keepHostAwake") as? Bool == false ? .off : .on
        setup.onKeepAwake = { [weak self] in
            guard let self, !self.options.previewUI else { return }
            UserDefaults.standard.set(self.setup?.keepAwake.state == .on, forKey: "keepHostAwake")
            self.refresh()
        }
        if !options.previewUI && !options.recoveryCheck {
            do { try startup.migrateLoginIfNeeded() } catch { startupError = error.localizedDescription }
        }
        updateStartup()
        setup.allowClipboard.state = UserDefaults.standard.object(forKey: "allowClipboard") as? Bool == false ? .off : .on
        setup.clipboardState.stringValue = ui("尚未同步 · 需要两端均开启并建立连接", "Inactive · both platforms must enable sync and connect")
        setup.onClipboard = { [weak self] in
            guard let self, let setup = self.setup else { return }
            let allowed = setup.allowClipboard.state == .on
            UserDefaults.standard.set(allowed, forKey: "allowClipboard")
            self.server?.updateClipboardPermission(allowed)
        }
        if let display = options.display { setup.refreshDisplays(select: display) }
        setup.ip.stringValue = options.bind ?? UserDefaults.standard.string(forKey: "hostIPv4") ?? ""
        setup.port.stringValue = String(options.recoveryCheck || CommandLine.arguments.contains("--port") ? Int(options.port) : (UserDefaults.standard.object(forKey: "hostPort") as? Int ?? Int(options.port)))
        setup.usePairing.state = (options.pairingOverride ?? UserDefaults.standard.bool(forKey: "requirePairing")) ? .on : .off
        loadPairing()
        permissions.onChange = { [weak self] in self?.refresh() }
        setup.onPairing = { [weak self] in self?.loadPairing(); self?.refresh() }
        setup.onLocate = { NSWorkspace.shared.activateFileViewerSelecting([Bundle.main.bundleURL]) }
        setup.onAuthorize = { [weak self] in self?.requestPermissions() }
        setup.onScreen = { [weak self] in self?.requestScreen(openSettings: true) }
        setup.onAccess = { [weak self] in self?.requestAccess(openSettings: true) }
        setup.onRefresh = { [weak self] in
            guard let self, !self.options.previewUI else { return }
            self.statusIsError = false; self.permissions.check(verifyCapture: true)
        }
        setup.onStart = { [weak self] in
            guard let self else { return }; self.recovery.start(at: ProcessInfo.processInfo.systemUptime); self.refresh()
        }
        setup.onStop = { [weak self] in
            guard let self else { return }; self.recovery.pause(); self.stopServer(); self.refresh()
        }
        setup.onRestart = { [weak self] in self?.relaunch() }
        setup.onCopy = { [weak self] in self?.copyPairing() }
        setup.onDisplay = { [weak self] in
            self?.displayConfigurationChanged()
        }
        if !options.previewUI { observeWorkspace() }
        item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        updateApplicationIcon()
        for sig in [SIGINT, SIGTERM] {
            signal(sig, SIG_IGN)
            let source = DispatchSource.makeSignalSource(signal: sig, queue: .main)
            source.setEventHandler { NSApp.terminate(nil) }; source.resume(); signals.append(source)
        }
        refresh()
        beginStartupPolling()
        if !options.background { setup.present() }
        timer = Timer.scheduledTimer(withTimeInterval: 1.5, repeats: true) { [weak self] _ in self?.refresh() }
        if options.recoveryCheck { runRecoveryCheck() }
    }
    func applicationDidBecomeActive(_ notification: Notification) { refresh() }
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        setup?.present(); refresh(); return true
    }
    private func observeWorkspace() {
        let center = NSWorkspace.shared.notificationCenter
        workspaceObservers.append(center.addObserver(forName: NSWorkspace.willSleepNotification, object: nil, queue: .main) { [weak self] _ in
            guard let self else { return }
            self.recovery.willSleep(); self.power.update(enabled: false)
            self.stopServer(); self.refresh()
        })
        for name in [NSWorkspace.didWakeNotification, NSWorkspace.screensDidWakeNotification] {
            workspaceObservers.append(center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in self?.resumeAfterWake() })
        }
        workspaceObservers.append(center.addObserver(forName: NSWorkspace.sessionDidResignActiveNotification, object: nil, queue: .main) { [weak self] _ in
            guard let self else { return }
            self.stopServer(); self.power.update(enabled: false); self.refresh()
        })
        workspaceObservers.append(center.addObserver(forName: NSWorkspace.sessionDidBecomeActiveNotification, object: nil, queue: .main) { [weak self] _ in
            guard let self else { return }
            self.recovery.sessionBecameActive(at: ProcessInfo.processInfo.systemUptime)
            self.permissions.resetCaptureHealth(); self.setup?.refreshDisplays(); self.refresh()
            self.beginStartupPolling()
        })
    }
    private func beginStartupPolling() {
        guard !options.previewUI, !options.recoveryCheck, server == nil, recovery.requested,
              !recovery.sleeping, startupRetry == nil else { return }
        let deadline = ProcessInfo.processInfo.systemUptime + 15
        startupRetry = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] timer in
            guard let self else { timer.invalidate(); return }
            guard self.server == nil, self.recovery.requested, !self.recovery.sleeping,
                  ProcessInfo.processInfo.systemUptime < deadline else {
                timer.invalidate(); self.startupRetry = nil; return
            }
            self.refresh()
        }
    }
    private func runRecoveryCheck() {
        Task { @MainActor in
            func ready(_ phase: String) async throws {
                let deadline = ProcessInfo.processInfo.systemUptime + 20
                while server == nil {
                    guard ProcessInfo.processInfo.systemUptime < deadline else { throw HostError("Host recovery timed out: \(phase); \(status)") }
                    try await Task.sleep(nanoseconds: 100_000_000)
                }
                guard power.active else { throw HostError("Keep-awake assertion missing: \(phase)") }
                log("Recovery check: \(phase)")
                // Allow the external loopback probe to receive a real encoded keyframe.
                try await Task.sleep(nanoseconds: 8_000_000_000)
            }
            do {
                try await ready("AUTOSTART")
                NSWorkspace.shared.notificationCenter.post(name: NSWorkspace.willSleepNotification, object: nil)
                guard server == nil && !power.active && recovery.sleeping && recovery.requested else { throw HostError("Sleep did not suspend the host while preserving startup intent") }
                log("Recovery check: SUSPENDED")
                try await Task.sleep(nanoseconds: 1_000_000_000)
                NSWorkspace.shared.notificationCenter.post(name: NSWorkspace.didWakeNotification, object: nil)
                try await ready("WAKE")
                let unchanged = server
                NotificationCenter.default.post(name: NSApplication.didChangeScreenParametersNotification, object: nil)
                guard server === unchanged else { throw HostError("Unchanged display geometry interrupted the live host") }
                // Simulate a stale arrangement snapshot, without changing any
                // physical/virtual display or desktop mode on the user's Mac.
                capturedDesktopBounds = []
                NotificationCenter.default.post(name: NSApplication.didChangeScreenParametersNotification, object: nil)
                guard server == nil && recovery.requested else { throw HostError("Display change disabled automatic recovery") }
                try await ready("DISPLAY")
                log("Recovery check: PASS (local notifications; no physical system sleep)")
                NSApp.terminate(nil)
            } catch {
                log("Recovery check: FAIL \(error)"); NSApp.terminate(nil)
            }
        }
    }
    private func resumeAfterWake() {
        recovery.didWake(at: ProcessInfo.processInfo.systemUptime)
        stopServer(); permissions.resetCaptureHealth(); setup?.refreshDisplays()
        statusIsError = false; lastDisplayRefresh = 0
        log("Wake: rebuilding display capture and Thunderbolt host automatically")
        refresh()
    }
    private func displayConfigurationChanged() {
        guard !options.previewUI else { return }
        recovery.displaysChanged(at: ProcessInfo.processInfo.systemUptime)
        setup?.refreshDisplays()
        // Compare with the configuration used to create capture/input. Two fresh
        // CGDisplayBounds reads always agree and miss moves, hotplug and resizing.
        if server != nil, let previous = capturedDisplay, let current = setup?.selectedDisplay,
           previous.id == current.id, previous.width == current.width, previous.height == current.height,
           capturedBounds == CGDisplayBounds(current.id), capturedDesktopBounds == DesktopPointer.activeDisplayBounds() {
            server?.updateDisplayCapabilities(current.payload); capturedDisplay = current
            refresh(); return
        }
        log("Display selection, geometry or arrangement changed; rebuilding capture and input")
        stopServer(); permissions.resetCaptureHealth(); statusIsError = false
        refresh()
    }
    private func refresh() {
        updateApplicationIcon()
        guard let setup else { return }
        let now = ProcessInfo.processInfo.systemUptime
        updateStartup()
        if !options.previewUI && !options.recoveryCheck && !ConsoleSession.ownsDesktop(geteuid()) {
            // Fast user switching can leave this app alive while loginwindow
            // has the console. Release the port without disabling recovery intent.
            if server != nil { stopServer() }
            power.update(enabled: false)
            status = ui("等待返回当前用户桌面；会自动恢复主机。", "Waiting for this user's desktop; host resumes automatically.")
            setup.update(screen: permissions.screen, access: permissions.access, checking: false, running: false, detectedIP: bridgeAddress(), status: status, detail: permissions.detail)
            updateMenu(); return
        }
        power.update(enabled: !options.previewUI && setup.keepAwake.state == .on && recovery.requested && !recovery.sleeping)
        setup.powerState.stringValue = power.active ? ui("已防止系统自动睡眠 · 屏幕可正常熄灭", "Automatic system sleep prevented; display sleep remains available") :
            power.error.map { ui("保持唤醒失败：", "Keep-awake failed: ") + $0 } ?? ui("未保持唤醒", "Keep-awake inactive")
        if !options.previewUI && !recovery.sleeping {
            permissions.refreshCurrent()
            if permissions.needsFreshCheck { permissions.check(verifyCapture: false) }
        }
        let displayInterval: TimeInterval = startupRetry != nil && setup.selectedDisplay == nil ? 0.1 : 1
        if !options.previewUI && !recovery.sleeping && server == nil && now - lastDisplayRefresh >= displayInterval {
            lastDisplayRefresh = now; setup.refreshDisplays()
        }
        let screen: PermissionState = options.previewUI ? .notEffective : permissions.screen
        let access: PermissionState = options.previewUI ? .notEffective : permissions.access
        let detected = bridgeAddress()
        let candidate = setup.ip.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        if options.previewUI { status = ui("界面预览 · 不申请权限、不启动服务", "UI preview · Permissions and host startup disabled") }
        else if recovery.sleeping { status = ui("Mac 正在睡眠，唤醒后将自动恢复主机。", "Mac sleeping; host resumes automatically after wake.") }
        else if server == nil, recovery.canStart(at: now), permissions.usable, setup.selectedDisplay != nil,
                (setup.usePairing.state == .off || !code.isEmpty), !(candidate.isEmpty && detected == nil) { startServer() }
        if !options.previewUI, !recovery.sleeping, server == nil, recovery.requested, !statusIsError {
            if permissions.checking { status = ui("正在复查当前应用的权限…", "Checking this app's permission state…") }
            else if screen == .relaunchRequired || access == .relaunchRequired {
                status = ui("授权已经生效到新进程；请重启此应用以应用权限。", "A fresh process has access. Relaunch this app to apply permissions.")
            } else if !permissions.usable {
                status = ui("当前进程权限尚未生效；如果系统开关已开，请点“检查实际权限”。", "This process does not have effective access. If the system toggle is on, verify access.")
            } else if setup.selectedDisplay == nil { status = ui("等待显示器恢复，主机会自动启动。", "Waiting for displays; host starts automatically when ready.") }
            else if now < recovery.retryAt { status = ui("正在恢复显示器和网桥，主机将自动重新启动…", "Recovering display and bridge; host restarts automatically…") }
            else { status = ui("等待网桥就绪，主机会自动启动。", "Waiting for the bridge; host starts automatically when ready.") }
        }
        setup.update(screen: screen, access: access, checking: permissions.checking, running: server != nil, detectedIP: detected, status: status, detail: permissions.detail)
        permissionHelper?.update(screen: screen, access: access, checking: permissions.checking, preview: options.previewUI)
        if PermissionState.authorizationComplete(screen: screen, access: access) {
            permissionHelper?.close(); permissionHelper = nil
        }
        if options.previewUI { setup.start.isEnabled = false; setup.restart.isEnabled = false; setup.authorize.isEnabled = false }
        // Relaunch is always available: screen permission may become visible only after relaunch.
        if !options.previewUI { setup.restart.isEnabled = true }
        updateMenu()
    }
    private func updateStartup() {
        guard let setup else { return }
        setup.launchAtLogin.state = startup.loginEnabled ? .on : startup.login.status == .requiresApproval ? .mixed : .off
        setup.launchAtBoot.state = startup.bootState == .running && startup.loginWindowInstalled && startup.desktopStartupInstalled ? .on : startup.bootConfigured ? .mixed : .off
        if !startupError.isEmpty { setup.startupState.stringValue = startupError; return }
        setup.startupState.stringValue = ui("登录启动：", "Login startup: ") + startup.status(startup.login) + " · " + ui("开机服务：", "Boot service: ") + startup.bootStatus + "\n" + startup.loginWindowStatus + "\n" + ui("当前桌面主机：", "Current desktop host: ") + (server != nil ? ui("运行中", "Running") : ui("未运行（检查权限与网桥）", "Not running (check permissions and bridge)"))
    }
    private var startupError = ""
    private func changeStartup(boot: Bool) {
        guard !options.previewUI, let setup else { return }
        do {
            if boot { try startup.setBoot(setup.launchAtBoot.state == .on) }
            else { try startup.setLogin(setup.launchAtLogin.state == .on) }
            startupError = ""
        } catch {
            startupError = error.localizedDescription
        }
        updateStartup()
    }
    private func requestPermissions() {
        guard !options.previewUI else { return }
        showPermissionHelper()
        // Both requests are made in one session; a declined request never terminates the app.
        requestAccess(openSettings: false)
        requestScreen(openSettings: false)
        refresh()
    }
    private func requestScreen(openSettings: Bool) {
        guard !options.previewUI else { return }
        if openSettings { showPermissionHelper() }
        if !CGPreflightScreenCaptureAccess(), !promptedScreen {
            promptedScreen = true; _ = CGRequestScreenCaptureAccess()
        }
        if openSettings || (!CGPreflightScreenCaptureAccess() && promptedScreen) {
            openPrivacy("Privacy_ScreenCapture")
        }
        status = ui("在系统设置中允许屏幕录制和辅助功能，返回此窗口即可自动检查。若系统要求重启，请完成两个权限后再重启一次。", "Allow Screen Recording and Accessibility in System Settings, then return here. If macOS requires a relaunch, finish both permissions before relaunching once.")
        refresh()
    }
    private func requestAccess(openSettings: Bool) {
        guard !options.previewUI else { return }
        if openSettings { showPermissionHelper() }
        if !CGPreflightPostEventAccess(), !promptedAccess {
            promptedAccess = true
            _ = CGRequestPostEventAccess()
        }
        if openSettings || (!CGPreflightPostEventAccess() && promptedAccess) { openPrivacy("Privacy_Accessibility") }
        refresh()
    }
    private func openPrivacy(_ pane: String) {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?\(pane)") { NSWorkspace.shared.open(url) }
    }
    private func showPermissionHelper() {
        permissions.refreshCurrent()
        guard !PermissionState.authorizationComplete(screen: permissions.screen, access: permissions.access) else { return }
        if permissionHelper == nil {
            let helper = PermissionHelperWindow()
            helper.onScreen = { [weak self] in self?.openPrivacy("Privacy_ScreenCapture") }
            helper.onAccess = { [weak self] in self?.openPrivacy("Privacy_Accessibility") }
            helper.onVerify = { [weak self] in
                guard let self, !self.options.previewUI else { return }
                self.statusIsError = false; self.permissions.check(verifyCapture: true)
            }
            helper.onRestart = { [weak self] in self?.relaunch() }
            permissionHelper = helper
        }
        permissionHelper?.update(screen: permissions.screen, access: permissions.access,
                                 checking: permissions.checking, preview: options.previewUI)
        permissionHelper?.present()
    }
    private func startServer() {
        guard !options.previewUI, !recovery.sleeping, recovery.requested, server == nil, let setup else { return }
        permissions.refreshCurrent()
        guard permissions.usable else { permissions.check(verifyCapture: true); return }
        let entered = setup.ip.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let ip = entered.isEmpty ? bridgeAddress() : entered as String? else {
            status = ui("请配置雷雳网桥 IPv4，或填写一个本机 IPv4 地址。", "Configure Thunderbolt Bridge IPv4 or enter a local IPv4 address."); return
        }
        do {
            guard setup.usePairing.state == .off || !code.isEmpty else { throw HostError(ui("配对码不可用", "Pairing code unavailable")) }
            guard ip != "0.0.0.0", let port = UInt16(setup.port.stringValue), port > 0 else {
                throw HostError(ui("请填写有效的本机 IPv4 和 1–65535 端口", "Enter a local IPv4 address and a port from 1 to 65535"))
            }
            _ = try address(ip, port: port)
            var selected = options; selected.bind = ip; selected.port = port
            guard let display = setup.selectedDisplay else { throw HostError(ui("没有可用显示器", "No display available")) }
            selected.display = display.id
            selected.requirePairing = setup.usePairing.state == .on
            let host = HostServer(ip: ip, options: selected, token: code)
            host.inputAllowed = { ConsoleSession.ownsDesktop(geteuid()) }
            host.displayCapabilities = display.payload
            host.desktopBounds = DesktopPointer.activeDisplayBounds()
            host.localCursorAvailable = cursor.available
            host.allowClipboard = setup.allowClipboard.state == .on
            clipboard.onText = { [weak host] session, text in host?.sendClipboard(text, session: session) }
            clipboard.onImage = { [weak host] session, image in host?.sendImage(image, session: session) }
            cursor.onImage = { [weak host] session, image in host?.sendCursor(image, session: session) }
            cursor.onPosition = { [weak host] session, point, visible in host?.sendCursorPosition(point, visible: visible, session: session) }
            host.onCursorState = { [weak self, weak host] session, enabled, variants in DispatchQueue.main.async {
                guard let self, let host, self.server === host else { return }
                self.cursor.setSession(session, enabled: enabled, variants: variants)
            } }
            host.onClipboardImage = { [weak self, weak host] session, image in DispatchQueue.main.async {
                guard let self, let host, self.server === host else { return }
                self.clipboard.receiveImage(image, session: session)
            } }
            host.onClipboardState = { [weak self, weak host] session, enabled in DispatchQueue.main.async {
                guard let self, let host, self.server === host else { return }
                self.clipboard.setSession(session, enabled: enabled)
                self.setup?.clipboardState.stringValue = enabled ? ui("已连接 · 文字和图片同步开启", "Connected · text and image sync enabled") : ui("剪贴板同步未开启", "Clipboard sync inactive")
            } }
            host.onClipboardText = { [weak self, weak host] session, text in DispatchQueue.main.async {
                guard let self, let host, self.server === host else { return }
                self.clipboard.receive(text, session: session)
            } }
            host.onStatus = { [weak self, weak host] value in DispatchQueue.main.async {
                guard let self, let host, self.server === host else { return }
                self.status = value.hasPrefix("Connected:") ? value.replacingOccurrences(of: "Connected:", with: ui("已连接：", "Connected:")) :
                    ui("服务已启动，等待 Windows 连接。", "Host ready. Waiting for Windows.")
                self.refresh()
            } }
            host.onCaptureFailure = { [weak self, weak host] reason in DispatchQueue.main.async {
                guard let self, let host, self.server === host else { return }
                self.recovery.failed(at: ProcessInfo.processInfo.systemUptime)
                self.stopServer(); self.permissions.resetCaptureHealth(); self.statusIsError = true
                self.status = ui("捕获中断，正在自动恢复：", "Capture interrupted; recovering automatically: ") + reason
                self.refresh()
            } }
            try host.start(); server = host; boundIP = ip; boundPort = port; statusIsError = false
            capturedDisplay = display; capturedBounds = CGDisplayBounds(display.id); capturedDesktopBounds = host.desktopBounds
            handoverRetry?.invalidate(); handoverRetry = nil
            startupRetry?.invalidate(); startupRetry = nil
            recovery.started()
            if !options.recoveryCheck {
                UserDefaults.standard.set(entered, forKey: "hostIPv4")
                UserDefaults.standard.set(Int(port), forKey: "hostPort")
            }
            status = ui("服务已启动：", "Host listening: ") + "\(ip):\(port) · " + ui("等待 Windows 连接", "Waiting for Windows")
            setup.update(screen: permissions.screen, access: permissions.access, checking: false, running: true, detectedIP: bridgeAddress(), status: status, detail: permissions.detail)
            updateMenu()
        } catch {
            let handingOver = (error as? HostBindError)?.addressInUse == true
            recovery.failed(at: ProcessInfo.processInfo.systemUptime, handover: handingOver)
            statusIsError = true
            status = handingOver ? ui("正在接管监听端口，稍后自动重试…", "Taking over the listening port; retrying shortly…") : ui("启动失败：", "Unable to start: ") + String(describing: error)
            if handingOver {
                handoverRetry?.invalidate()
                handoverRetry = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: false) { [weak self] _ in self?.refresh() }
            }
            log(status)
            setup.update(screen: permissions.screen, access: permissions.access, checking: false, running: false, detectedIP: bridgeAddress(), status: status, detail: permissions.detail)
        }
    }
    private func stopServer() {
        handoverRetry?.invalidate(); handoverRetry = nil
        clipboard.stop(); cursor.stop()
        setup?.clipboardState.stringValue = ui("尚未同步 · 需要两端均开启并建立连接", "Inactive · both platforms must enable sync and connect")
        server?.stop(); server = nil; boundIP = nil; boundPort = nil
        capturedDisplay = nil; capturedBounds = nil; capturedDesktopBounds = []
        status = ui("服务已停止；可以修改连接设置后重新启动。", "Host stopped. Edit the connection settings and start again.")
    }
    private func loadPairing() {
        guard let setup else { return }
        let enabled = setup.usePairing.state == .on
        if !options.previewUI && !options.recoveryCheck { UserDefaults.standard.set(enabled, forKey: "requirePairing") }
        do { code = enabled ? (options.previewUI ? "0123456789abcdef0123456789abcdef" : try pairingCode(options.token)) : ""; statusIsError = false }
        catch { code = ""; statusIsError = true; status = ui("无法读取配对码：", "Unable to load pairing code: ") + String(describing: error) }
        setup.pairing.stringValue = code
        setup.pairing.placeholderString = ui("配对码验证已关闭", "Pairing disabled")
    }
    private func copyPairing() {
        NSPasteboard.general.clearContents(); NSPasteboard.general.setString(code, forType: .string)
    }
    private func relaunch() {
        guard !options.previewUI else { return }
        guard let setup, let selectedPort = UInt16(setup.port.stringValue), selectedPort > 0 else {
            status = ui("端口无效，请填写 1–65535 后再重启。", "Invalid port. Enter a value from 1 to 65535 before relaunching.")
            refresh(); return
        }
        UserDefaults.standard.set(setup.ip.stringValue.trimmingCharacters(in: .whitespacesAndNewlines), forKey: "hostIPv4")
        UserDefaults.standard.set(Int(selectedPort), forKey: "hostPort")
        let process = Process(); process.executableURL = Bundle.main.executableURL
        var arguments: [String] = [], skipValue = false
        for arg in CommandLine.arguments.dropFirst() {
            if skipValue { skipValue = false; continue }
            if arg == "--require-pairing" || arg == "--no-pairing" { continue }
            if arg == "--bind" || arg == "--port" { skipValue = true; continue }
            if arg != "--preview-ui" { arguments.append(arg) }
        }
        let ip = setup.ip.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        if !ip.isEmpty { arguments += ["--bind", ip] }
        arguments += ["--port", String(selectedPort), setup.usePairing.state == .on ? "--require-pairing" : "--no-pairing"]
        process.arguments = ["--relaunch-after-pid", String(getpid())] + arguments
        do { recovery.pause(); stopServer(); permissionHelper?.close(); permissionHelper = nil; try process.run(); NSApp.terminate(nil) }
        catch { status = ui("重启失败：", "Unable to relaunch: ") + error.localizedDescription; refresh() }
    }
    private func updateMenu() {
        let menu = NSMenu()
        let state = NSMenuItem(title: boundIP.map { "\($0):\(boundPort ?? options.port)" } ?? ui("等待设置", "Setup needed"), action: nil, keyEquivalent: "")
        state.isEnabled = false; menu.addItem(state)
        let settings = NSMenuItem(title: ui("权限与连接设置…", "Permissions & connection…"), action: #selector(showSettings), keyEquivalent: ",")
        settings.target = self; menu.addItem(settings)
        let pair = NSMenuItem(title: ui("复制配对码", "Copy pairing code"), action: #selector(copyCode), keyEquivalent: "")
        pair.target = self; pair.isEnabled = setup?.usePairing.state == .on; menu.addItem(pair)
        menu.addItem(.separator()); menu.addItem(NSMenuItem(title: ui("退出", "Quit"), action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        item?.menu = menu
    }
    @objc private func showSettings() { setup?.present(); refresh() }
    private func updateApplicationIcon() {
        updateStatusIcon()
        let dark = NSApp.effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .darkAqua
        guard darkIcon != dark else { return }
        let name = dark ? "ThunderDisplayDark" : "ThunderDisplay"
        guard let url = Bundle.main.url(forResource: name, withExtension: "icns"), let icon = NSImage(contentsOf: url) else { return }
        darkIcon = dark; NSApp.applicationIconImage = icon
        setup?.setApplicationIcon(icon)
    }
    private func updateStatusIcon() {
        guard let button = item?.button, button.image == nil else { return }
        let image = NSImage(size: NSSize(width: 22, height: 22))
        for scale in [1, 2, 3] {
            let suffix = scale == 1 ? "" : "@\(scale)x"
            guard let url = Bundle.main.url(forResource: "ThunderDisplayStatus" + suffix, withExtension: "png"),
                  let data = try? Data(contentsOf: url), let representation = NSBitmapImageRep(data: data) else { continue }
            representation.size = image.size
            image.addRepresentation(representation)
        }
        guard !image.representations.isEmpty else { return }
        image.isTemplate = true
        button.title = ""; button.image = image; button.toolTip = "ThunderDisplay"
    }
    @objc private func copyCode() { copyPairing() }
    func applicationWillTerminate(_ notification: Notification) {
        timer?.invalidate(); workspaceObservers.forEach { NSWorkspace.shared.notificationCenter.removeObserver($0) }; workspaceObservers.removeAll()
        startupRetry?.invalidate(); handoverRetry?.invalidate()
        power.update(enabled: false); clipboard.stop(); cursor.stop(); server?.stop()
    }
}

do {
    let options = try Options()
    // A system-installed Aqua agent starts only for the user who enabled it.
    // Other login sessions exit before any permissions, capture or UI are used.
    if let uid = options.startupUser, uid != geteuid() { exit(0) }
    if options.inputCheck {
        try checkNativeInput(useQuartz: options.inputCheckQuartz, keyboardOnly: options.inputCheckKeyboardOnly)
    } else if options.loginWindowFrameCheck {
        try loginWindowFrameCheck()
    } else if options.loginWindowCheck {
        print("Pre-login executable marker: \(LoginWindowInput.executableHasMarker)")
        print("LoginWindow diagnostic: uid=\(geteuid()), manager=\(ConsoleSession.managerName), securityGraphics=\(ConsoleSession.graphicsAvailable), consoleLoggedIn=\(ConsoleSession.loggedIn), preLogin=\(ConsoleSession.preLogin), ordinaryScreen=\(CGPreflightScreenCaptureAccess()), eventPost=\(CGPreflightPostEventAccess())")
        if let state = LoginWindowState.read() { print("LoginWindow agent (live): \(state.phase.rawValue); \(state.detail)") }
        // The pre-login heartbeat is written before login, so it is usually stale or
        // belongs to a previous boot by the time this runs; report it either way,
        // with the phase and reason the agent was leaving when it stopped.
        if let last = LoginWindowState.readLastRecorded() {
            print("LoginWindow agent (last recorded): \(last.phase.rawValue); \(last.detail); captureChecked=\(last.captureChecked), inputChecked=\(last.inputChecked), lastFailure=\(last.lastFailure ?? "none"), stoppedFrom=\(last.previousPhase?.rawValue ?? "none"); \(last.previousDetail ?? "none"), recordedAt=\(last.recordedAt.map { ISO8601DateFormatter().string(from: $0) } ?? "unknown")")
        } else {
            print("LoginWindow agent (last recorded): no readable heartbeat")
        }
    } else if options.loginWindow {
        let manager = ConsoleSession.managerName
        guard geteuid() == 0, manager == "LoginWindow" else {
            let reason = "Pre-login host launched in wrong context: " + manager
            if geteuid() == 0 { try? LoginWindowState(pid: getpid(), uptime: ProcessInfo.processInfo.systemUptime, phase: .blocked, detail: reason, lastFailure: reason).write() }
            throw HostError("Pre-login host must be launched by its LoginWindow LaunchAgent")
        }
        if ConsoleSession.loggedIn { exit(0) }
        let app = NSApplication.shared; app.setActivationPolicy(.prohibited)
        let delegate = LoginWindowHost(options: options); app.delegate = delegate
        withExtendedLifetime(delegate) { app.run() }
    } else if let parent = options.relaunchParent {
        try relaunchAfterExit(parent)
    } else if options.cursorCheck {
        _ = NSApplication.shared
        print(NativeCursorMonitor().diagnostic())
    } else if options.installerConfig {
        let encoder = PropertyListEncoder(); encoder.outputFormat = .xml
        FileHandle.standardOutput.write(try encoder.encode(StartupServices().installationConfiguration()))
    } else if options.startupStatus || options.repairStartup || options.registerLogin || options.unregisterLogin {
        let startup = StartupServices()
        if options.registerLogin { try startup.setLogin(true) }
        if options.unregisterLogin { try startup.setLogin(false) }
        if options.repairStartup { try startup.repairConfiguredStartup() }
        print("Login startup: \(startup.status(startup.login))")
        print("Boot service: \(startup.bootStatus)")
        print("LoginWindow: \(startup.loginWindowStatus)")
        print("Desktop startup component: \(startup.desktopStartupInstalled ? "installed" : "missing")")
    } else if options.permissionProbe {
        let result = FreshPermissions(screen: CGPreflightScreenCaptureAccess(), access: CGPreflightPostEventAccess(), axTrusted: AXIsProcessTrusted())
        let data = try JSONEncoder().encode(result)
        FileHandle.standardOutput.write(data)
    } else if options.captureRateCheck {
        try captureRateCheck(displayID: options.display, raw: options.rawCaptureRateCheck, updates: options.updateCaptureRateCheck,
            depth: options.captureDepthFive ? 5 : 3, unthrottled: options.unthrottledCaptureCheck)
    } else if options.rawTransportCheck {
        try rawTransportCheck()
    } else if options.captureCheck {
        try captureCheck(displayID: options.display, tenBit: options.captureCheck10, nativePixels: options.captureCheckNative, cursorVisible: !options.captureCheckLocalCursor, desktopSRGB: options.desktopColorCheck)
    } else if options.encoderCheck {
        try encoderCheck(bitrate: options.encoderCheckBitrate, desktopSRGB: options.desktopColorCheck)
    } else if options.diagnose {
        print("Screen recording: \(CGPreflightScreenCaptureAccess() ? "granted" : "not granted")")
        print("Keyboard/mouse event posting: \(CGPreflightPostEventAccess() ? "granted" : "not granted")")
        print("AX accessibility client (diagnostic only): \(AXIsProcessTrusted() ? "granted" : "not granted")")
        print("Thunderbolt Bridge: \(bridgeAddress() ?? "no IPv4 address detected")")
        var displays = [CGDirectDisplayID](repeating: 0, count: 16), count: UInt32 = 0
        if CGGetActiveDisplayList(16, &displays, &count) == .success {
            for id in displays.prefix(Int(count)) { print("Display \(id): \(CGDisplayPixelsWide(id))×\(CGDisplayPixelsHigh(id)), coordinates \(CGDisplayBounds(id))") }
        }
        for display in hostDisplays() {
            print("Display \(display.id) \(display.name): logical \(display.logicalWidth)×\(display.logicalHeight), render pixels \(display.renderText), available maximum \(display.maximumText)")
        }
    } else {
        let lease: HostInstanceLease?
        if options.recoveryCheck { lease = nil } else { lease = try HostInstanceLease() }
        let app = NSApplication.shared; app.setActivationPolicy(.accessory)
        let delegate = AppDelegate(options: options); app.delegate = delegate
        // Keep AppKit alive to host permission dialogs and the menu bar.
        withExtendedLifetime((delegate, lease)) { app.run() }
    }
} catch HostLaunchError.alreadyRunning {
    NSRunningApplication.runningApplications(withBundleIdentifier: "dev.thunderdisplay.host")
        .first { $0.processIdentifier != getpid() }?.activate(options: [.activateIgnoringOtherApps])
} catch { fputs("\(error)\n", stderr); exit(1) }
