import Darwin
import Foundation
import Wire

// Own a duplicate of the video socket. Closing/reusing the control server's
// descriptor cannot redirect an old sender into a new graphical session.
final class VideoSender: @unchecked Sendable {
    private let work=DispatchQueue(label:"ThunderDisplay.video-send",qos:.userInteractive)
    private let mailbox=VideoMailbox(), socketLock=NSLock(), session: UInt64, codec: Codec
    private var socket: Int32
    private let target: sockaddr_in, maximumAge: UInt64
    private let onFailure: () -> Void
    private let emit: ((OutgoingVideoFrame, () -> Bool) -> Bool)?
    private var sent=0, failed=0, totalWait: UInt64=0, totalSend: UInt64=0, maxWait: UInt64=0, maxSend: UInt64=0
    private var lastStats=DispatchTime.now().uptimeNanoseconds
    init(socket: Int32, session: UInt64, codec: Codec, target: sockaddr_in, fps: UInt16,
         emit: ((OutgoingVideoFrame, () -> Bool) -> Bool)? = nil, onFailure: @escaping () -> Void) throws {
        let copy=dup(socket)
        guard copy>=0 else { throw HostError("Duplicate video socket: \(errno)") }
        self.socket=copy; self.session=session; self.codec=codec; self.target=target
        // A stalled sender cannot accumulate hundreds of milliseconds of video.
        maximumAge=max(50_000_000,2_000_000_000/UInt64(max(1,fps)))
        self.emit=emit; self.onFailure=onFailure
    }
    func enqueue(_ frame: OutgoingVideoFrame) -> Bool {
        let result=mailbox.push(frame)
        if result.start { work.async { [self] in drain() } }
        return result.accepted
    }
    private func send(_ frame: OutgoingVideoFrame) -> Bool {
        if let emit { return emit(frame,{ self.mailbox.cancelled }) }
        var destination=target
        let count=(frame.data.count+ProtocolWire.fragmentSize-1)/ProtocolWire.fragmentSize
        for index in 0..<count {
            guard !mailbox.cancelled else { return false }
            let packet=ProtocolWire.packet(frame:frame.data,session:session,id:frame.id,pts:frame.pts,codec:codec,key:frame.key,index:index)
            // Lock only the nonblocking syscall, never packet construction or
            // the whole frame. Stop can release the bound port immediately.
            socketLock.lock()
            let result=socket<0 ? -1 : packet.withUnsafeBytes { bytes in withAddress(&destination) { sendto(socket,bytes.baseAddress!,bytes.count,0,$0,$1) } }
            socketLock.unlock()
            guard result==packet.count else { return false }
        }
        return true
    }
    private func drain() {
        while let frame=mailbox.next() {
            let begin=DispatchTime.now().uptimeNanoseconds, wait=begin-frame.queuedAt
            let success=wait<=maximumAge && send(frame)
            let duration=DispatchTime.now().uptimeNanoseconds-begin
            totalWait &+= wait; totalSend &+= duration; maxWait=max(maxWait,wait); maxSend=max(maxSend,duration)
            if success { sent+=1 }
            else if !mailbox.cancelled { failed+=1; mailbox.discontinuity(); onFailure() }
            let now=DispatchTime.now().uptimeNanoseconds
            if now-lastStats>=5_000_000_000 {
                let count=max(1,sent+failed)
                log("Video send latency us: queue avg \(totalWait/UInt64(count)/1000), max \(maxWait/1000); send avg \(totalSend/UInt64(count)/1000), max \(maxSend/1000); sent \(sent), discarded \(failed)")
                lastStats=now; sent=0; failed=0; totalWait=0; totalSend=0; maxWait=0; maxSend=0
            }
        }
    }
    func stop() {
        mailbox.stop()
        socketLock.lock(); if socket>=0 { close(socket); socket = -1 }; socketLock.unlock()
        // No wait for the video work item on the input/port-handover queue.
    }
    deinit { stop() }
}
