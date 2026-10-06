import Foundation
import Wire

/// One controller per connection. Network threads only enqueue; creation,
/// delivery and release run on AppKit's queue without blocking socket teardown.
public final class QueuedInputController: InputControlling {
    private let lock = NSLock()
    private var pending: [Input] = [], scheduled = false, closed = false
    private var controller: (any InputControlling)? // Execution queue only.
    private let allowed: () -> Bool
    private let factory: () throws -> any InputControlling
    private let onFailure: (String) -> Void
    private let schedule: (@escaping () -> Void) -> Void
    private let capacity: Int
    public init(capacity: Int = 256, allowed: @escaping () -> Bool,
                factory: @escaping () throws -> any InputControlling,
                onFailure: @escaping (String) -> Void,
                schedule: @escaping (@escaping () -> Void) -> Void = { DispatchQueue.main.async(execute: $0) }) {
        self.capacity = max(1, capacity); self.allowed = allowed; self.factory = factory
        self.onFailure = onFailure; self.schedule = schedule
    }
    public func apply(_ input: Input) throws {
        lock.lock()
        guard !closed else { lock.unlock(); throw InputPostingError.sessionEnded }
        // Coalesce only adjacent pointer moves; never move a key/button across
        // the click that focused a password field or discard its release.
        if input.kind == 1, pending.last?.kind == 1 { pending[pending.count - 1] = input }
        else {
            guard pending.count < capacity else { lock.unlock(); throw InputPostingError.queueFull }
            pending.append(input)
        }
        let start = !scheduled; scheduled = true; lock.unlock()
        if start { schedule { self.drain() } }
    }
    public func releaseAll() {
        lock.lock(); closed = true; pending.removeAll(); lock.unlock()
        schedule { self.controller?.releaseAll(); self.controller = nil }
    }
    private func drain() {
        for _ in 0..<32 {
            lock.lock()
            guard !closed, !pending.isEmpty else { scheduled = false; lock.unlock(); return }
            let input = pending.removeFirst(); lock.unlock()
            guard allowed() else { releaseAll(); return }
            do {
                if controller == nil { controller = try factory() }
                try controller?.apply(input)
            } catch {
                releaseAll(); onFailure(error.localizedDescription); return
            }
        }
        schedule { self.drain() }
    }
}
