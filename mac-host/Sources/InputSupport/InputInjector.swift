import CoreGraphics
import ApplicationServices
import AppKit
import Wire

public final class InputInjector {
    private let bounds: CGRect
    private let captureSize: CGSize, contentRect: CGRect
    private let source = CGEventSource(stateID: .privateState)
    private let authorized: () -> Bool
    private let post: (CGEvent) throws -> Void
    private var keys = Set<CGKeyCode>(), buttons = Set<UInt16>()
    private var lockedFlags: CGEventFlags = []
    private var point: CGPoint
    private var clickButton: UInt16?, clickPoint = CGPoint.zero, clickTime: UInt64 = 0, clickCount: Int64 = 1
    public convenience init(display: CGDirectDisplayID, captureSize: CGSize, contentRect: CGRect,
                            authorized: @escaping () -> Bool = { CGPreflightPostEventAccess() },
                            post: @escaping (CGEvent) throws -> Void = { $0.post(tap: .cghidEventTap) }) {
        self.init(bounds: CGDisplayBounds(display), captureSize: captureSize, contentRect: contentRect, authorized: authorized, post: post)
    }
    public init(bounds: CGRect, captureSize: CGSize, contentRect: CGRect,
                authorized: @escaping () -> Bool, post: @escaping (CGEvent) throws -> Void) {
        self.bounds = bounds; point = CGPoint(x: bounds.midX, y: bounds.midY)
        self.captureSize = captureSize; self.contentRect = contentRect
        self.authorized = authorized; self.post = post
    }
    private func flags(_ bits: UInt16) -> CGEventFlags {
        var f: CGEventFlags = []
        if bits & 2 != 0 { f.insert(.maskShift) }
        if bits & 4 != 0 { f.insert(.maskControl) }
        if bits & 8 != 0 { f.insert(.maskAlternate) }
        if bits & 16 != 0 { f.insert(.maskCommand) }
        if bits & 32 != 0 { f.insert(.maskAlphaShift) }
        return f
    }
    public func apply(_ input: Input) throws {
        if input.kind == 5 { releaseAll(); return }
        guard authorized() else { throw InputPostingError.permissionDenied }
        let down = input.flags & 1 != 0
        let f = flags(input.flags)
        lockedFlags = f.intersection(.maskAlphaShift)
        switch input.kind {
        case 1, 2:
            guard (0...65535).contains(input.x), (0...65535).contains(input.y) else { return }
            let pixel = CGPoint(x: CGFloat(input.x) / 65535 * (captureSize.width - 1),
                                y: CGFloat(input.y) / 65535 * (captureSize.height - 1))
            // Ignore black bars on capture; button releases and active drags still reach the edge.
            if !contentRect.contains(pixel), (input.kind == 1 && buttons.isEmpty) || (input.kind == 2 && down) { return }
            let nx = min(1, max(0, (pixel.x - contentRect.minX) / max(1, contentRect.width - 1)))
            let ny = min(1, max(0, (pixel.y - contentRect.minY) / max(1, contentRect.height - 1)))
            point = CGPoint(x: bounds.minX + nx * max(0, bounds.width - 1), y: bounds.minY + ny * max(0, bounds.height - 1))
            var type: CGEventType = .mouseMoved; var button: CGMouseButton = .left
            if input.kind == 2 {
                guard input.code < 3 else { return }
                button = input.code == 0 ? .left : input.code == 1 ? .right : .center
                type = input.code == 0 ? (down ? .leftMouseDown : .leftMouseUp) :
                       input.code == 1 ? (down ? .rightMouseDown : .rightMouseUp) : (down ? .otherMouseDown : .otherMouseUp)
                if down { buttons.insert(input.code) } else { buttons.remove(input.code) }
                if down {
                    let now = DispatchTime.now().uptimeNanoseconds
                    if clickButton == input.code, now - clickTime <= UInt64(NSEvent.doubleClickInterval * 1_000_000_000),
                       hypot(point.x - clickPoint.x, point.y - clickPoint.y) <= 4 { clickCount = min(3, clickCount + 1) }
                    else { clickCount = 1 }
                    clickButton = input.code; clickPoint = point; clickTime = now
                }
            } else if buttons.contains(0) { type = .leftMouseDragged }
            else if buttons.contains(1) { type = .rightMouseDragged; button = .right }
            else if buttons.contains(2) { type = .otherMouseDragged; button = .center }
            let e = CGEvent(mouseEventSource: source, mouseType: type, mouseCursorPosition: point, mouseButton: button)
            if input.kind == 2 { e?.setIntegerValueField(.mouseEventClickState, value: clickCount) }
            guard let e else { throw InputPostingError.allocation }
            e.flags = deviceFlags(f); try post(e)
        case 3:
            guard let key = Self.keyMap[input.code] else { return }
            let repeatKey = down && keys.contains(key)
            if down { keys.insert(key) } else { keys.remove(key) }
            let e = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: down)
            guard let e else { throw InputPostingError.allocation }
            // Modifier transitions are flagsChanged, not ordinary character keys.
            if (54...62).contains(key) { e.type = .flagsChanged }
            e.flags = deviceFlags(f)
            e.setIntegerValueField(.keyboardEventAutorepeat, value: repeatKey ? 1 : 0)
            try post(e)
        case 4:
            let e = CGEvent(scrollWheelEvent2Source: source, units: .pixel, wheelCount: 2,
                            wheel1: max(-2000, min(2000, input.y)), wheel2: max(-2000, min(2000, input.x)), wheel3: 0)
            guard let e else { throw InputPostingError.allocation }
            e.flags = deviceFlags(f); try post(e)
        default: break
        }
    }
    private func deviceFlags(_ flags: CGEventFlags) -> CGEventFlags {
        var result = flags
        let masks: [CGKeyCode: UInt64] = [59:1,56:2,60:4,55:8,54:16,58:32,61:64,62:8192]
        for (key, mask) in masks where keys.contains(key) { result.insert(CGEventFlags(rawValue: mask)) }
        return result
    }
    public func releaseAll() {
        // Release character keys before modifiers, and update left/right state
        // for each event so a disconnect cannot leave Shift/Command held down.
        for key in keys.sorted(by: { ((54...62).contains($0) ? 1 : 0, $0) < ((54...62).contains($1) ? 1 : 0, $1) }) {
            keys.remove(key)
            if let e = CGEvent(keyboardEventSource: source, virtualKey: key, keyDown: false) {
                if (54...62).contains(key) { e.type = .flagsChanged }
                var f = lockedFlags
                if keys.contains(56) || keys.contains(60) { f.insert(.maskShift) }
                if keys.contains(59) || keys.contains(62) { f.insert(.maskControl) }
                if keys.contains(58) || keys.contains(61) { f.insert(.maskAlternate) }
                if keys.contains(54) || keys.contains(55) { f.insert(.maskCommand) }
                e.flags = deviceFlags(f); try? post(e)
            }
        }
        keys.removeAll()
        for button in buttons {
            let type: CGEventType = button == 0 ? .leftMouseUp : button == 1 ? .rightMouseUp : .otherMouseUp
            let b: CGMouseButton = button == 0 ? .left : button == 1 ? .right : .center
            if let event = CGEvent(mouseEventSource: source, mouseType: type, mouseCursorPosition: point, mouseButton: b) {
                event.flags = lockedFlags; try? post(event)
            }
        }
        buttons.removeAll()
        clickButton = nil
    }
    // Windows virtual keys -> physical ANSI macOS keys; left/right modifiers remain distinct.
    private static let keyMap: [UInt16: CGKeyCode] = [
        0x41:0,0x53:1,0x44:2,0x46:3,0x48:4,0x47:5,0x5A:6,0x58:7,0x43:8,0x56:9,0x42:11,
        0x51:12,0x57:13,0x45:14,0x52:15,0x59:16,0x54:17,0x31:18,0x32:19,0x33:20,0x34:21,
        0x36:22,0x35:23,0xBB:24,0x39:25,0x37:26,0xBD:27,0x38:28,0x30:29,0xDD:30,0x4F:31,
        0x55:32,0xDB:33,0x49:34,0x50:35,0x0D:36,0x4C:37,0x4A:38,0xDE:39,0x4B:40,0xBA:41,
        0xDC:42,0xBC:43,0xBF:44,0x4E:45,0x4D:46,0xBE:47,0x09:48,0x20:49,0xC0:50,0x08:51,
        0x1B:53,0x5C:54,0x5B:55,0x10:56,0xA0:56,0x14:57,0x12:58,0xA4:58,0x11:59,0xA2:59,0x10D:76,
        0xA1:60,0xA5:61,0xA3:62,0x6E:65,0x6A:67,0x6B:69,0x90:71,0x6F:75,0x6D:78,
        0x60:82,0x61:83,0x62:84,0x63:85,0x64:86,0x65:87,0x66:88,0x67:89,0x68:91,0x69:92,
        0x74:96,0x75:97,0x76:98,0x72:99,0x77:100,0x78:101,0x7A:103,0x7C:105,0x7F:106,
        0x7D:107,0x79:109,0x7B:111,0x7E:113,0x2D:114,0x24:115,0x21:116,0x2E:117,
        0x73:118,0x23:119,0x71:120,0x22:121,0x70:122,0x25:123,0x27:124,0x28:125,0x26:126
    ]
}
