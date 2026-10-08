import Darwin
import Foundation
import Wire

/// Exercise the production TCP sender at 4K60 with synthetic pixels only.
/// Loopback measures software capacity, not the physical Thunderbolt link.
func rawTransportCheck() throws {
    let session: UInt64 = 0x0102030405060708
    let count = try RawVideoWire.byteCount(width: 4096, height: 2560, packed: true)
    let source = Data(repeating: 0, count: try RawVideoWire.byteCount(width: 4096, height: 2560))
    var packTotal: UInt64 = 0, packMaximum: UInt64 = 0
    for _ in 0..<20 {
        let began = DispatchTime.now().uptimeNanoseconds
        let result = try source.withUnsafeBytes { bytes in
            try RawVideoWire.pack(width: 4096, height: 2560, luma: bytes, lumaStride: 8192,
                chroma: UnsafeRawBufferPointer(start: bytes.baseAddress!.advanced(by: 4096 * 2560 * 2), count: 4096 * 2560), chromaStride: 8192, packed: true)
        }
        let duration = DispatchTime.now().uptimeNanoseconds-began
        guard result.count == count, result.first == 0, result.last == 0 else { throw HostError("Packed ten-bit diagnostic failed") }
        packTotal += duration; packMaximum = max(packMaximum, duration)
    }
    print(String(format: "Raw packing: 4096×2560, 20 synthetic frames; us avg/max %llu/%llu; payload %d bytes; all ten effective bits retained", packTotal/20/1000, packMaximum/1000, count))
    let state = NSLock(), done = DispatchGroup()
    var failure: String?, frames = 0, first: UInt64 = 0, last: UInt64 = 0
    let sender = try RawVideoSender(ip: "127.0.0.1", peerIP: inet_addr("127.0.0.1"), session: session, width: 4096, height: 2560, packed: true) { reason in
        state.lock(); failure = reason; state.unlock()
    }
    defer { sender.stop() }
    done.enter()
    DispatchQueue.global(qos: .userInteractive).async {
        defer { done.leave() }
        let fd = socket(AF_INET, SOCK_STREAM, 0)
        guard fd >= 0 else { state.lock(); failure = "Loopback receiver unavailable"; state.unlock(); return }
        defer { close(fd) }
        var endpoint = sockaddr_in(); endpoint.sin_len = UInt8(MemoryLayout<sockaddr_in>.size); endpoint.sin_family = sa_family_t(AF_INET)
        endpoint.sin_addr.s_addr = inet_addr("127.0.0.1"); endpoint.sin_port = sender.port.bigEndian
        var timeout = timeval(tv_sec: 2, tv_usec: 0)
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        guard withUnsafePointer(to: &endpoint, { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { connect(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size)) } }) == 0 else {
            state.lock(); failure = "Loopback connect failed"; state.unlock(); return
        }
        let hello = RawVideoWire.handshake(session: session)
        guard hello.withUnsafeBytes({ Darwin.send(fd, $0.baseAddress!, $0.count, 0) }) == hello.count else { return }
        func exact(_ data: inout Data) -> Bool {
            data.withUnsafeMutableBytes { (bytes: UnsafeMutableRawBufferPointer) in
                var offset = 0
                while offset < bytes.count {
                    let n = recv(fd, bytes.baseAddress!.advanced(by: offset), bytes.count-offset, 0)
                    if n <= 0 { return false }; offset += n
                }
                return true
            }
        }
        var header = Data(count: 20), pixels = Data(count: count), previous: UInt32 = 0
        while exact(&header) {
            do {
                var reader = Reader(header)
                guard try reader.get(UInt32.self) == 0x54445246 else { throw WireError.malformed }
                let id = try reader.get(UInt32.self)
                guard id > previous, try reader.get(UInt32.self) == UInt32(count), exact(&pixels) else { throw WireError.malformed }
                previous = id
                guard stride(from: 0, to: count, by: 4096).allSatisfy({ pixels[$0] == 0 }) else { throw WireError.malformed }
                let now = DispatchTime.now().uptimeNanoseconds
                state.lock(); if frames == 0 { first = now }; frames += 1; last = now; state.unlock()
            } catch { state.lock(); failure = "Raw loopback frame was incomplete or corrupt"; state.unlock(); break }
        }
    }
    let pixels = Data(repeating: 0, count: count)
    let queue = DispatchQueue(label: "ThunderDisplay.raw-check-producer", qos: .userInteractive)
    let timer = DispatchSource.makeTimerSource(queue: queue)
    var id: UInt32 = 0
    timer.schedule(deadline: .now(), repeating: .nanoseconds(16_666_667), leeway: .microseconds(100))
    timer.setEventHandler { id += 1; _ = sender.enqueue(OutgoingVideoFrame(data: pixels, pts: UInt64(id)*16667, id: id, key: true)) }
    timer.resume()
    RunLoop.current.run(until: Date().addingTimeInterval(3))
    timer.cancel(); queue.sync {}
    // Finish the last complete frame before closing the stream.
    RunLoop.current.run(until: Date().addingTimeInterval(0.1)); sender.stop()
    guard done.wait(timeout: .now()+3) == .success else { throw HostError("Raw loopback teardown timed out") }
    state.lock(); let result = (failure, frames, first, last); state.unlock()
    if let failure = result.0 { throw HostError(failure) }
    guard result.1 > 1, result.3 > result.2 else { throw HostError("Raw loopback returned no complete frames") }
    let seconds = Double(result.3-result.2)/1e9, fps = Double(result.1-1)/seconds
    print(String(format: "Raw TCP loopback: 4096×2560 packed10, %.1f fps, %.2f Gbps, %d complete synthetic frames; no capture/input; not a physical-link benchmark", fps, fps*Double(count)*8/1e9, result.1))
}
