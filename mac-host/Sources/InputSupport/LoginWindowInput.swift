import CoreGraphics
import Foundation
import HIDBridge

/// The parameter HID client also requires the current local-user context. At a
/// cold LoginWindow it can reject root with NotPrivileged even when PostEvent is
/// authorized. The ordinary Quartz path retains its own macOS authorization.
public final class LoginWindowInput {
    public enum Backend: String { case hid = "System HID", quartz = "Authorized session Quartz" }
    public let backend: Backend
    private let native: NativeHIDInput?
    public let keyboardTarget: Int32?
    public static func mayUseQuartz(after status: Int32, authorized: Bool) -> Bool {
        authorized && UInt32(bitPattern: status) == 0xe00002c1
    }
    public init(onProgress: ((NativeHIDInput.PreparationStep) -> Void)? = nil) throws {
        do {
            native = try NativeHIDInput(onProgress: onProgress); backend = .hid; keyboardTarget = nil
        } catch InputPostingError.system(let status) {
            guard UInt32(bitPattern: status) == 0xe00002c1,
                  Self.mayUseQuartz(after: status, authorized: CGPreflightPostEventAccess()) else {
                throw InputPostingError.system(status)
            }
            native = nil; backend = .quartz
            onProgress?(.keyboardRouting)
            let target = TDLoginWindowKeyboardTarget()
            guard target > 0 else { throw InputPostingError.loginTargetUnavailable }
            keyboardTarget = target
        }
    }
    public var isAuthorized: Bool {
        backend == .hid ? NativeHIDInput.isAuthorized : CGPreflightPostEventAccess()
    }
    public func post(_ event: CGEvent) throws {
        guard isAuthorized else { throw InputPostingError.permissionDenied }
        if let native { try native.post(event) }
        else {
            if Self.isKeyboard(event.type) {
                guard let target = keyboardTarget, TDIsLoginWindowKeyboardTarget(target) else { throw InputPostingError.loginTargetUnavailable }
            }
            try Self.postQuartz(event, keyboardTarget: keyboardTarget)
        }
    }
    public static func isKeyboard(_ type: CGEventType) -> Bool {
        type == .keyDown || type == .keyUp || type == .flagsChanged
    }
    public static func postQuartz(_ event: CGEvent, keyboardTarget: Int32? = nil) throws {
        guard CGPreflightPostEventAccess() else { throw InputPostingError.permissionDenied }
        // Post into the graphical login session rather than re-entering the
        // HID event tap after the native HID client rejected LocalUser.
        // Session events do not reposition the hardware cursor themselves.
        switch event.type {
        case .mouseMoved, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged,
             .leftMouseDown, .leftMouseUp, .rightMouseDown, .rightMouseUp, .otherMouseDown, .otherMouseUp:
            let result = CGWarpMouseCursorPosition(event.location)
            guard result == .success else { throw InputPostingError.cursor(Int32(result.rawValue)) }
        default: break
        }
        // Keep the working session mouse path. Route key and modifier events
        // explicitly to the verified macOS login process, using the standard
        // application-routing API with the same PostEvent authorization.
        if isKeyboard(event.type), let target = keyboardTarget {
            guard target > 0 else { throw InputPostingError.loginTargetUnavailable }
            event.postToPid(target)
            return
        }
        // This standard Quartz API retains macOS authorization and does not
        // provide a delivery receipt. A real LoginWindow check is still required.
        event.post(tap: .cgSessionEventTap)
    }
}
