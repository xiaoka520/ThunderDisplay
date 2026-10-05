import Darwin
import CoreGraphics
import Foundation
import Wire

// Mutable peer state is confined to HostServer.queue.
private final class Peer: @unchecked Sendable {
    let fd: Int32, address: sockaddr_in
    var source: DispatchSourceRead?, framer = ControlFramer(), output = Data()
    var negotiated = false, ready = false, lastSeen = DispatchTime.now().uptimeNanoseconds
    let accepted = DispatchTime.now().uptimeNanoseconds
    var session: UInt64 = 0, frame: UInt32 = 0, videoAddress: sockaddr_in?
    var engine: CaptureEngine?, injector: InputInjector?
    var richFeatures = false, extendedFeatures = false, highBitrate = false, localCursor = false, clipboard = false
    var clipboardRequested = false
    var desktopSRGB = false, cursorVariants = false
    var clipboardInput = TextClipboardAssembler(), clipboardOutput: [Data] = []
    var imageInput = BinaryAssembler(), cursorOutput: [Data] = []
    var cursorID: UInt32 = 0
    var clipboardID: UInt32 = 0
    init(fd: Int32, address: sockaddr_in) { self.fd = fd; self.address = address }
}

final class HostServer: @unchecked Sendable {
    let queue = DispatchQueue(label: "ThunderDisplay.pipeline", qos: .userInteractive)
    private let ip: String, port: UInt16, display: UInt32?, token: Data
    private let requirePairing: Bool
    private var listener: Int32 = -1, udp: Int32 = -1, discovery: Int32 = -1
    private var acceptSource: DispatchSourceRead?, discoverySource: DispatchSourceRead?, videoSource: DispatchSourceRead?, timer: DispatchSourceTimer?
    private var videoPort: UInt16 = 0
    private var peer: Peer?
    var onCaptureFailure: ((String) -> Void)?
    var onStatus: ((String) -> Void)?
    var onClipboardState: ((UInt64, Bool) -> Void)?
    var onClipboardText: ((UInt64, String) -> Void)?
    var onClipboardImage: ((UInt64, Data) -> Void)?
    var onCursorState: ((UInt64, Bool, Bool) -> Void)?
    var localCursorAvailable = false // Main-thread snapshot before starting the server.
    var allowClipboard = true // Initialized before start; later changes go through the queue.
    var restrictToLocalSubnet = false // Always true in the root LoginWindow agent.
    var inputAllowed: (() -> Bool)? // Recheck the session before every injected packet.
    func updateClipboardPermission(_ allowed: Bool) {
        queue.async { [weak self] in
            guard let self else { return }; self.allowClipboard = allowed
            if let p = self.peer, p.ready, p.clipboardRequested { self.configureClipboard(p, enabled: allowed) }
        }
    }
    private func configureClipboard(_ p: Peer, enabled: Bool) {
        p.clipboard = enabled; p.clipboardInput = TextClipboardAssembler(); p.imageInput = BinaryAssembler(); p.clipboardOutput = []
        onClipboardState?(p.session, enabled)
        send(Data([Message.clipboardControl.rawValue, enabled ? 1 : 0]), p)
    }
    // Snapshot collected on the AppKit thread; no NSScreen calls on the socket queue.
    var displayCapabilities: Data?
    var captureDisplayID: UInt32? { display }
    func updateDisplayCapabilities(_ data: Data) { queue.async { [weak self] in self?.displayCapabilities = data } }
    init(ip: String, options: Options, token: String) {
        self.ip = ip; port = options.port; display = options.display; self.token = Data(token.utf8)
        requirePairing = options.requirePairing
    }
    func start() throws {
        do {
            listener = try makeSocket(type: SOCK_STREAM, port: port)
            guard listen(listener, 4) == 0 else { throw HostError("listen: \(errno)") }
            udp = try makeSocket(type: SOCK_DGRAM, port: 0)
            var endpoint = sockaddr_in(), length = socklen_t(MemoryLayout<sockaddr_in>.size)
            guard withUnsafeMutablePointer(to: &endpoint, { p in
                p.withMemoryRebound(to: sockaddr.self, capacity: 1) { getsockname(udp, $0, &length) }
            }) == 0 else { throw HostError("Cannot obtain UDP video port") }
            videoPort = UInt16(bigEndian: endpoint.sin_port)
            var buffer: Int32 = 4 * 1024 * 1024
            while setsockopt(udp, SOL_SOCKET, SO_SNDBUF, &buffer, socklen_t(MemoryLayout<Int32>.size)) != 0 && buffer > 65536 { buffer /= 2 }
            var actual: Int32 = 0, optionLength = socklen_t(MemoryLayout<Int32>.size)
            getsockopt(udp, SOL_SOCKET, SO_SNDBUF, &actual, &optionLength)
            log("UDP video port \(videoPort); send buffer \(actual) bytes")
            discovery = try makeSocket(type: SOCK_DGRAM, port: port, bindIP: "0.0.0.0")
            let accept = DispatchSource.makeReadSource(fileDescriptor: listener, queue: queue)
            accept.setEventHandler { [weak self] in self?.acceptPeer() }; accept.resume(); acceptSource = accept
            let discover = DispatchSource.makeReadSource(fileDescriptor: discovery, queue: queue)
            discover.setEventHandler { [weak self] in self?.discover() }; discover.resume(); discoverySource = discover
            let video = DispatchSource.makeReadSource(fileDescriptor: udp, queue: queue)
            video.setEventHandler { [weak self] in self?.videoProbe() }; video.resume(); videoSource = video
            let timer = DispatchSource.makeTimerSource(queue: queue)
            timer.schedule(deadline: .now() + .milliseconds(10), repeating: .milliseconds(10))
            timer.setEventHandler { [weak self] in self?.tick() }; timer.resume(); self.timer = timer
            log("Listening on \(ip):\(port); waiting for a paired Windows client")
        } catch { for fd in [listener, udp, discovery] where fd >= 0 { close(fd) }; throw error }
    }
    private func makeSocket(type: Int32, port: UInt16, bindIP: String? = nil) throws -> Int32 {
        let fd = socket(AF_INET, type, 0)
        guard fd >= 0 else { throw HostError("socket: \(errno)") }
        do {
            try nonblocking(fd); var yes: Int32 = 1
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, 4)
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, 4)
            var a = try address(bindIP ?? ip, port: port)
            guard withAddress(&a, { Darwin.bind(fd, $0, $1) }) == 0 else { throw HostError("Cannot bind \(ip):\(port): \(String(cString: strerror(errno)))") }
            return fd
        } catch { close(fd); throw error }
    }
    private func acceptPeer() {
        var a = sockaddr_in(), size = socklen_t(MemoryLayout<sockaddr_in>.size)
        let fd = withUnsafeMutablePointer(to: &a) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { accept(listener, $0, &size) } }
        guard fd >= 0 else { return }
        // Direct-link mode accepts only the subnet of the selected local interface.
        guard (!restrictToLocalSubnet && requirePairing) || sameSubnet(a.sin_addr, localIP: ip) else { close(fd); return }
        // One controller only; do not displace an existing connection.
        guard peer == nil else { close(fd); return }
        do { try nonblocking(fd) } catch { close(fd); return }
        var yes: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, 4); setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, 4)
        let p = Peer(fd: fd, address: a); peer = p
        let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler { [weak self, weak p] in if let p { self?.read(p) } }
        source.setCancelHandler { close(fd) }; p.source = source; source.resume()
    }
    private func read(_ p: Peer) {
        var buffer = [UInt8](repeating: 0, count: 8192)
        for _ in 0..<8 {
            let n = recv(p.fd, &buffer, buffer.count, 0)
            if n == 0 { disconnect(p, "Client disconnected"); return }
            if n < 0 {
                if errno != EAGAIN && errno != EWOULDBLOCK { disconnect(p, "TCP receive failed") }
                return
            }
            do {
                let messages = try p.framer.append(Data(buffer.prefix(n)))
                for m in messages { try process(m, p) ; if peer !== p { return } }
            } catch { disconnect(p, "Invalid control packet"); return }
        }
    }
    private func process(_ data: Data, _ p: Peer) throws {
        guard let kind = data.first.flatMap(Message.init(rawValue:)) else { throw WireError.malformed }
        if !p.negotiated {
            if kind == .capabilityQuery {
                guard (data.count == 1 || (data.count == 2 && (2...7).contains(data[data.startIndex + 1])) ||
                    (data.count == 3 && (4...7).contains(data[data.startIndex + 1]) && data[data.startIndex + 2] <= 1)), var capabilities = displayCapabilities else { throw WireError.malformed }
                p.extendedFeatures = data.count >= 2 && data[data.startIndex + 1] >= 3
                p.highBitrate = data.count >= 2 && data[data.startIndex + 1] >= 4
                p.richFeatures = data.count >= 2 && data[data.startIndex + 1] >= 5
                p.desktopSRGB = data.count >= 2 && data[data.startIndex + 1] >= 6
                p.cursorVariants = data.count >= 2 && data[data.startIndex + 1] >= 7
                p.localCursor = p.richFeatures && localCursorAvailable && data.count == 3 && data[data.startIndex + 2] == 1
                if capabilities.count >= 28 { capabilities[25] = (capabilities[25] & 3) | (p.extendedFeatures ? 12 : 0) | (p.highBitrate ? 16 : 0) | (localCursorAvailable && p.richFeatures ? 32 : 0) | (p.richFeatures ? 64 : 0) | (p.desktopSRGB ? 128 : 0) }
                // Legacy clients parse only the original 8-bit capability payload.
                if data.count == 1 && capabilities.count >= 28 { capabilities[23] = 3; capabilities[24] = 8 }
                send(capabilities, p); return
            }
            let hello = try Hello(data)
            guard !hello.wide || p.richFeatures else { throw WireError.malformed }
            guard hello.height <= 2304 || p.extendedFeatures else { throw WireError.malformed }
            guard hello.bitrate <= 300_000_000 || p.highBitrate else { fail(p, "High bitrate requires capability query version 4. Update both platforms to 0.6.2."); return }
            guard ProtocolWire.acceptsToken(hello.token, expected: token, required: requirePairing) else {
                fail(p, "PairingRequired: This Mac requires a pairing code. Enable pairing on the Windows client and enter the Mac code."); return
            }
            p.negotiated = true; p.lastSeen = DispatchTime.now().uptimeNanoseconds
            p.session = UInt64.random(in: 1 ... UInt64.max)
            var target = p.address; target.sin_port = hello.udpPort.bigEndian; p.videoAddress = target
            var selected: CaptureEngine?
            if hello.codecMask & 4 != 0 { selected = try? CaptureEngine(hello: hello, codec: .hevc10, queue: queue, cursorVisible: !p.localCursor, desktopSRGB: p.desktopSRGB) }
            if selected == nil && hello.codecMask & 2 != 0 { selected = try? CaptureEngine(hello: hello, codec: .hevc, queue: queue, cursorVisible: !p.localCursor, desktopSRGB: p.desktopSRGB) }
            if selected == nil && hello.codecMask & 1 != 0 { selected = try? CaptureEngine(hello: hello, codec: .h264, queue: queue, cursorVisible: !p.localCursor, desktopSRGB: p.desktopSRGB) }
            guard let engine = selected else { fail(p, p.desktopSRGB ? "DesktopColorUnavailable: No compatible sRGB hardware encoder" : "No compatible hardware encoder. Try H.264 or a lower mode."); return }
            p.engine = engine
            engine.onFrame = { [weak self, weak p] data, pts, key in
                guard let self, let p, self.peer === p, p.ready else { return false }
                return self.sendVideo(data, pts: pts, key: key, peer: p)
            }
            engine.onFailure = { [weak self, weak p] reason in
                guard let self, let p, self.peer === p else { return }
                self.onCaptureFailure?(reason); self.fail(p, reason)
            }
            Task { [self, p, engine] in
                do {
                    let id = try await engine.start(displayID: display)
                    queue.async { [weak self, weak p] in
                        guard let self, let p, self.peer === p else { engine.stop(); return }
                        p.injector = InputInjector(display: id, captureSize: CGSize(width: Int(hello.width), height: Int(hello.height)), contentRect: engine.contentRect)
                        self.send(ProtocolWire.welcome(session: p.session, codec: engine.codec, hello: hello, acceptedBitrate: engine.effectiveBitrate), p)
                        log("Paired \(ipString(p.address.sin_addr)); \(hello.width)x\(hello.height) target \(hello.fps) fps, \(engine.codec), \(engine.effectiveBitrate / 1_000_000) Mbps")
                        self.onStatus?("Connected: \(hello.width)×\(hello.height) / \(hello.fps) Hz · \(engine.codec == .hevc10 ? "HEVC Main10 / SDR 10-bit" : engine.codec == .hevc ? "HEVC / SDR 8-bit" : "H.264 / SDR 8-bit")")
                    }
                } catch { queue.async { [weak self, weak p] in if let p, self?.peer === p { if engine.codec == .hevc10 && CGPreflightScreenCaptureAccess() { self?.fail(p, "Main10Unavailable: Screen capture: \(error.localizedDescription)") }
                        else { self?.onCaptureFailure?(error.localizedDescription); self?.fail(p, "Screen capture: \(error.localizedDescription)") } } } }
            }
            return
        }
        p.lastSeen = DispatchTime.now().uptimeNanoseconds
        switch kind {
        case .ready:
            guard data.count == 1, p.injector != nil else { throw WireError.malformed }
            p.ready = true; p.engine?.active = true; p.engine?.requestIDR()
            onCursorState?(p.session, p.localCursor && p.richFeatures, p.cursorVariants)
        case .input:
            guard p.ready else { throw WireError.malformed }
            let input = try Input(data)
            if inputAllowed?() ?? true { p.injector?.apply(input) }
        case .requestIDR:
            guard data.count == 1 else { throw WireError.malformed }; p.engine?.requestIDR()
        case .ping:
            guard data.count == 1 else { throw WireError.malformed }; send(Data([Message.pong.rawValue]), p)
        case .clipboardControl:
            guard p.ready, p.extendedFeatures, data.count == 2, data[data.startIndex + 1] <= 1 else { throw WireError.malformed }
            p.clipboardRequested = data[data.startIndex + 1] == 1
            configureClipboard(p, enabled: p.clipboardRequested && allowClipboard)
        case .clipboardText:
            guard p.ready, p.extendedFeatures else { throw WireError.malformed }
            guard p.clipboard else { return } // A disable acknowledgment may cross in-flight text.
            if let text = try p.clipboardInput.append(data) { onClipboardText?(p.session, text) }
        case .clipboardImage:
            guard p.ready, p.richFeatures else { throw WireError.malformed }
            guard p.clipboard else { return }
            if let image = try p.imageInput.append(data, kind: .clipboardImage, limit: BinaryWire.imageLimit) { onClipboardImage?(p.session, image) }
        default: throw WireError.malformed
        }
    }
    func sendClipboard(_ text: String, session: UInt64) {
        queue.async { [weak self] in
            guard let self, let p = self.peer, p.session == session, p.ready, p.clipboard else { return }
            p.clipboardID &+= 1; if p.clipboardID == 0 { p.clipboardID = 1 }
            // Replace stale clipboard work, without placing large transfers in the input channel's queue.
            p.clipboardOutput = TextClipboardWire.packets(text, id: p.clipboardID)
        }
    }
    func sendImage(_ image: Data, session: UInt64) {
        queue.async { [weak self] in
            guard let self, let p = self.peer, p.session == session, p.ready, p.clipboard, p.richFeatures else { return }
            p.clipboardID &+= 1; if p.clipboardID == 0 { p.clipboardID = 1 }
            p.clipboardOutput = BinaryWire.packets(image, kind: .clipboardImage, id: p.clipboardID, limit: BinaryWire.imageLimit)
        }
    }
    func sendCursor(_ image: Data, session: UInt64) {
        queue.async { [weak self] in
            guard let self, let p = self.peer, p.session == session, p.ready, p.localCursor, p.richFeatures else { return }
            p.cursorID &+= 1; if p.cursorID == 0 { p.cursorID = 1 }
            p.cursorOutput = BinaryWire.packets(image, kind: .cursorImage, id: p.cursorID, limit: BinaryWire.cursorLimit)
        }
    }
    private func send(_ message: Data, _ p: Peer) {
        guard peer === p else { return }
        p.output.append(ProtocolWire.frame(message))
        if p.output.count > 65536 { disconnect(p, "Control channel stalled"); return }
        flush(p)
    }
    private func flush(_ p: Peer) {
        while !p.output.isEmpty {
            let n = p.output.withUnsafeBytes { Darwin.send(p.fd, $0.baseAddress!, $0.count, 0) }
            if n <= 0 {
                if n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) { return }
                disconnect(p, "TCP send failed"); return
            }
            p.output = Data(p.output.dropFirst(n))
        }
    }
    private func sendVideo(_ frame: Data, pts: UInt64, key: Bool, peer p: Peer) -> Bool {
        let limit = p.richFeatures ? ProtocolWire.maxFrameSize : p.highBitrate ? ProtocolWire.gigabitMaxFrameSize : ProtocolWire.legacyMaxFrameSize
        guard !frame.isEmpty, frame.count <= limit, var target = p.videoAddress, let codec = p.engine?.codec else { return false }
        p.frame &+= 1
        let count = (frame.count + ProtocolWire.fragmentSize - 1) / ProtocolWire.fragmentSize
        for index in 0..<count {
            let packet = ProtocolWire.packet(frame: frame, session: p.session, id: p.frame, pts: pts, codec: codec, key: key, index: index)
            let n = packet.withUnsafeBytes { bytes in withAddress(&target) { sendto(udp, bytes.baseAddress!, bytes.count, 0, $0, $1) } }
            if n != packet.count {
                if key { log("IDR UDP send failed at fragment \(index)/\(count): \(String(cString: strerror(errno)))") }
                return false // Do not queue a stale compressed frame.
            }
        }
        return true
    }
    private func tick() {
        guard let p = peer else { return }
        let now = DispatchTime.now().uptimeNanoseconds
        if (!p.ready && now - p.accepted > 15_000_000_000) || now - p.lastSeen > 10_000_000_000 { disconnect(p, "Client timeout"); return }
        flush(p)
        for _ in 0..<16 {
            guard p.output.count < 16384, !p.cursorOutput.isEmpty else { break }
            send(p.cursorOutput.removeFirst(), p)
        }
        if p.clipboard, p.output.count < 4096, !p.clipboardOutput.isEmpty {
            send(p.clipboardOutput.removeFirst(), p)
        }
    }
    private func fail(_ p: Peer, _ reason: String) {
        guard peer === p else { return }
        log(reason); send(Data([Message.failure.rawValue]) + Data(reason.prefix(1000).utf8), p)
        // Give the tiny failure message a short bounded chance to reach the client.
        queue.asyncAfter(deadline: .now() + .milliseconds(100)) { [weak self, weak p] in if let p { self?.disconnect(p, reason) } }
    }
    private func disconnect(_ p: Peer, _ reason: String) {
        guard peer === p else { return }
        p.injector?.releaseAll(); p.engine?.stop(); p.engine = nil
        onClipboardState?(p.session, false)
        onCursorState?(p.session, false, p.cursorVariants)
        shutdown(p.fd, SHUT_RDWR); p.source?.cancel(); p.source = nil; peer = nil
        log(reason); onStatus?("Waiting for client")
    }
    private func discover() {
        var bytes = [UInt8](repeating: 0, count: 64), sender = sockaddr_in(), length = socklen_t(MemoryLayout<sockaddr_in>.size)
        let n = withUnsafeMutablePointer(to: &sender) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { recvfrom(discovery, &bytes, bytes.count, 0, $0, &length) } }
        guard n > 0 else { return }
        let payload = Data(bytes.prefix(n))
        // Reply from the queried socket first. The client then sends from its video
        // socket to our video port, establishing the matching UDP response path.
        if let p = peer, validProbe(payload, sender: sender, peer: p) {
            let reply = Data("TDVIDEO1 \(p.session) \(videoPort)".utf8)
            _ = reply.withUnsafeBytes { data in withAddress(&sender) { sendto(discovery, data.baseAddress!, data.count, 0, $0, $1) } }
            return
        }
        guard n == 8, payload == Data("TDDISC1?".utf8), sameSubnet(sender.sin_addr, localIP: ip) else { return }
        let reply = Data("TDHOST1 \(port)".utf8)
        _ = reply.withUnsafeBytes { data in withAddress(&sender) { sendto(udp, data.baseAddress!, data.count, 0, $0, $1) } }
    }
    private func validProbe(_ data: Data, sender: sockaddr_in, peer p: Peer) -> Bool {
        p.ready && sender.sin_addr.s_addr == p.address.sin_addr.s_addr &&
            sender.sin_port == p.videoAddress?.sin_port && data == Data("TDVIDEO1 \(p.session)".utf8)
    }
    private func videoProbe() {
        for _ in 0..<8 {
            var bytes = [UInt8](repeating: 0, count: 64), sender = sockaddr_in(), length = socklen_t(MemoryLayout<sockaddr_in>.size)
            let n = withUnsafeMutablePointer(to: &sender) { p in
                p.withMemoryRebound(to: sockaddr.self, capacity: 1) { recvfrom(udp, &bytes, bytes.count, 0, $0, &length) }
            }
            guard n > 0 else { return }
            guard let p = peer, validProbe(Data(bytes.prefix(n)), sender: sender, peer: p) else { continue }
            // Repeat the IDR after the return path is open, including on a static desktop.
            p.engine?.requestIDR()
        }
    }
    func stop() {
        queue.sync {
            if let p = peer { disconnect(p, "Host stopped") }
            timer?.cancel(); timer = nil
            let l = listener, d = discovery
            acceptSource?.setCancelHandler { close(l) }; acceptSource?.cancel(); acceptSource = nil
            discoverySource?.setCancelHandler { close(d) }; discoverySource?.cancel(); discoverySource = nil
            let u = udp
            videoSource?.setCancelHandler { close(u) }; videoSource?.cancel(); videoSource = nil; udp = -1
        }
    }
}
