import AppKit
import ServiceManagement
import HostState

final class StartupServices {
    let login = SMAppService.mainApp
    private let legacyAgent = SMAppService.agent(plistName: "dev.thunderdisplay.host.agent.plist")
    // Keep this identity separate from the retired SMAppService daemon. Its
    // persisted registration can be resubmitted by smd during the next boot.
    private let bootLabel = "dev.thunderdisplay.boot.system"
    private let bootPlist = "/Library/LaunchDaemons/dev.thunderdisplay.boot.system.plist"
    private var checkedAt = Date.distantPast
    private var cachedBootState = LaunchServiceState.unavailable
    var loginEnabled: Bool { login.status == .enabled }
    var administratorBootInstalled: Bool { FileManager.default.fileExists(atPath: bootPlist) }
    private var legacyBootInstalled: Bool { FileManager.default.fileExists(atPath: "/Library/LaunchDaemons/dev.thunderdisplay.boot.plist") }
    var bootConfigured: Bool { administratorBootInstalled || legacyBootInstalled }
    var loginWindowInstalled: Bool { FileManager.default.fileExists(atPath: "/Library/LaunchAgents/dev.thunderdisplay.loginwindow.plist") }
    var desktopStartupInstalled: Bool { FileManager.default.fileExists(atPath: "/Library/LaunchAgents/dev.thunderdisplay.desktop.plist") }
    var loginWindowStatus: String {
        guard loginWindowInstalled else { return ui("登录界面组件未安装；请更新开机组件", "LoginWindow component missing; update startup components") }
        if ConsoleSession.loggedIn {
            // The heartbeat describing the pre-login run may have been written during
            // the previous boot, and `uptime` is monotonic within one boot only, so a
            // freshness-checked read rejects it after a restart. Without the
            // age-independent read a failed pre-login start can never be explained.
            guard let previous = LoginWindowState.readLastRecorded() else { return ui("组件已安装，等待登录前会话实测", "Component installed; pre-login verification pending") }
            if let failure = previous.lastFailure, !failure.isEmpty { return ui("最近登录前检查失败：", "Recent pre-login check failed: ") + failure }
            if previous.captureChecked && previous.inputChecked { return ui("最近登录前画面与输入授权检查通过；远控仍需实测", "Recent pre-login frame / input authorization check passed; remote control still needs verification") }
            if let blocker = loginWindowBlocker(previous) { return ui("最近登录前未启动采集：", "Recent pre-login run did not start capture: ") + blocker }
            return ui("组件已安装，等待登录前会话实测", "Component installed; pre-login verification pending")
        }
        guard let state = LoginWindowState.read() else {
            guard let last = LoginWindowState.readLastRecorded(), let blocker = loginWindowBlocker(last) else { return ui("组件已安装但没有有效运行状态", "Installed; no valid runtime status") }
            return ui("组件已安装但没有有效运行状态；上次未启动采集：", "Installed; no live status. Last run did not start capture: ") + blocker
        }
        switch state.phase {
        case .checking: return ui("正在验证登录界面捕获", "Checking login-screen capture")
        case .claiming: return ui("捕获与输入通过检查，正在启动", "Capture / input checked; starting")
        case .listening: return ui("登录界面主机正在监听", "Login-screen host listening")
        case .blocked: return ui("登录界面主机不可用：", "Login-screen host unavailable: ") + state.detail
        case .stopped:
            guard let blocker = loginWindowBlocker(state) else { return ui("登录界面组件已停止", "LoginWindow component stopped") }
            return ui("登录界面组件已停止；最近未启动采集：", "LoginWindow component stopped; last run did not start capture: ") + blocker
        }
    }
    /// Why a login-window run that never reached `listening` did not start. Returns
    /// nil once capture and input both checked out. The agent records the phase and
    /// reason it was leaving, so a handover that published `stopped` no longer hides
    /// the blocker behind a fixed "agent stopped" message.
    private func loginWindowBlocker(_ state: LoginWindowState) -> String? {
        if let failure = state.lastFailure, !failure.isEmpty { return failure }
        if state.captureChecked && state.inputChecked { return nil }
        if let stopping = state.previousDetail, !stopping.isEmpty { return stopping }
        if state.phase == .blocked, !state.detail.isEmpty { return state.detail }
        return nil
    }
    var bootState: LaunchServiceState {
        if Date().timeIntervalSince(checkedAt) >= 5 {
            checkedAt = Date()
            let process = Process(), output = Pipe()
            process.executableURL = URL(fileURLWithPath: "/bin/launchctl")
            process.arguments = ["print", "system/" + bootLabel]
            process.standardOutput = output; process.standardError = FileHandle.nullDevice
            do {
                try process.run()
                let data = output.fileHandleForReading.readDataToEndOfFile(); process.waitUntilExit()
                cachedBootState = .parseInstalledDaemon(String(decoding: data, as: UTF8.self), succeeded: process.terminationStatus == 0, plistPath: bootPlist)
            } catch { cachedBootState = .unavailable }
        }
        return cachedBootState
    }
    var bootStatus: String {
        if legacyBootInstalled && !administratorBootInstalled {
            return ui("旧开机配置会在重启后失效；请重新启用以迁移", "Legacy boot configuration fails after restart; enable again to migrate")
        }
        switch bootState {
        case .running: return ui("系统守护进程运行中", "System daemon running")
        case .failed(let code): return ui("启动失败，退出码 ", "Startup failed, exit code ") + String(code) + ui("；请重新启用以修复", "; enable again to repair")
        case .stopped: return ui("已配置但未运行；请重新启用以修复", "Configured but not running; enable again to repair")
        case .unavailable: return bootConfigured ? ui("已配置但未加载；请重新启用以修复", "Configured but not loaded; enable again to repair") : ui("未启用", "Disabled")
        }
    }
    func setLogin(_ enabled: Bool) throws {
        if enabled {
            if login.status != .enabled && login.status != .requiresApproval { try login.register() }
        } else if login.status != .notRegistered && login.status != .notFound { try login.unregister() }
        // Retire the broken bundle-relative LaunchAgent, avoiding duplicate starters.
        if legacyAgent.status != .notRegistered && legacyAgent.status != .notFound { try legacyAgent.unregister() }
        UserDefaults.standard.set(enabled, forKey: "loginStartupRequested")
    }
    func migrateLoginIfNeeded() throws {
        guard !UserDefaults.standard.bool(forKey: "nativeLoginMigrationDone") else { return }
        if legacyAgent.status == .enabled || legacyAgent.status == .requiresApproval {
            try setLogin(true)
        }
        UserDefaults.standard.set(true, forKey: "nativeLoginMigrationDone")
    }
    func repairConfiguredStartup() throws {
        try setLogin(true)
        if bootConfigured { try setBoot(true) }
    }
    func setBoot(_ enabled: Bool) throws {
        if enabled {
            try setLogin(true)
            // Local bundles cannot safely run a nobody helper inside a private home.
            // Use the reviewed installer with an absolute, root-owned helper path.
            try administratorInstall(uninstall: false)
        } else {
            if bootConfigured { try administratorInstall(uninstall: true) }
        }
        checkedAt = .distantPast
        if enabled, bootState != .running || !loginWindowInstalled || !desktopStartupInstalled { throw HostError(ui("系统、登录界面或桌面启动组件安装未完成；请检查系统后台项目权限。", "System / LoginWindow / desktop component installation incomplete; check background item permissions.")) }
    }
    private func administratorInstall(uninstall: Bool) throws {
        guard let script = Bundle.main.url(forResource: "install-boot-service", withExtension: "sh") else { throw HostError(ui("开机服务安装组件缺失", "Boot installer is missing")) }
        func shellQuote(_ value: String) -> String { "'" + value.replacingOccurrences(of: "'", with: "'\"'\"'") + "'" }
        let command = "/bin/bash " + shellQuote(script.path) + " " + (uninstall ? "--uninstall" : shellQuote(Bundle.main.bundlePath))
        var arguments = command
        let temporary = FileManager.default.temporaryDirectory.appendingPathComponent("ThunderDisplay-startup-" + UUID().uuidString, isDirectory: true)
        defer { try? FileManager.default.removeItem(at: temporary) }
        if !uninstall {
            let port = UserDefaults.standard.object(forKey: "hostPort") as? Int ?? 47990
            guard let selectedPort = UInt16(exactly: port), selectedPort > 0 else { throw HostError("Invalid startup port") }
            let pairing = UserDefaults.standard.bool(forKey: "requirePairing")
            let config = LoginWindowConfiguration(port: selectedPort, requirePairing: pairing, token: pairing ? try pairingCode(nil) : nil, desktopUID: geteuid())
            try FileManager.default.createDirectory(at: temporary, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
            let file = temporary.appendingPathComponent("config.plist")
            let encoder = PropertyListEncoder(); encoder.outputFormat = .xml
            try encoder.encode(config).write(to: file)
            try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: file.path)
            arguments += " " + shellQuote(file.path)
        }
        let escaped = arguments.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        guard let appleScript = NSAppleScript(source: "do shell script \"" + escaped + "\" with administrator privileges") else { throw HostError("Cannot create administrator installer") }
        var error: NSDictionary?
        _ = appleScript.executeAndReturnError(&error)
        if let error { throw HostError(error[NSAppleScript.errorMessage] as? String ?? ui("开机服务安装被取消", "Boot service installation cancelled")) }
    }
    func status(_ service: SMAppService) -> String {
        switch service.status {
        case .enabled: return ui("已注册到“登录时打开”", "Registered to Open at Login")
        case .requiresApproval: return ui("等待系统批准，尚未启用", "Awaiting system approval; not enabled")
        case .notRegistered: return ui("未启用", "Disabled")
        case .notFound:
            return ui("系统未找到应用，请重新启用登录项", "App not found by the system; enable the login item again")
        @unknown default: return ui("状态未知", "Unknown status")
        }
    }
    func openSettings() { SMAppService.openSystemSettingsLoginItems() }
}
