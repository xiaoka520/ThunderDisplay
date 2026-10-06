import CoreGraphics
import Foundation
import HIDBridge

public enum InputPostingError: Error, CustomStringConvertible, LocalizedError {
    case permissionDenied, allocation, system(Int32), cursor(Int32), loginTargetUnavailable
    public var description: String {
        switch self {
        case .permissionDenied: return "Keyboard/mouse event posting is not authorized"
        case .allocation: return "Cannot create keyboard/mouse event"
        case .system(let status): return String(format: "System HID input rejected (0x%08x)", UInt32(bitPattern: status))
        case .cursor(let status): return "Graphical-session cursor positioning failed (\(status))"
        case .loginTargetUnavailable: return "System login keyboard target is unavailable or changed"
        }
    }
    public var errorDescription: String? { description }
}

/// The LoginWindow host creates this after its root/bootstrap/session guard.
/// An explicit Aqua diagnostic uses the same API under the logged-in user.
/// The connection is used on HostServer's serial input queue, not shared with
/// the desktop host. macOS continues to enforce event-post authorization.
public final class NativeHIDInput {
    public enum PreparationStep: String { case authorization = "System HID authorization", connection = "Opening system HID connection", delivery = "Preparing authorized system HID input", keyboardRouting = "Checking system login keyboard destination" }
    public static var isAuthorized: Bool { TDHIDAuthorized() }
    private let connection: OpaquePointer
    public init(onProgress: ((PreparationStep) -> Void)? = nil) throws {
        onProgress?(.authorization)
        guard Self.isAuthorized else { throw InputPostingError.permissionDenied }
        var status: Int32 = 0
        onProgress?(.connection)
        guard let connection = TDHIDOpen(&status) else { throw InputPostingError.system(status) }
        onProgress?(.delivery)
        let checked = TDHIDCheck(connection)
        if checked != 0 { TDHIDClose(connection); throw InputPostingError.system(checked) }
        self.connection = connection
    }
    deinit { TDHIDClose(connection) }
    public func post(_ event: CGEvent) throws {
        let result = TDHIDPost(connection, event)
        guard result == 0 else { throw InputPostingError.system(result) }
    }
}
