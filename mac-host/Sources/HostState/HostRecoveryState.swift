import Foundation

// Only an explicit user pause changes the intent to run the host.
public struct HostRecoveryState {
    public private(set) var requested = true
    public private(set) var sleeping = false
    public private(set) var retryAt: TimeInterval = 0
    public init() {}
    public func canStart(at now: TimeInterval) -> Bool { requested && !sleeping && now >= retryAt }
    public mutating func pause() { requested = false; retryAt = 0 }
    public mutating func start(at now: TimeInterval) { requested = true; retryAt = now }
    public mutating func willSleep() { sleeping = true }
    public mutating func didWake(at now: TimeInterval) { sleeping = false; retryAt = now + 1 }
    public mutating func displaysChanged(at now: TimeInterval) { retryAt = now + 1 }
    public mutating func failed(at now: TimeInterval) { retryAt = now + 3 }
    public mutating func started() { retryAt = 0 }
}
