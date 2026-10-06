import Foundation

public enum WireError: Error { case malformed }

public struct Writer {
    public private(set) var data = Data()
    public init() {}
    public mutating func put<T: FixedWidthInteger>(_ value: T) {
        var v = value.bigEndian
        withUnsafeBytes(of: &v) { data.append(contentsOf: $0) }
    }
    public mutating func bytes(_ value: Data) { data.append(value) }
}

public struct Reader {
    private let data: Data
    public private(set) var offset = 0
    public init(_ data: Data) { self.data = Data(data) }
    public mutating func get<T: FixedWidthInteger>(_ type: T.Type = T.self) throws -> T {
        guard offset + MemoryLayout<T>.size <= data.count else { throw WireError.malformed }
        var v: T = 0
        for byte in data[offset..<(offset + MemoryLayout<T>.size)] { v = (v << 8) | T(truncatingIfNeeded: byte) }
        offset += MemoryLayout<T>.size
        return v
    }
    public mutating func bytes(_ count: Int) throws -> Data {
        guard count >= 0, offset + count <= data.count else { throw WireError.malformed }
        defer { offset += count }
        return data.subdata(in: offset..<(offset + count))
    }
    public var atEnd: Bool { offset == data.count }
}

public enum Message: UInt8, Sendable { case hello = 1, welcome, input, requestIDR, ping, pong, failure, ready, capabilityQuery, capabilities, clipboardControl, clipboardText, cursorImage, clipboardImage, helloWide, welcomeWide, sessionTransition, sessionTransitionAck }

/// A session-scoped notice, accepted only after pairing and stream negotiation.
public enum SessionTransitionWire {
    public static func packet(session: UInt64, acknowledgment: Bool = false) -> Data {
        var w = Writer(); w.put((acknowledgment ? Message.sessionTransitionAck : .sessionTransition).rawValue)
        w.put(session); w.put(UInt8(1)); return w.data // 1 = LoginWindow -> desktop
    }
    public static func matches(_ data: Data, session: UInt64, acknowledgment: Bool = false) -> Bool {
        guard session != 0 else { return false }
        return data == packet(session: session, acknowledgment: acknowledgment)
    }
}
public enum Codec: UInt8, Sendable { case h264 = 1, hevc = 2, hevc10 = 4 }

public struct Hello: Sendable {
    public let udpPort: UInt16, width: UInt16, height: UInt16, fps: UInt16
    public let bitrate: UInt64, codecMask: UInt8, token: Data
    public let wide: Bool
    public init(_ data: Data) throws {
        var r = Reader(data)
        let type = try r.get(UInt8.self); wide = type == Message.helloWide.rawValue
        guard (wide || type == Message.hello.rawValue), try r.get(UInt16.self) == (wide ? 2 : 1) else { throw WireError.malformed }
        udpPort = try r.get(); width = try r.get(); height = try r.get(); fps = try r.get()
        bitrate = wide ? try r.get(UInt64.self) : UInt64(try r.get(UInt32.self)); codecMask = try r.get(); token = try r.bytes(32)
        guard r.atEnd, udpPort > 0, width >= 320, width <= 4096, height >= 240, height <= 4096,
              width % 2 == 0, height % 2 == 0, fps >= 1, fps <= 240,
              bitrate >= 10_000_000, bitrate <= (wide ? 20_000_000_000 : 1_000_000_000), codecMask & 7 != 0, codecMask & ~7 == 0
        else { throw WireError.malformed }
    }
}

public struct Input: Sendable {
    public let kind: UInt8, code: UInt16, flags: UInt16, x: Int32, y: Int32
    public init(_ data: Data) throws {
        var r = Reader(data)
        guard try r.get(UInt8.self) == Message.input.rawValue else { throw WireError.malformed }
        kind = try r.get(); code = try r.get(); flags = try r.get(); x = try r.get(); y = try r.get()
        guard r.atEnd, (1...5).contains(kind), flags & ~63 == 0 else { throw WireError.malformed }
    }
}

