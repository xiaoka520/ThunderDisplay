import Foundation
import Dispatch

/// Bounds startup operations even if a synchronous system API blocks AppKit.
/// Arm only before input is accepted; timeout recovery must not interrupt typing.
public final class PreparationWatchdog: @unchecked Sendable {
    private let lock = NSLock()
    private var pending: (stage: String, deadline: UInt64, ticket: UInt64)?
    private var nextTicket: UInt64 = 0
    private let timer: DispatchSourceTimer
    public init(onTimeout: @escaping @Sendable (String) -> Void) {
        timer = DispatchSource.makeTimerSource(queue: DispatchQueue(label: "dev.thunderdisplay.preparation-watchdog"))
        timer.setEventHandler { [weak self] in
            guard let self else { return }
            self.lock.lock()
            let expired = self.pending.flatMap { DispatchTime.now().uptimeNanoseconds >= $0.deadline ? $0.stage : nil }
            if expired != nil { self.pending = nil }
            self.lock.unlock()
            if let expired { onTimeout(expired) }
        }
        timer.schedule(deadline: .now(), repeating: .milliseconds(100))
        timer.resume()
    }
    @discardableResult public func arm(_ stage: String, timeout: TimeInterval) -> UInt64 {
        precondition(timeout.isFinite && timeout > 0 && timeout <= 60)
        lock.lock(); defer { lock.unlock() }
        nextTicket &+= 1
        pending = (stage, DispatchTime.now().uptimeNanoseconds + UInt64(timeout * 1_000_000_000), nextTicket)
        return nextTicket
    }
    public func disarm() {
        lock.lock(); defer { lock.unlock() }
        pending = nil
    }
    public func disarm(ticket: UInt64) {
        lock.lock(); defer { lock.unlock() }
        if pending?.ticket == ticket { pending = nil }
    }
    deinit { timer.cancel() }
}
