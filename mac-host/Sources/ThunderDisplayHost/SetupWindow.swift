import AppKit
import HostState

private let chineseUI = Locale.preferredLanguages.first?.lowercased().hasPrefix("zh") ?? false
func ui(_ chinese: String, _ english: String) -> String { chineseUI ? chinese : english }
private let ink = NSColor.labelColor
private let accent = NSColor.controlAccentColor
private final class DocumentView: NSView { override var isFlipped: Bool { true } }
private final class AppearanceCard: NSStackView {
    override var wantsUpdateLayer: Bool { true }
    override func viewDidChangeEffectiveAppearance() { super.viewDidChangeEffectiveAppearance(); needsDisplay = true }
    override func updateLayer() {
        effectiveAppearance.performAsCurrentDrawingAppearance {
            layer?.backgroundColor = NSColor.controlBackgroundColor.cgColor
            layer?.borderColor = NSColor.separatorColor.cgColor
        }
    }
}

final class PermissionBadge: NSView {
    private let text = NSTextField(labelWithString: "")
    var stringValue: String { get { text.stringValue } set { text.stringValue = newValue } }
    var textColor: NSColor? { get { text.textColor } set { text.textColor = newValue; needsDisplay = true } }
    override var wantsUpdateLayer: Bool { true }
    override func viewDidChangeEffectiveAppearance() { super.viewDidChangeEffectiveAppearance(); needsDisplay = true }
    override func updateLayer() {
        effectiveAppearance.performAsCurrentDrawingAppearance {
            layer?.backgroundColor = text.textColor?.withAlphaComponent(0.12).cgColor
        }
    }
    init() {
        super.init(frame: .zero); wantsLayer = true; layer?.cornerRadius = 17
        text.font = .systemFont(ofSize: 12, weight: .semibold); text.alignment = .center
        text.translatesAutoresizingMaskIntoConstraints = false; addSubview(text)
        NSLayoutConstraint.activate([widthAnchor.constraint(equalToConstant: 148), heightAnchor.constraint(equalToConstant: 34),
            text.centerXAnchor.constraint(equalTo: centerXAnchor), text.centerYAnchor.constraint(equalTo: centerYAnchor),
            text.leadingAnchor.constraint(greaterThanOrEqualTo: leadingAnchor, constant: 12),
            text.trailingAnchor.constraint(lessThanOrEqualTo: trailingAnchor, constant: -12)])
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
}

final class SetupWindow: NSWindowController, NSWindowDelegate {
    private struct Presentation: Equatable {
        let screen: PermissionState, access: PermissionState
        let checking: Bool, running: Bool, detectedIP: String?, status: String, detail: String
        let paired: Bool, hasCode: Bool, hasIP: Bool
    }
    private var lastPresentation: Presentation?
    private let contentDocument = DocumentView(), scroll = NSScrollView(), root = NSStackView()
    private var wrapFields: [NSTextField] = []
    private var displays: [HostDisplay] = [], currentDisplay: CGDirectDisplayID?
    private var preferredDisplay: CGDirectDisplayID?
    private var observing: NSObjectProtocol?
    let displayPicker = NSPopUpButton(), displayCurrent = NSTextField(labelWithString: ""), displayRender = NSTextField(labelWithString: "")
    let displayMaximum = NSTextField(wrappingLabelWithString: "")
    let displayColor = NSTextField(wrappingLabelWithString: "")
    private let detect = NSButton(title: ui("重新检测", "Refresh"), target: nil, action: nil)
    let screenState = PermissionBadge(), accessState = PermissionBadge()
    let permissionDetail = NSTextField(wrappingLabelWithString: "")
    let allowClipboard = NSButton(checkboxWithTitle: ui("允许同步文字和图片剪贴板", "Allow text and image clipboard"), target: nil, action: nil)
    let clipboardState = NSTextField(wrappingLabelWithString: "")
    var onClipboard: (() -> Void)?
    let launchAtLogin = NSButton(checkboxWithTitle: ui("登录时打开 ThunderDisplay", "Open ThunderDisplay at login"), target: nil, action: nil)
    let launchAtBoot = NSButton(checkboxWithTitle: ui("随系统启动（含登录界面组件）", "Start with system (includes LoginWindow component)"), target: nil, action: nil)
    let startupState = NSTextField(wrappingLabelWithString: "")
    let keepAwake = NSButton(checkboxWithTitle: ui("保持 Mac 可连接（防止系统自动睡眠）", "Keep Mac reachable (prevent automatic system sleep)"), target: nil, action: nil)
    let powerState = NSTextField(wrappingLabelWithString: "")
    var onKeepAwake: (() -> Void)?
    var onLoginStartup: (() -> Void)?, onBootStartup: (() -> Void)?, onStartupSettings: (() -> Void)?
    var onRepairStartup: (() -> Void)?
    let usePairing = NSButton(checkboxWithTitle: ui("启用配对码验证", "Require a pairing code"), target: nil, action: nil)
    private var copyPairing: NSButton!, pairingRow: NSStackView!
    let networkState = NSTextField(wrappingLabelWithString: ""), serviceState = NSTextField(wrappingLabelWithString: "")
    let ip = NSTextField(), port = NSTextField(), pairing = NSSecureTextField()
    let authorize = NSButton(title: ui("一键申请权限", "Grant permissions"), target: nil, action: nil)
    let start = NSButton(title: ui("启动主机", "Start host"), target: nil, action: nil)
    let stop = NSButton(title: ui("停止服务", "Stop host"), target: nil, action: nil)
    let restart = NSButton(title: ui("重启应用", "Relaunch app"), target: nil, action: nil)
    var onAuthorize: (() -> Void)?, onScreen: (() -> Void)?, onAccess: (() -> Void)?
    var onPairing: (() -> Void)?, onLocate: (() -> Void)?, onDisplay: (() -> Void)?
    var onRefresh: (() -> Void)?, onStart: (() -> Void)?, onStop: (() -> Void)?, onRestart: (() -> Void)?, onCopy: (() -> Void)?
    var selectedDisplay: HostDisplay? { displays.first { $0.id == currentDisplay } }

