import Darwin
import Foundation
import Wire

/// One independent TCP video connection. A blocked video write never occupies
/// the input queue; one pending, independent P010 image replaces older work.
final class RawVideoSender: @unchecked Sendable {
    let port: UInt16
    private let session: UInt64, peerIP: in_addr_t, expectedSize: Int
    private let packed: Bool, updates: Bool, width: Int, height: Int
    private let mailbox = VideoMailbox(), lock = NSLock(), available = NSCondition()
    private let work = DispatchQueue(label: "ThunderDisplay.raw-video", qos: .userInteractive)
    private var listener: Int32, connection: Int32 = -1
    private let onFailure: (String) -> Void
    init(ip: String, peerIP: in_addr_t, session: UInt64, width: Int, height: Int, packed: Bool = false, updates: Bool = false, onFailure: @escaping (String) -> Void) throws {
        expectedSize = try RawVideoWire.byteCount(width: width, height: height, packed: packed); self.packed = packed
        guard !updates || packed else { throw HostError("Raw updates require packed ten-bit pixels") }
        self.updates = updates; self.width = width; self.height = height
        self.session = session; self.peerIP = peerIP; self.onFailure = onFailure
        let fd = socket(AF_INET, SOCK_STREAM, 0)
        guard fd >= 0 else { throw HostError("Raw video socket unavailable") }
        var address = sockaddr_in(); address.sin_len = UInt8(MemoryLayout<sockaddr_in>.size); address.sin_family = sa_family_t(AF_INET)
        var size = socklen_t(MemoryLayout<sockaddr_in>.size)
        guard inet_pton(AF_INET, ip, &address.sin_addr) == 1,
              withUnsafePointer(to: &address, { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { bind(fd, $0, size) } }) == 0,
              listen(fd, 2) == 0,
              withUnsafeMutablePointer(to: &address, { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { getsockname(fd, $0, &size) } }) == 0,
              fcntl(fd, F_SETFL, O_NONBLOCK) == 0 else { close(fd); throw HostError("Raw video listener unavailable") }
        listener = fd; port = UInt16(bigEndian: address.sin_port)
        work.async { [self] in run() }
    }
    func enqueue(_ frame: OutgoingVideoFrame) -> Bool {
        guard frame.data.count == expectedSize else { return false }
        let accepted = mailbox.push(frame).accepted
        available.lock(); available.signal(); available.unlock(); return accepted
    }
    func stop() {
        mailbox.stop(); available.lock(); available.broadcast(); available.unlock()
        lock.lock()
        // The worker alone closes descriptors, so a late write cannot reach a
        // reused descriptor belonging to a subsequent graphical session.
        if listener >= 0 { shutdown(listener, SHUT_RDWR) }
        if connection >= 0 { shutdown(connection, SHUT_RDWR) }
        lock.unlock()
    }
    private func closeConnection() {
        lock.lock(); if connection >= 0 { close(connection); connection = -1 }; lock.unlock()
    }
    private func wait(_ fd: Int32, _ events: Int16) {
        var item = pollfd(fd: fd, events: events, revents: 0); _ = poll(&item, 1, 10)
    }
    private func authenticate(_ fd: Int32) -> Bool {
        let expected = RawVideoWire.handshake(session: session)
        var bytes = [UInt8](repeating: 0, count: expected.count), offset = 0
        let count = bytes.count
        let deadline = DispatchTime.now().uptimeNanoseconds + 2_000_000_000
        while offset < bytes.count, !mailbox.cancelled, DispatchTime.now().uptimeNanoseconds < deadline {
            let n = bytes.withUnsafeMutableBytes { recv(fd, $0.baseAddress!.advanced(by: offset), count - offset, 0) }
            if n > 0 { offset += n }
            else if n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { wait(fd, Int16(POLLIN)) }
            else { return false }
        }
        return offset == bytes.count && Data(bytes) == expected
    }
    private func sendAll(_ data: Data, fd: Int32) -> Bool {
        data.withUnsafeBytes { bytes in
            var offset = 0, progress = DispatchTime.now().uptimeNanoseconds
            while offset < bytes.count {
                if mailbox.cancelled { return false }
                // Use the previously deployed socket-send path while restoring
                // first-frame delivery on the physical Thunderbolt link.
                let n = Darwin.send(fd, bytes.baseAddress!.advanced(by: offset), min(262144, bytes.count-offset), 0)
                if n > 0 {
                    offset += n
                    progress = DispatchTime.now().uptimeNanoseconds
                } else if n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    if DispatchTime.now().uptimeNanoseconds-progress > 2_000_000_000 { return false }
                    wait(fd, Int16(POLLOUT))
                } else { return false }
            }
            return true
        }
    }
    private func sendFrame(_ header: Data, payload: Data, fd: Int32) -> Bool {
        sendAll(header, fd: fd) && sendAll(payload, fd: fd)
    }
    private func run() {
        defer {
            closeConnection()
            lock.lock(); if listener >= 0 { close(listener); listener = -1 }; lock.unlock()
        }
        let deadline = DispatchTime.now().uptimeNanoseconds + 15_000_000_000
        while !mailbox.cancelled, DispatchTime.now().uptimeNanoseconds < deadline {
            var address = sockaddr_in(), size = socklen_t(MemoryLayout<sockaddr_in>.size)
            let fd = withUnsafeMutablePointer(to: &address) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { accept(listener, $0, &size) } }
            if fd < 0 { wait(listener, Int16(POLLIN)); continue }
            var yes: Int32 = 1
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, socklen_t(MemoryLayout<Int32>.size))
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, socklen_t(MemoryLayout<Int32>.size))
            guard address.sin_addr.s_addr == peerIP, fcntl(fd, F_SETFL, O_NONBLOCK) == 0 else { close(fd); continue }
            lock.lock(); connection = fd; lock.unlock()
            guard authenticate(fd) else { closeConnection(); continue }
            lock.lock(); close(listener); listener = -1; lock.unlock()
            // Autogrowth tops out at 4 MiB on the measured host. At 6 ms RTT,
            // 4K60 packed10 needs ~7.1 MB in flight. Reserve up to 8 MiB only
            // for this authenticated video socket; never lower a larger buffer.
            var sendCapacity: Int32 = 0, capacityLength = socklen_t(MemoryLayout<Int32>.size)
            let capacityKnown = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendCapacity, &capacityLength) == 0
            var requestedCapacity: Int32 = min(8 * 1024 * 1024, Int32(expectedSize))
            var bufferError: Int32 = 0
            if capacityKnown && sendCapacity < requestedCapacity {
                if setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &requestedCapacity, socklen_t(MemoryLayout<Int32>.size)) != 0 { bufferError = errno }
            }
            capacityLength = socklen_t(MemoryLayout<Int32>.size)
            if getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendCapacity, &capacityLength) == 0 {
                log("Raw TCP send capacity: \(sendCapacity) bytes; requested \(requestedCapacity); option error \(bufferError)")
            }
            var sent = 0, discarded = 0, bytes: UInt64 = 0, total: UInt64 = 0, maximum: UInt64 = 0
            var stats = DispatchTime.now().uptimeNanoseconds
            var replaced = mailbox.replacedFrames
            var baseline: Data?, baseID: UInt32 = 0, fullFrames = 0, updateWork: UInt64 = 0, updateMaximum: UInt64 = 0
            var wireWork: UInt64 = 0, wireMaximum: UInt64 = 0
            var lastSlowReport: UInt64 = 0
            while !mailbox.cancelled {
                available.lock()
                var pending = mailbox.next()
                while pending == nil && !mailbox.cancelled { available.wait(); pending = mailbox.next() }
                available.unlock()
                guard let frame = pending else { break }
                let began = DispatchTime.now().uptimeNanoseconds
                if began-frame.queuedAt > 50_000_000 { discarded += 1; continue }
                let payload: Data
                do { payload = updates ? try RawVideoWire.update(frame.data, baseline: baseline, baseID: baseID, width: width, height: height) : frame.data }
                catch { if !mailbox.cancelled { onFailure("Raw update construction failed") }; return }
                let sending = DispatchTime.now().uptimeNanoseconds, updateDuration = sending-began
                updateWork += updateDuration; updateMaximum = max(updateMaximum,updateDuration)
                if updates && payload.count == expectedSize + 16 { fullFrames += 1 }
                guard sendFrame(RawVideoWire.header(id: frame.id, size: payload.count, pts: frame.pts), payload: payload, fd: fd) else {
                    if !mailbox.cancelled { onFailure("Raw video connection stalled or closed") }; return
                }
                if updates { baseline = frame.data; baseID = frame.id }
                let ended = DispatchTime.now().uptimeNanoseconds, elapsed = ended-began
                wireWork += ended-sending; wireMaximum = max(wireMaximum,ended-sending)
                if (ended-sending > 25_000_000 || updateDuration > 16_666_667), ended-lastSlowReport >= 5_000_000_000 {
                    log("Raw frame slow: id \(frame.id); pts \(frame.pts); bytes \(payload.count); queued us \((began-frame.queuedAt)/1000); build us \(updateDuration/1000); socket us \((ended-sending)/1000)")
                    lastSlowReport = ended
                }
                sent += 1; bytes += UInt64(payload.count); total += elapsed; maximum = max(maximum, elapsed)
                if ended-stats >= 5_000_000_000 {
                    let pendingReplaced = mailbox.replacedFrames
                    log(String(format: "Raw \(packed ? "packed10" : "P010") TCP: %.1f fps, %.1f Gbps; send us avg/max %llu/%llu; stale discarded %d; pending replaced %llu", Double(sent)*1e9/Double(ended-stats), Double(bytes)*8/Double(ended-stats), total/UInt64(max(1,sent))/1000, maximum/1000, discarded, pendingReplaced-replaced))
                    if updates { log("Raw exact updates: full \(fullFrames)/\(sent); bytes/frame \(bytes/UInt64(max(1,sent))); comparison/copy us avg/max \(updateWork/UInt64(max(1,sent))/1000)/\(updateMaximum/1000); socket send us avg/max \(wireWork/UInt64(max(1,sent))/1000)/\(wireMaximum/1000)") }
                    replaced = pendingReplaced
                    var info = tcp_connection_info(), length = socklen_t(MemoryLayout<tcp_connection_info>.size)
                    if getsockopt(fd, IPPROTO_TCP, TCP_CONNECTION_INFO, &info, &length) == 0 {
                        log("Raw TCP window: receiver \(info.tcpi_snd_wnd), scale \(info.tcpi_snd_wscale), congestion \(info.tcpi_snd_cwnd), buffered \(info.tcpi_snd_sbbytes)/\(sendCapacity) bytes; RTT \(info.tcpi_srtt) ms; retransmitted \(info.tcpi_txretransmitbytes) bytes")
                    }
                    stats = ended; sent = 0; discarded = 0; bytes = 0; total = 0; maximum = 0
                    fullFrames = 0; updateWork = 0; updateMaximum = 0; wireWork = 0; wireMaximum = 0
                }
            }
            return
        }
        if !mailbox.cancelled { onFailure("Raw video connection timed out") }
    }
    deinit { stop() }
}
