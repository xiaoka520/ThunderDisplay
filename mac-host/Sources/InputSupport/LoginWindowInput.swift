import CoreGraphics
import Foundation
import HIDBridge

/// Owned by the LoginWindow AppKit thread. No PID redirection, HID parameter
/// client or synthetic readiness keys. Session/context guards live in the host.
public final class LoginWindowInput {
    public static let backendName = "Pre-login Quartz session"
    public static var executableHasMarker: Bool { TDPreLoginAppMarkerPresent() }
    private var buttons: UInt8 = 0
    public var isAuthorized: Bool { CGPreflightPostEventAccess() }
    public init() throws {
        guard TDPreLoginAppMarkerPresent() else { throw InputPostingError.missingPreLoginMarker }
        guard CGPreflightPostEventAccess() else { throw InputPostingError.permissionDenied }
    }
    public func post(_ event: CGEvent) throws {
        dispatchPrecondition(condition: .onQueue(.main))
        guard isAuthorized else { throw InputPostingError.permissionDenied }
        switch event.type {
        case .mouseMoved, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged,
             .leftMouseDown, .leftMouseUp, .rightMouseDown, .rightMouseUp, .otherMouseDown, .otherMouseUp:
            let result = TDSessionPostMouse(event, &buttons)
            guard result == 0 else { throw InputPostingError.cursor(result) }
        default:
            // Session posting retains the login application's normal focus and
            // secure-input handling. Quartz provides no keyboard delivery ACK.
            event.post(tap: .cgSessionEventTap)
        }
    }
}
