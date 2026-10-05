import AppKit
import ScreenCaptureKit
import Security

// Isolated local-signing experiment. No network, input injection, login item,
// file capture, or changes to the production application's permission state.
func entitlementClaim() -> Bool {
    guard let task = SecTaskCreateFromSelf(nil) else { return false }
    return (SecTaskCopyValueForEntitlement(task, "com.apple.developer.persistent-content-capture" as CFString, nil) as? Bool) == true
}
func report(_ message: String) {
    print(message); fflush(stdout)
}
if CommandLine.arguments.contains("--preflight") {
    report("PROCESS_STARTED bundle=\(Bundle.main.bundleIdentifier ?? "unknown") entitlementClaim=\(entitlementClaim()) ordinaryScreenPreflight=\(CGPreflightScreenCaptureAccess())")
    exit(0)
}

final class Probe: NSObject, NSApplicationDelegate, SCStreamOutput {
    private var window: NSWindow!, status: NSTextField!, button: NSButton!
    private var stream: SCStream?, running = false, received = false
    private var timeoutTask: Task<Void, Never>?
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 265), styleMask: [.titled, .closable], backing: .buffered, defer: false)
        window.title = "ThunderDisplay · 本地权限测试"
        window.isReleasedWhenClosed = false
        let stack = NSStackView(); stack.orientation = .vertical; stack.alignment = .leading; stack.spacing = 16
        stack.translatesAutoresizingMaskIntoConstraints = false
        let title = NSTextField(labelWithString: "测试“远程桌面”权限")
        title.font = .systemFont(ofSize: 22, weight: .semibold)
        stack.addArrangedSubview(title)
        let text = NSTextField(wrappingLabelWithString: "这是独立测试 App。检查本地签名是否能申请持久屏幕捕获；仅验证能否收到一帧，不保存画面，不提供网络访问。系统权限由你批准。")
        text.preferredMaxLayoutWidth = 470; stack.addArrangedSubview(text)
        status = NSTextField(wrappingLabelWithString: "尚未测试"); status.preferredMaxLayoutWidth = 470; stack.addArrangedSubview(status)
        button = NSButton(title: "测试权限", target: self, action: #selector(requestCapture)); stack.addArrangedSubview(button)
        window.contentView!.addSubview(stack)
        NSLayoutConstraint.activate([stack.leadingAnchor.constraint(equalTo: window.contentView!.leadingAnchor, constant: 24),
            stack.trailingAnchor.constraint(equalTo: window.contentView!.trailingAnchor, constant: -24),
            stack.topAnchor.constraint(equalTo: window.contentView!.topAnchor, constant: 24)])
        window.center(); window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps: true)
        report("PROCESS_STARTED entitlementClaim=\(entitlementClaim()) ordinaryScreenPreflight=\(CGPreflightScreenCaptureAccess())")
        if CommandLine.arguments.contains("--request") { DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { self.requestCapture() } }
    }
    @objc private func requestCapture() {
        guard !running else { return }; running = true; received = false; button.isEnabled = false
        status.stringValue = "正在请求系统授权…"
        report("CAPTURE_REQUEST entitlementClaim=\(entitlementClaim()) ordinaryScreenPreflight=\(CGPreflightScreenCaptureAccess())")
        Task { @MainActor in
            do {
                let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
                guard let display = content.displays.first else { throw NSError(domain: "Probe", code: 1, userInfo: [NSLocalizedDescriptionKey: "未找到显示器"]) }
                let config = SCStreamConfiguration(); config.width = 64; config.height = 64
                config.showsCursor = false; config.capturesAudio = false
                config.minimumFrameInterval = CMTime(value: 1, timescale: 30); config.queueDepth = 3
                let capture = SCStream(filter: SCContentFilter(display: display, excludingWindows: []), configuration: config, delegate: nil)
                try capture.addStreamOutput(self, type: .screen, sampleHandlerQueue: .main)
                stream = capture; try await capture.startCapture()
                timeoutTask = Task { @MainActor in
                    do { try await Task.sleep(nanoseconds: 5_000_000_000) }
                    catch { return }
                    if !received { finish("未收到有效画面；不能判定授权成功") }
                }
            } catch { finish("申请 / 捕获失败：" + error.localizedDescription) }
        }
    }
    func stream(_ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer, of type: SCStreamOutputType) {
        guard !received, type == .screen, CMSampleBufferIsValid(sampleBuffer),
              let attachment = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
              let state = attachment.first?[.status] as? Int, state == SCFrameStatus.complete.rawValue,
              CMSampleBufferGetImageBuffer(sampleBuffer) != nil else { return }
        received = true
        finish("已收到有效帧；普通屏幕录制检测：\(CGPreflightScreenCaptureAccess())。还需核对系统将授权列在哪一项。")
    }
    private func finish(_ message: String) {
        timeoutTask?.cancel(); timeoutTask = nil
        report("CAPTURE_RESULT \(message)"); status.stringValue = message
        let capture = stream; stream = nil
        Task { @MainActor in try? await capture?.stopCapture(); running = false; button.isEnabled = true }
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
let application = NSApplication.shared
let delegate = Probe(); application.delegate = delegate; application.run()
