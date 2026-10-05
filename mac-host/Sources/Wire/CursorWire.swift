import Foundation

public struct CursorPayload {
    public let width: UInt32, height: UInt32
    public let hotX: Int32, hotY: Int32
    public let images: [Data]
    public init(_ data: Data) throws {
        guard data.count <= BinaryWire.cursorLimit else { throw WireError.malformed }
        var r = Reader(data)
        let version = try r.get(UInt8.self)
        guard version == 1 || version == 2 else { throw WireError.malformed }
        width = try r.get(); height = try r.get(); hotX = try r.get(); hotY = try r.get()
        guard width >= 65536, height >= 65536, width <= 256 * 65536, height <= 256 * 65536,
              hotX >= 0, hotY >= 0, UInt32(hotX) < width, UInt32(hotY) < height else { throw WireError.malformed }
        if version == 1 { images = [try r.bytes(data.count - r.offset)] }
        else {
            let count = try r.get(UInt8.self)
            guard count > 0, count <= 8 else { throw WireError.malformed }
            var decoded = [Data]()
            for _ in 0..<count {
                let size = try r.get(UInt32.self)
                decoded.append(try r.bytes(Int(size)))
            }
            images = decoded
        }
        guard r.atEnd, images.allSatisfy({ $0.starts(with: [0x89, 0x50, 0x4e, 0x47, 13, 10, 26, 10]) }) else { throw WireError.malformed }
    }
    public static func encode(width: UInt32, height: UInt32, hotX: Int32, hotY: Int32, images: [Data], variants: Bool) throws -> Data {
        guard !images.isEmpty, images.count <= 8 else { throw WireError.malformed }
        let selected = variants ? images : [images.last!]
        guard selected.allSatisfy({ $0.count <= BinaryWire.cursorLimit }) else { throw WireError.malformed }
        var w = Writer(); w.put(UInt8(variants ? 2 : 1)); w.put(width); w.put(height); w.put(hotX); w.put(hotY)
        if variants { w.put(UInt8(selected.count)) }
        for image in selected { if variants { w.put(UInt32(image.count)) }; w.bytes(image) }
        _ = try Self(w.data)
        return w.data
    }
}
