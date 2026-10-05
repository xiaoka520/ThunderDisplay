public enum PermissionState: Equatable {
    case ready, notEffective, relaunchRequired, checkFailed
    public static func evaluate(current: Bool, fresh: Bool?, verified: Bool? = nil) -> Self {
        if let verified {
            if verified { return .ready }
            return !current && fresh == true ? .relaunchRequired : .checkFailed
        }
        if current { return .ready }
        return fresh == true ? .relaunchRequired : .notEffective
    }
    public var usable: Bool { self == .ready }
    public var granted: Bool { self == .ready || self == .relaunchRequired }
    public static func authorizationComplete(screen: Self, access: Self) -> Bool { screen.granted && access.granted }
}
