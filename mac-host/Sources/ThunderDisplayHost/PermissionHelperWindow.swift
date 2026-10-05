import AppKit
import HostState

private final class DraggableAppView: NSView, NSDraggingSource {
    private let appURL: URL
    private let icon: NSImage
    override var isFlipped: Bool { true }

    init(appURL: URL) {
        self.appURL = appURL
        icon = NSWorkspace.shared.icon(forFile: appURL.path)
        super.init(frame: .zero)
        setAccessibilityElement(true)
        setAccessibilityRole(.image)
        setAccessibilityLabel(ui("拖拽 ThunderDisplayHost.app 到权限列表", "Drag ThunderDisplayHost.app into the permission list"))
        setAccessibilityValue(appURL.path)
        toolTip = appURL.path
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }
    override func resetCursorRects() { addCursorRect(bounds, cursor: .openHand) }
    override func draw(_ dirtyRect: NSRect) {
        let card = NSBezierPath(roundedRect: bounds.insetBy(dx: 1, dy: 1), xRadius: 12, yRadius: 12)
        NSColor.controlBackgroundColor.setFill(); card.fill()
        NSColor.separatorColor.setStroke(); card.stroke()
        icon.draw(in: NSRect(x: (bounds.width - 64) / 2, y: 14, width: 64, height: 64),
                  from: .zero, operation: .sourceOver, fraction: 1, respectFlipped: true, hints: nil)
        let paragraph = NSMutableParagraphStyle(); paragraph.alignment = .center
        (appURL.lastPathComponent as NSString).draw(in: NSRect(x: 8, y: 84, width: bounds.width - 16, height: 24),
            withAttributes: [.font: NSFont.systemFont(ofSize: 14, weight: .semibold), .foregroundColor: NSColor.labelColor, .paragraphStyle: paragraph])
        (ui("按住图标拖进系统设置", "Drag this app into System Settings") as NSString).draw(
            in: NSRect(x: 8, y: 111, width: bounds.width - 16, height: 22),
            withAttributes: [.font: NSFont.systemFont(ofSize: 12), .foregroundColor: NSColor.secondaryLabelColor, .paragraphStyle: paragraph])
    }
    override func mouseDown(with event: NSEvent) { /* Drag begins only after pointer movement. */ }
    override func mouseDragged(with event: NSEvent) {
        // A real file URL lets System Settings add the current application bundle.
        let item = NSDraggingItem(pasteboardWriter: appURL as NSURL)
        let point = convert(event.locationInWindow, from: nil)
        item.setDraggingFrame(NSRect(x: point.x - 32, y: point.y - 32, width: 64, height: 64), contents: icon)
        let session = beginDraggingSession(with: [item], event: event, source: self)
        session.animatesToStartingPositionsOnCancelOrFail = true
        log("Permission helper drag started: \(appURL.path)")
    }
    func draggingSession(_ session: NSDraggingSession, sourceOperationMaskFor context: NSDraggingContext) -> NSDragOperation { .copy }
    func ignoreModifierKeys(for session: NSDraggingSession) -> Bool { true }
}

final class PermissionHelperWindow: NSWindowController {
    private let status = NSTextField(wrappingLabelWithString: "")
    private let verify: NSButton
    private let restart: NSButton
    private var previousStatus: String?
    var onScreen: (() -> Void)?, onAccess: (() -> Void)?, onVerify: (() -> Void)?, onRestart: (() -> Void)?