    init() {
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 740, height: 880),
                              styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.title = ui("ThunderDisplay · Mac 主机", "ThunderDisplay · Mac Host")
        window.isReleasedWhenClosed = false; window.minSize = NSSize(width: 660, height: 500)
        window.appearance = nil
        window.backgroundColor = .windowBackgroundColor
        window.titlebarAppearsTransparent = true
        super.init(window: window); window.delegate = self
        root.orientation = .vertical; root.alignment = .leading; root.spacing = 18
        root.translatesAutoresizingMaskIntoConstraints = false
        scroll.frame = window.contentView!.bounds; scroll.autoresizingMask = [.width, .height]
        scroll.hasVerticalScroller = true; scroll.autohidesScrollers = true; scroll.drawsBackground = false
        scroll.documentView = contentDocument; window.contentView!.addSubview(scroll); contentDocument.addSubview(root)
        NSLayoutConstraint.activate([
            root.leadingAnchor.constraint(equalTo: contentDocument.leadingAnchor, constant: 24),
            root.trailingAnchor.constraint(equalTo: contentDocument.trailingAnchor, constant: -24),
            root.topAnchor.constraint(equalTo: contentDocument.topAnchor, constant: 24)
        ])
        let appIcon = Bundle.main.url(forResource: "ThunderDisplay", withExtension: "icns").flatMap { NSImage(contentsOf: $0) }
        let icon = NSImageView(image: appIcon ?? NSImage(systemSymbolName: "display.2", accessibilityDescription: "ThunderDisplay") ?? NSImage())
        if appIcon == nil { icon.contentTintColor = accent }
        icon.imageScaling = .scaleProportionallyUpOrDown
        icon.widthAnchor.constraint(equalToConstant: 42).isActive = true; icon.heightAnchor.constraint(equalToConstant: 42).isActive = true
        let titles = vertical([label("ThunderDisplay", size: 28, weight: .bold), label(ui("MAC 主机 · 雷雳直连", "MAC HOST · THUNDERBOLT"), size: 11, weight: .medium)])
        let header = row([icon, titles, spacer(), label(Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.6.0", size: 12, weight: .semibold)])
        add(header)
        add(wrapped(ui("把 Mac 桌面带到你的 Windows 屏幕。清晰、流畅，本地直连。", "Bring your Mac desktop to Windows. Sharp, fluid, directly connected."), card: false))

        let permission = card(ui("访问权限", "Access permissions"), symbol: "lock.shield")
        permission.addArrangedSubview(permissionRow(ui("屏幕录制", "Screen recording"), detail: ui("传输 Mac 桌面", "Share the Mac desktop"), state: screenState, action: #selector(screen)))
        permission.addArrangedSubview(permissionRow(ui("键盘与鼠标控制", "Keyboard and mouse"), detail: ui("在辅助功能中允许控制", "Allow control in Accessibility"), state: accessState, action: #selector(access)))
        configure(authorize, #selector(authorizeAll), primary: true); configure(restart, #selector(relaunch))
        permission.addArrangedSubview(row([authorize, button(ui("检查授权", "Verify access"), #selector(refresh)), restart, spacer()]))
        prepareWrapped(permissionDetail, in: permission); permission.addArrangedSubview(permissionDetail)
        permission.addArrangedSubview(row([button(ui("在 Finder 显示应用", "Reveal app in Finder"), #selector(locate)), spacer()]))
        add(permission)

        let display = card(ui("Mac 显示器", "Mac display"), symbol: "display")
        displayPicker.target = self; displayPicker.action = #selector(displayChanged)
        displayPicker.setContentHuggingPriority(.defaultLow, for: .horizontal)
        configure(detect, #selector(detectDisplays))
        display.addArrangedSubview(row([displayPicker, detect]))
        displayCurrent.font = .monospacedDigitSystemFont(ofSize: 16, weight: .semibold); displayCurrent.textColor = ink
        displayRender.font = .monospacedDigitSystemFont(ofSize: 16, weight: .semibold); displayRender.textColor = ink
        display.addArrangedSubview(row([metric(ui("桌面逻辑尺寸", "DESKTOP LOGICAL SIZE"), value: displayCurrent), metric(ui("捕获源像素（HiDPI）", "SOURCE RENDER PIXELS"), value: displayRender)]))
        prepareWrapped(displayMaximum, in: display); display.addArrangedSubview(displayMaximum)
        prepareWrapped(displayColor, in: display); display.addArrangedSubview(displayColor)
        display.addArrangedSubview(wrapped(ui("HiDPI 下逻辑尺寸与渲染像素不同。清晰度优先保留 Mac 渲染像素，由 Windows 高质量缩放。新版两端采用 sRGB SDR · 8 / 10-bit · 4:2:0，旧端使用兼容色彩。实际参数见连接状态。", "HiDPI logical size differs from render pixels. Best-detail mode preserves Mac render pixels for Windows scaling. Updated platforms use sRGB SDR · 8 / 10-bit · 4:2:0; older clients use legacy color. Connection status shows actual parameters."), in: display))
        add(display)

        let network = card(ui("雷雳连接", "Thunderbolt connection"), symbol: "bolt.horizontal.circle")
        ip.placeholderString = ui("自动检测网桥 IPv4", "Auto-detect bridge IPv4"); port.stringValue = "47990"
        for field in [ip, port, pairing] { field.bezelStyle = .roundedBezel; field.font = .systemFont(ofSize: 14); field.heightAnchor.constraint(equalToConstant: 32).isActive = true }
        ip.setContentHuggingPriority(.defaultLow, for: .horizontal); port.widthAnchor.constraint(equalToConstant: 90).isActive = true
        network.addArrangedSubview(row([label(ui("本机 IPv4", "Local IPv4")), ip, label(ui("端口", "Port")), port]))
        prepareWrapped(networkState, in: network); network.addArrangedSubview(networkState)
        usePairing.target = self; usePairing.action = #selector(pairingChanged)
        network.addArrangedSubview(row([usePairing, spacer(), label(ui("直连默认无需配对码", "Optional on a direct link"), size: 12)]))
        pairing.isEditable = false; pairing.isSelectable = true; pairing.setContentHuggingPriority(.defaultLow, for: .horizontal)
        copyPairing = button(ui("复制", "Copy"), #selector(copyCode)); pairingRow = row([pairing, copyPairing]); network.addArrangedSubview(pairingRow)
        configure(start, #selector(startHost), primary: true); configure(stop, #selector(stopHost))
        network.addArrangedSubview(row([start, stop, spacer()]))
        serviceState.font = .systemFont(ofSize: 13, weight: .medium)
        prepareWrapped(serviceState, in: network); network.addArrangedSubview(serviceState)
        add(network)
        let clipboard = card(ui("文字与图片剪贴板", "Text & image clipboard"), symbol: "doc.on.clipboard")
        allowClipboard.target = self; allowClipboard.action = #selector(clipboardChanged)
        clipboard.addArrangedSubview(row([allowClipboard, spacer()]))
        prepareWrapped(clipboardState, in: clipboard); clipboard.addArrangedSubview(clipboardState)
        clipboard.addArrangedSubview(wrapped(ui("同步连接后新复制的文字（64 KiB）和图片（PNG，32 MiB）；不传文件。Windows 端也可以关闭。", "Sync new text (64 KiB) and PNG images (32 MiB) while connected. Files are excluded. Windows also has a switch."), in: clipboard))
        add(clipboard)
        let startup = card(ui("自启动与无人值守", "Startup & unattended access"), symbol: "power")
        launchAtLogin.target = self; launchAtLogin.action = #selector(loginStartupChanged)
        launchAtBoot.target = self; launchAtBoot.action = #selector(bootStartupChanged)
        startup.addArrangedSubview(launchAtLogin); startup.addArrangedSubview(launchAtBoot)
        keepAwake.target = self; keepAwake.action = #selector(keepAwakeChanged)
        startup.addArrangedSubview(keepAwake)
        prepareWrapped(powerState, in: startup); startup.addArrangedSubview(powerState)
        startup.addArrangedSubview(wrapped(ui("默认保持系统唤醒，屏幕仍可熄灭。雷雳不支持网络唤醒；手动让 Mac 睡眠后需先唤醒 Mac，主机与串流会自动恢复。", "The system stays awake by default; the display can still sleep. Thunderbolt does not support network wake. After manual sleep, wake the Mac first; the host and stream recover automatically."), in: startup))
        prepareWrapped(startupState, in: startup); startup.addArrangedSubview(startupState)
        startup.addArrangedSubview(button(ui("打开登录项与后台项目", "Open Login Items & background services"), #selector(startupSettings)))
        startup.addArrangedSubview(button(ui("更新开机组件与连接配置", "Update startup components & connection settings"), #selector(repairStartup)))
        startup.addArrangedSubview(wrapped(ui("系统守护进程负责发现；登录界面组件在独立图形会话验证实际画面与输入授权，通过后开放连接。登录后由桌面主机接管。更新组件需管理员确认；端口与配对设置在更新时同步。FileVault 开机解锁界面不支持。", "The system daemon handles discovery. The LoginWindow component verifies a real frame and input access before accepting control; the desktop host takes over after login. Updates need administrator authentication and copy port / pairing settings. FileVault preboot is unsupported."), in: startup))
        add(startup)
        add(wrapped(ui("仅用户桌面会话可用。显示器的 HDR / 广色域能力与当前串流格式分别展示；连接不会更改系统显示模式。", "Available in your desktop session. Display HDR / wide-color capabilities are shown separately from the stream format. Connecting keeps your system display mode."), card: false))
        refreshDisplays()
        observing = NotificationCenter.default.addObserver(forName: NSApplication.didChangeScreenParametersNotification, object: nil, queue: .main) { [weak self] _ in self?.refreshDisplays(); self?.onDisplay?() }
        window.center(); fitDocument()
    }
    required init?(coder: NSCoder) { fatalError("init(coder:) has not been implemented") }
    deinit { if let observing { NotificationCenter.default.removeObserver(observing) } }
    private func label(_ value: String, size: CGFloat = 13, weight: NSFont.Weight = .regular) -> NSTextField {
        let field = NSTextField(labelWithString: value); field.font = .systemFont(ofSize: size, weight: weight); field.textColor = ink; return field
    }
    private func vertical(_ views: [NSView]) -> NSStackView {
        let stack = NSStackView(views: views); stack.orientation = .vertical; stack.alignment = .leading; stack.spacing = 6; return stack
    }
    private func row(_ views: [NSView]) -> NSStackView {
        let stack = NSStackView(views: views); stack.orientation = .horizontal; stack.spacing = 12; stack.alignment = .centerY; return stack
    }
    private func spacer() -> NSView { let view = NSView(); view.setContentHuggingPriority(.defaultLow, for: .horizontal); return view }
    private func add(_ view: NSView) {
        root.addArrangedSubview(view); view.widthAnchor.constraint(equalTo: root.widthAnchor).isActive = true
        if let stack = view as? NSStackView, stack.layer?.cornerRadius == 18 {
            for child in stack.arrangedSubviews { child.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -40).isActive = true }
        }
    }
    private func card(_ title: String, symbol: String) -> NSStackView {
        let stack = AppearanceCard(); stack.orientation = .vertical; stack.alignment = .leading
        stack.spacing = 12; stack.edgeInsets = NSEdgeInsets(top: 18, left: 20, bottom: 18, right: 20)
        stack.wantsLayer = true; stack.layer?.cornerRadius = 18; stack.layer?.borderWidth = 1
        stack.needsDisplay = true
        let icon = NSImageView(image: NSImage(systemSymbolName: symbol, accessibilityDescription: title) ?? NSImage()); icon.contentTintColor = accent
        icon.widthAnchor.constraint(equalToConstant: 20).isActive = true; icon.heightAnchor.constraint(equalToConstant: 20).isActive = true
        let heading = row([icon, label(title, size: 15, weight: .semibold), spacer()]); stack.addArrangedSubview(heading)
        heading.widthAnchor.constraint(equalTo: stack.widthAnchor, constant: -40).isActive = true
        return stack
    }
    private func prepareWrapped(_ field: NSTextField, in stack: NSStackView?) {
        field.textColor = .secondaryLabelColor; field.font = .systemFont(ofSize: 12)
        wrapFields.append(field)
    }
    private func wrapped(_ value: String, in stack: NSStackView? = nil, card: Bool = true) -> NSTextField {
        let field = NSTextField(wrappingLabelWithString: value); prepareWrapped(field, in: stack)
        return field
    }
    private func metric(_ title: String, value: NSTextField) -> NSStackView {
        let stack = vertical([label(title, size: 10, weight: .medium), value]); stack.setContentHuggingPriority(.defaultLow, for: .horizontal)
        return stack
    }
    private func configure(_ button: NSButton, _ action: Selector, primary: Bool = false) {
        button.target = self; button.action = action; button.bezelStyle = .rounded; button.controlSize = .large
        if primary { button.bezelColor = accent; button.contentTintColor = .white }
    }
    private func button(_ title: String, _ action: Selector) -> NSButton {
        let button = NSButton(title: title, target: self, action: action); configure(button, action); return button
    }
    private func permissionRow(_ title: String, detail: String, state: PermissionBadge, action: Selector) -> NSStackView {
        let description = vertical([label(title, size: 13, weight: .semibold), label(detail, size: 11)])
        description.setContentHuggingPriority(.defaultLow, for: .horizontal)
        let stack = row([description, spacer(), state, button(ui("打开设置", "Open settings"), action)])
        return stack
    }
    func refreshDisplays(select id: CGDirectDisplayID? = nil) {
        if let id { preferredDisplay = id }
        if preferredDisplay == nil { preferredDisplay = currentDisplay ?? UserDefaults.standard.object(forKey: "captureDisplay") as? UInt32 ?? CGMainDisplayID() }
        let preferred = preferredDisplay
        displays = hostDisplays(); displayPicker.removeAllItems()
        for display in displays { displayPicker.addItem(withTitle: display.name); displayPicker.lastItem?.representedObject = display.id }
        currentDisplay = displays.first { $0.id == preferred }?.id ?? displays.first?.id
        if let index = displays.firstIndex(where: { $0.id == currentDisplay }) { displayPicker.selectItem(at: index) }
        updateDisplayLabels()
    }
    private func updateDisplayLabels() {
        displayCurrent.stringValue = selectedDisplay?.currentText ?? ui("无可用显示器", "No display")
        displayRender.stringValue = selectedDisplay?.renderText ?? "—"
        displayMaximum.stringValue = ui("系统最大可用模式（像素）：", "Largest available mode (pixels): ") + (selectedDisplay?.maximumText ?? "—")
        displayColor.stringValue = selectedDisplay?.colorText ?? ui("无法读取屏幕参数", "Display capabilities unavailable")
        fitDocument()
    }
    func update(screen: PermissionState, access: PermissionState, checking: Bool, running: Bool, detectedIP: String?, status: String, detail: String) {
        let presentation = Presentation(screen: screen, access: access, checking: checking, running: running,
            detectedIP: detectedIP, status: status, detail: detail, paired: usePairing.state == .on, hasCode: !pairing.stringValue.isEmpty, hasIP: !ip.stringValue.isEmpty)
        guard lastPresentation != presentation else { return }; lastPresentation = presentation
        for (field, state) in [(screenState, screen), (accessState, access)] {
            switch state {
            case .ready: field.stringValue = ui("已授权", "Allowed"); field.textColor = .systemGreen
            case .relaunchRequired: field.stringValue = ui("已授权，需重启", "Relaunch needed"); field.textColor = .systemOrange
            case .checkFailed: field.stringValue = ui("检查失败", "Check failed"); field.textColor = .systemRed
            case .notEffective: field.stringValue = checking ? ui("检查中…", "Checking…") : ui("待授权", "Access needed"); field.textColor = .systemOrange
            }
            field.needsDisplay = true
        }
        let ready = screen.usable && access.usable && !checking
        authorize.isEnabled = !ready && !checking; start.isEnabled = !running && ready; stop.isEnabled = running
        ip.isEnabled = !running; port.isEnabled = !running; usePairing.isEnabled = !running; displayPicker.isEnabled = !running
        let paired = usePairing.state == .on; pairingRow.isHidden = !paired
        pairing.isEnabled = paired; copyPairing.isEnabled = paired && !pairing.stringValue.isEmpty; restart.isEnabled = !ready
        var guidance = detail
        if screen == .relaunchRequired || access == .relaunchRequired {
            guidance = ui("权限已批准。完成两项授权后重启一次即可生效。", "Access granted. Finish both permissions, then relaunch once.") + "\n" + detail
        } else if !ready && !checking {
            guidance += ui("\n申请权限会自动打开应用拖拽助手。若已开启但未生效，请移除旧授权条目，再添加当前应用。", "\nGranting access opens the app drag helper. If a toggle is on but ineffective, remove the old entry and add this app.")
        }
        permissionDetail.stringValue = guidance.trimmingCharacters(in: .whitespacesAndNewlines); permissionDetail.isHidden = guidance.isEmpty
        networkState.stringValue = detectedIP.map { ui("网桥已就绪 · ", "Bridge ready · ") + $0 } ?? ui("未检测到网桥 IPv4，可手动填写本机地址。", "No bridge IPv4 detected. Enter a local address manually.")
        networkState.textColor = detectedIP != nil || !ip.stringValue.isEmpty ? .secondaryLabelColor : .systemOrange
        serviceState.stringValue = status; fitDocument()
    }
    private func fitDocument() {
        let width = max(612, scroll.contentSize.width)
        contentDocument.setFrameSize(NSSize(width: width, height: max(scroll.contentSize.height, contentDocument.frame.height)))
        for field in wrapFields { field.preferredMaxLayoutWidth = max(100, width - 88) }
        contentDocument.layoutSubtreeIfNeeded()
        let height = max(scroll.contentSize.height, root.fittingSize.height + 48)
        if abs(contentDocument.frame.height - height) > 0.5 { contentDocument.setFrameSize(NSSize(width: width, height: height)) }
    }
    func windowDidResize(_ notification: Notification) { fitDocument() }
    func present() { showWindow(nil); fitDocument(); contentDocument.scroll(.zero); NSApp.activate(ignoringOtherApps: true); window?.makeKeyAndOrderFront(nil) }
    @objc private func displayChanged() {
        currentDisplay = displayPicker.selectedItem?.representedObject as? UInt32
        preferredDisplay = currentDisplay
        if let currentDisplay { UserDefaults.standard.set(currentDisplay, forKey: "captureDisplay") }
        updateDisplayLabels(); onDisplay?()
    }
    @objc private func detectDisplays() { refreshDisplays(); onDisplay?() }
    @objc private func pairingChanged() { onPairing?() }
    @objc private func clipboardChanged() { onClipboard?() }
    @objc private func loginStartupChanged() { onLoginStartup?() }
    @objc private func keepAwakeChanged() { onKeepAwake?() }
    @objc private func bootStartupChanged() { onBootStartup?() }
    @objc private func startupSettings() { onStartupSettings?() }
    @objc private func repairStartup() { onRepairStartup?() }
    @objc private func locate() { onLocate?() }
    @objc private func authorizeAll() { onAuthorize?() }
    @objc private func screen() { onScreen?() }
    @objc private func access() { onAccess?() }
    @objc private func refresh() { onRefresh?() }
    @objc private func startHost() { onStart?() }
    @objc private func stopHost() { onStop?() }
    @objc private func relaunch() { onRestart?() }
    @objc private func copyCode() { onCopy?() }
}