public enum ProtocolWire {
    public static let controlLimit = 4096
    public static let fragmentSize = 1160
    public static let legacyMaxFrameSize = 4 * 1024 * 1024
    public static let gigabitMaxFrameSize = 16 * 1024 * 1024
    public static let maxFrameSize = 64 * 1024 * 1024
    public static func capabilities(width: UInt32, height: UInt32, hz: UInt16, maximumWidth: UInt32,
                                     maximumHeight: UInt32, maximumHz: UInt16, flags: UInt8, name: String, codecMask: UInt8 = 3, streamBits: UInt8 = 8) -> Data {
        var w = Writer(); w.put(Message.capabilities.rawValue); w.put(UInt16(1))
        w.put(width); w.put(height); w.put(hz); w.put(maximumWidth); w.put(maximumHeight); w.put(maximumHz)
        w.put(codecMask); w.put(streamBits); w.put(flags)
        var nameBytes = Data(name.utf8.prefix(256))
        while String(data: nameBytes, encoding: .utf8) == nil { nameBytes.removeLast() }
        w.put(UInt16(nameBytes.count)); w.bytes(nameBytes); return w.data
    }
    public static func frame(_ payload: Data) -> Data {
        var w = Writer(); w.put(UInt32(payload.count)); w.bytes(payload); return w.data
    }
    public static func welcome(session: UInt64, codec: Codec, hello: Hello, acceptedBitrate: UInt64? = nil) -> Data {
        var w = Writer(); w.put(hello.wide ? Message.welcomeWide.rawValue : Message.welcome.rawValue); w.put(UInt16(hello.wide ? 2 : 1)); w.put(session); w.put(codec.rawValue)
        w.put(hello.width); w.put(hello.height); w.put(hello.fps)
        let bitrate = acceptedBitrate ?? hello.bitrate
        if hello.wide { w.put(bitrate) } else { w.put(UInt32(bitrate)) }; return w.data
    }
    public static func packet(frame: Data, session: UInt64, id: UInt32, pts: UInt64,
                              codec: Codec, key: Bool, index: Int) -> Data {
        let count = (frame.count + fragmentSize - 1) / fragmentSize
        precondition(!frame.isEmpty && frame.count <= maxFrameSize && index >= 0 && index < count)
        let start = index * fragmentSize, length = min(fragmentSize, frame.count - start)
        var w = Writer(); w.put(UInt32(0x54444231)); w.put(UInt8(1)); w.put(codec.rawValue); w.put(UInt16(key ? 1 : 0))
        w.put(session); w.put(id); w.put(pts); w.put(UInt32(frame.count)); w.put(UInt16(index)); w.put(UInt16(count))
        w.put(UInt16(length)); w.put(UInt16(0)); w.bytes(frame.subdata(in: start..<(start + length))); return w.data
    }
    public static func matchesToken(_ a: Data, _ b: Data) -> Bool {
        guard a.count == 32, b.count == 32 else { return false }
        return zip(a, b).reduce(UInt8(0)) { $0 | ($1.0 ^ $1.1) } == 0
    }
    public static func acceptsToken(_ received: Data, expected: Data, required: Bool) -> Bool {
        guard received.count == 32 else { return false }
        return !required || matchesToken(received, expected)
    }
}

/// Handles TCP fragmentation/coalescing without trusting the peer's length prefix.
public struct ControlFramer {
    private var pending = Data()
    public init() {}
    public mutating func append(_ data: Data) throws -> [Data] {
        pending.append(data)
        var result: [Data] = []
        while pending.count >= 4 {
            var reader = Reader(pending); let count = Int(try reader.get(UInt32.self))
            guard count > 0, count <= ProtocolWire.controlLimit else { throw WireError.malformed }
            guard pending.count >= count + 4 else { break }
            result.append(pending.subdata(in: 4..<(count + 4)))
            pending = Data(pending.dropFirst(count + 4))
        }
        guard pending.count <= ProtocolWire.controlLimit + 4 else { throw WireError.malformed }
        return result
    }
}