    init() {
        let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 380, height: 540),
                            styleMask: [.titled, .closable, .nonactivatingPanel], backing: .buffered, defer: false)
        panel.title = ui("ThunderDisplay · 拖拽授权", "ThunderDisplay · Grant access")
        panel.isReleasedWhenClosed = false
        panel.isFloatingPanel = true; panel.level = .floating; panel.hidesOnDeactivate = false
        panel.collectionBehavior = [.moveToActiveSpace, .fullScreenAuxiliary]
        verify = NSButton(title: ui("检查实际权限", "Verify access"), target: nil, action: nil)
        restart = NSButton(title: ui("授权完成，重启应用", "Relaunch after granting access"), target: nil, action: nil)
        super.init(window: panel)

        let stack = NSStackView(); stack.orientation = .vertical; stack.alignment = .leading; stack.spacing = 12
        stack.translatesAutoresizingMaskIntoConstraints = false; panel.contentView!.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: panel.contentView!.leadingAnchor, constant: 20),
            stack.trailingAnchor.constraint(equalTo: panel.contentView!.trailingAnchor, constant: -20),
            stack.topAnchor.constraint(equalTo: panel.contentView!.topAnchor, constant: 18)
        ])
        let intro = text(ui("把下面的应用拖进权限列表，再打开对应开关。已有条目却未生效时，先选旧条目并点“−”移除，再拖入当前应用。两项授权完成后，此窗口自动关闭。", "Drag this app into each permission list and enable its switch. If an old entry is enabled but ineffective, select it and remove it with − before adding this copy. This helper closes when both permissions are granted."))
        stack.addArrangedSubview(intro)
        let drag = DraggableAppView(appURL: Bundle.main.bundleURL.standardizedFileURL)
        drag.widthAnchor.constraint(equalToConstant: 340).isActive = true
        drag.heightAnchor.constraint(equalToConstant: 142).isActive = true
        stack.addArrangedSubview(drag)
        let path = text(Bundle.main.bundlePath); path.font = .systemFont(ofSize: 10); path.textColor = .secondaryLabelColor
        path.isSelectable = true; stack.addArrangedSubview(path)
        let screen = NSButton(title: ui("1 · 屏幕录制设置", "1 · Screen Recording"), target: self, action: #selector(openScreen))
        let access = NSButton(title: ui("2 · 辅助功能设置", "2 · Accessibility"), target: self, action: #selector(openAccess))
        for button in [screen, access, verify, restart] { button.bezelStyle = .rounded }
        let row = NSStackView(views: [screen, access]); row.orientation = .horizontal; row.spacing = 8
        stack.addArrangedSubview(row)
        status.font = .systemFont(ofSize: 12); status.widthAnchor.constraint(equalToConstant: 340).isActive = true
        status.heightAnchor.constraint(equalToConstant: 40).isActive = true
        stack.addArrangedSubview(status)
        verify.target = self; verify.action = #selector(check)
        restart.target = self; restart.action = #selector(relaunch)
        stack.addArrangedSubview(verify); stack.addArrangedSubview(restart)
        stack.addArrangedSubview(text(ui("完成两项授权后再重启一次；不需要逐项重开应用。", "Grant both permissions before relaunching once.")))
        panel.center()
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    private func text(_ value: String) -> NSTextField {
        let field = NSTextField(wrappingLabelWithString: value)
        field.widthAnchor.constraint(equalToConstant: 340).isActive = true
        field.preferredMaxLayoutWidth = 340
        return field
    }
    func update(screen: PermissionState, access: PermissionState, checking: Bool, preview: Bool) {
        func label(_ state: PermissionState) -> String {
            switch state {
            case .ready: return ui("可用", "Available")
            case .notEffective: return ui("当前未生效", "Not effective")
            case .relaunchRequired: return ui("已授权，需重启", "Relaunch needed")
            case .checkFailed: return ui("检查失败", "Check failed")
            }
        }
        let message = ui("屏幕录制：", "Screen: ") + label(screen) + "\n" + ui("辅助功能：", "Accessibility: ") + label(access)
        if message != previousStatus { status.stringValue = message; previousStatus = message }
        verify.isEnabled = !checking && !preview; restart.isEnabled = !checking && !preview
    }
    func present() {
        guard let window else { return }
        if !window.isVisible {
            let visible = (NSScreen.main ?? NSScreen.screens.first)?.visibleFrame ?? .zero
            window.setFrameOrigin(NSPoint(x: max(visible.minX, visible.maxX - window.frame.width - 20),
                                          y: max(visible.minY, visible.maxY - window.frame.height - 32)))
        }
        window.makeKeyAndOrderFront(nil)
    }
    @objc private func openScreen() { onScreen?() }
    @objc private func openAccess() { onAccess?() }
    @objc private func check() { onVerify?() }
    @objc private func relaunch() { onRestart?() }
}
