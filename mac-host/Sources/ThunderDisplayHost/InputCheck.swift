import AppKit
import Carbon
import InputSupport
import HostState
import Wire

/// Explicit diagnostic only; never run automatically at the login window.
/// Generated text goes solely into our own temporary secure field, never logged.
private final class InputCheckTarget: NSObject {
    var clicked = false
    var submitted = false
    @objc func click() { clicked = true }
    @objc func submit() { submitted = true }
}

func checkNativeInput(useQuartz: Bool = false, keyboardOnly: Bool = false) throws {
    guard ConsoleSession.managerName == "Aqua", ConsoleSession.ownsDesktop(geteuid()) else {
        throw HostError("Input diagnostic must run as the current desktop user in Aqua")
    }
    let originalPoint = CGEvent(source: nil)?.location
    let originalFlags = CGEventSource.flagsState(.hidSystemState)
    let eventSource = useQuartz ? nil : CGEventSource(stateID: .privateState)
    let post: (CGEvent) throws -> Void
    if useQuartz {
        let session = try LoginWindowInput()
        post = session.post
    } else {
        let native = try NativeHIDInput(); post = native.post
    }
    let app = NSApplication.shared
    app.setActivationPolicy(.regular)
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 460, height: 160), styleMask: [.titled], backing: .buffered, defer: false)
    window.title = "ThunderDisplay · 输入检查"
    let target = InputCheckTarget()
    let label = NSTextField(labelWithString: "正在验证系统鼠标和安全输入，完成后自动关闭。")
    label.frame = NSRect(x: 20, y: 112, width: 420, height: 24)
    let field = NSSecureTextField(frame: NSRect(x: 20, y: 64, width: 420, height: 28))
    let nextField = NSSecureTextField(frame: NSRect(x: 160, y: 20, width: 280, height: 28))
    field.target = target; field.action = #selector(InputCheckTarget.submit)
    field.nextKeyView = nextField; nextField.nextKeyView = field
    let button = NSButton(title: "鼠标检查", target: target, action: #selector(InputCheckTarget.click))
    button.frame = NSRect(x: 20, y: 20, width: 120, height: 30)
    window.contentView?.addSubview(label); window.contentView?.addSubview(field); window.contentView?.addSubview(nextField); window.contentView?.addSubview(button)
    window.center(); window.makeKeyAndOrderFront(nil); app.activate(ignoringOtherApps: true)
    guard originalFlags.intersection([.maskShift, .maskCommand, .maskControl, .maskAlternate]).isEmpty else {
        window.close(); throw HostError("Release physical modifier keys before checking input")
    }
    var failure: Error?, secureInput = false
    var stage = "launch"
    let input = InputInjector(display: CGMainDisplayID(), captureSize: CGSize(width: 640, height: 360),
        contentRect: CGRect(x: 0, y: 0, width: 640, height: 360), authorized: { true }, post: post, eventSource: eventSource)
    func requireFocus() throws {
        guard app.isActive, window.isKeyWindow else { throw HostError("Input check lost focus during \(stage); typing cancelled (active=\(app.isActive), keyWindow=\(window.isKeyWindow))") }
    }
    func globalPoint(_ view: NSView) -> CGPoint {
        let rect = window.convertToScreen(view.convert(view.bounds, to: nil))
        return CGPoint(x: rect.midX, y: CGDisplayBounds(CGMainDisplayID()).height - rect.midY)
    }
    func mouse(_ type: CGEventType, _ point: CGPoint) throws {
        guard let event = CGEvent(mouseEventSource: eventSource, mouseType: type, mouseCursorPosition: point, mouseButton: .left) else { throw InputPostingError.allocation }
        event.flags = originalFlags; event.setIntegerValueField(.mouseEventClickState, value: 1)
        try post(event)
    }
    func key(_ code: UInt16, down: Bool, shift: Bool) throws {
        try requireFocus()
        let flags: UInt16 = (down ? 1 : 0) | (shift ? 2 : 0)
        var packet = Data([Message.input.rawValue, 3, UInt8(code >> 8), UInt8(code & 255), 0, UInt8(flags)])
        packet.append(Data(repeating: 0, count: 8))
        try input.apply(Input(packet))
    }
    func tap(_ code: UInt16, shift: Bool = false) throws {
        if shift { try key(0xA1, down: true, shift: true) }
        try key(code, down: true, shift: shift); try key(code, down: false, shift: shift)
        if shift { try key(0xA1, down: false, shift: false) }
    }
    Task { @MainActor in
        do {
            // Activate after AppKit has completed launch, then verify focus before
            // generating any clicks or keys. Pre-run activation can be discarded.
            try await Task.sleep(nanoseconds: 100_000_000)
            window.makeKeyAndOrderFront(nil); app.activate(ignoringOtherApps: true)
            // A UIElement app becoming regular may need an explicit activation
            // by LaunchServices/window focus. Wait without generating input.
            for _ in 0..<600 {
                if app.isActive && window.isKeyWindow { break }
                try await Task.sleep(nanoseconds: 100_000_000)
            }
            try requireFocus()
            if keyboardOnly {
                guard window.makeFirstResponder(field) else { throw HostError("Cannot focus our keyboard diagnostic field") }
            } else {
                stage = "mouse click"
                try mouse(.mouseMoved, globalPoint(button))
                try await Task.sleep(nanoseconds: 100_000_000)
                guard let cursor = CGEvent(source: nil)?.location,
                      hypot(cursor.x-globalPoint(button).x, cursor.y-globalPoint(button).y) < 2 else {
                    let expected = globalPoint(button), actual = CGEvent(source: nil)?.location ?? .zero
                    throw HostError("System cursor coordinates did not match the test target (expected \(expected.x),\(expected.y); actual \(actual.x),\(actual.y)); click cancelled")
                }
                try mouse(.leftMouseDown, globalPoint(button)); try mouse(.leftMouseUp, globalPoint(button))
                try await Task.sleep(nanoseconds: 200_000_000)
                guard target.clicked else { throw HostError("System mouse event was accepted but our button did not receive the click") }
                try requireFocus(); stage = "field focus"
                try mouse(.mouseMoved, globalPoint(field)); try mouse(.leftMouseDown, globalPoint(field)); try mouse(.leftMouseUp, globalPoint(field))
                try await Task.sleep(nanoseconds: 200_000_000)
            }
            guard field.currentEditor() != nil else { throw HostError("Our secure diagnostic field did not gain focus") }
            stage = "secure keyboard"
            guard EnableSecureEventInput() == noErr else { throw HostError("Cannot enable secure input for diagnostic") }
            secureInput = true
            for (code, shifted) in [(UInt16(0x54), false), (UInt16(0x44), true), (UInt16(0x39), false)] {
                try tap(code, shift: shifted)
                try await Task.sleep(nanoseconds: 80_000_000)
            }
            try await Task.sleep(nanoseconds: 200_000_000)
            guard (field.currentEditor()?.string ?? field.stringValue) == "tD9" else { throw HostError("System keyboard event was accepted but secure-field text did not match") }
            stage = "navigation keyboard"
            try tap(0x25); try tap(0x08) // Left then Backspace: delete the middle letter.
            try await Task.sleep(nanoseconds: 150_000_000)
            guard (field.currentEditor()?.string ?? field.stringValue) == "t9" else { throw HostError("Arrow or Backspace event did not reach our secure field") }
            try tap(0x44, shift: true)
            try await Task.sleep(nanoseconds: 150_000_000)
            guard (field.currentEditor()?.string ?? field.stringValue) == "tD9" else { throw HostError("Navigation did not preserve the insertion point") }
            try tap(0x09)
            try await Task.sleep(nanoseconds: 150_000_000)
            guard nextField.currentEditor() != nil else { throw HostError("Tab did not move focus to our second secure field") }
            try tap(0x09, shift: true)
            try await Task.sleep(nanoseconds: 150_000_000)
            guard field.currentEditor() != nil else { throw HostError("Shift-Tab did not return to our first secure field") }
            target.submitted = false
            try tap(0x0D)
            try await Task.sleep(nanoseconds: 150_000_000)
            guard target.submitted else { throw HostError("Return did not submit our secure field") }
        } catch { failure = error }
        input.releaseAll()
        if let restore = CGEvent(keyboardEventSource: eventSource, virtualKey: 57, keyDown: false) {
            restore.type = .flagsChanged; restore.flags = originalFlags; try? post(restore)
        }
        if secureInput { _ = DisableSecureEventInput() }
        if let originalPoint { _ = CGWarpMouseCursorPosition(originalPoint) }
        field.stringValue = ""; nextField.stringValue = ""; window.endEditing(for: nil); window.orderOut(nil)
        app.stop(nil)
        app.postEvent(NSEvent.otherEvent(with: .applicationDefined, location: .zero, modifierFlags: [], timestamp: 0, windowNumber: 0, context: nil, subtype: 0, data1: 0, data2: 0)!, atStart: false)
    }
    app.run(); window.close()
    if let failure { throw failure }
    print("\(useQuartz ? "Pre-login Quartz session" : "System HID") input verified: \(keyboardOnly ? "keyboard only" : "mouse and keyboard"), protocol keyboard, Shift, arrows, Backspace, Tab, Shift-Tab, Return and secure field. No user text or password recorded.")
}
