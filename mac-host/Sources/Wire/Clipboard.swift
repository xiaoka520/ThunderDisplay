import Foundation

public enum TextClipboardWire {
    public static let limit = 65536, chunkSize = 3072
    public static func packets(_ text: String, id: UInt32) -> [Data] {
        let data = Data(text.utf8)
        guard id != 0, data.count <= limit, !data.contains(0) else { return [] }
        var result: [Data] = [], offset = 0
        repeat {
            let count = min(chunkSize, data.count - offset)
            var w = Writer(); w.put(Message.clipboardText.rawValue); w.put(id)
            w.put(UInt32(data.count)); w.put(UInt32(offset)); w.bytes(data.subdata(in: offset..<(offset + count)))
            result.append(w.data); offset += count
        } while offset < data.count
        return result
    }
}

public struct TextClipboardAssembler {
    private var id: UInt32 = 0, total = 0, pending = Data()
    public init() {}
    public mutating func append(_ data: Data) throws -> String? {
        var r = Reader(data)
        guard try r.get(UInt8.self) == Message.clipboardText.rawValue else { throw WireError.malformed }
        let transfer = try r.get(UInt32.self), size = Int(try r.get(UInt32.self)), offset = Int(try r.get(UInt32.self))
        let count = data.count - r.offset
        guard transfer != 0, size <= TextClipboardWire.limit, count <= TextClipboardWire.chunkSize,
              offset <= size, count <= size - offset, count > 0 || size == 0 else { throw WireError.malformed }
        if offset == 0 { id = transfer; total = size; pending = Data() }
        guard transfer == id, size == total, offset == pending.count else { throw WireError.malformed }
        pending.append(try r.bytes(count))
        guard pending.count == total else { return nil }
        guard !pending.contains(0), let text = String(data: pending, encoding: .utf8) else { throw WireError.malformed }
        pending = Data(); id = 0; total = 0
        return text
    }
}
