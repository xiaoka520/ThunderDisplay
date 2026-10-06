import Foundation

public struct OutgoingVideoFrame: Sendable {
    public let data: Data, pts: UInt64, id: UInt32, key: Bool, queuedAt: UInt64
    public init(data: Data, pts: UInt64, id: UInt32, key: Bool) {
        self.data=data; self.pts=pts; self.id=id; self.key=key
        queuedAt=DispatchTime.now().uptimeNanoseconds
    }
}

// One frame being sent plus at most one pending frame. Dropping a reference
// frame retires the dependency chain; only a fresh IDR can resume that chain.
public final class VideoMailbox: @unchecked Sendable {
    private let lock=NSLock()
    private var pending: OutgoingVideoFrame?
    private var draining=false, stopped=false, needsKey=true
    public init() {}
    public func push(_ frame: OutgoingVideoFrame) -> (accepted: Bool, start: Bool) {
        lock.lock(); defer { lock.unlock() }
        guard !stopped else { return (false,false) }
        if pending != nil { pending=nil; needsKey=true }
        guard !needsKey || frame.key else { return (false,false) }
        if frame.key { needsKey=false }
        pending=frame
        let start = !draining; draining=true
        return (true,start)
    }
    public func next() -> OutgoingVideoFrame? {
        lock.lock(); defer { lock.unlock() }
        guard !stopped, let frame=pending else { draining=false; return nil }
        pending=nil; return frame
    }
    public func discontinuity() {
        lock.lock(); pending=nil; needsKey=true; lock.unlock()
    }
    public func stop() { lock.lock(); stopped=true; pending=nil; lock.unlock() }
    public var cancelled: Bool { lock.lock(); defer { lock.unlock() }; return stopped }
}
