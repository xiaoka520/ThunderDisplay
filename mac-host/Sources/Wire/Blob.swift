import Foundation
public enum BinaryWire {
    public static let imageLimit = 32 * 1024 * 1024, cursorLimit = 512 * 1024, chunkSize = 3072
    public static func packets(_ data: Data, kind: Message, id: UInt32, limit: Int) -> [Data] {
        guard id != 0, !data.isEmpty, data.count <= limit else { return [] }
        return stride(from: 0, to: data.count, by: chunkSize).map { offset in
            var w = Writer(); w.put(kind.rawValue); w.put(id); w.put(UInt32(data.count)); w.put(UInt32(offset))
            w.bytes(data.subdata(in: offset..<min(data.count, offset + chunkSize))); return w.data
        }
    }
}
public struct BinaryAssembler {
    private var id: UInt32 = 0, total = 0, pending = Data()
    public init() {}
    public mutating func append(_ data: Data, kind: Message, limit: Int) throws -> Data? {
        var r = Reader(data)
        guard try r.get(UInt8.self) == kind.rawValue else { throw WireError.malformed }
        let transfer = try r.get(UInt32.self), size = Int(try r.get(UInt32.self)), offset = Int(try r.get(UInt32.self))
        let count = data.count - r.offset
        guard transfer != 0, size > 0, size <= limit, count > 0, count <= BinaryWire.chunkSize,
            offset <= size, count <= size - offset else { throw WireError.malformed }
        if offset == 0 { id = transfer; total = size; pending = Data() }
        guard transfer == id, size == total, offset == pending.count else { throw WireError.malformed }
        pending.append(try r.bytes(count))
        guard pending.count == total else { return nil }
        let complete = pending; self = BinaryAssembler(); return complete
    }
}
