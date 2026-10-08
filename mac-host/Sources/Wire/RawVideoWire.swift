import Foundation
import RawPixelSupport

public enum RawVideoWire {
    // Fixed 128 x 64 luma tiles; corresponding interleaved chroma has 32 rows.
    // Every comparison is byte-exact. Deltas refer to the last transmitted
    // snapshot, so dropping a capture before transmission cannot break the chain.
    public static func update(_ frame: Data, baseline: Data?, baseID: UInt32, width: Int, height: Int) throws -> Data {
        let expected = try byteCount(width: width, height: height, packed: true)
        guard frame.count == expected, baseline == nil || baseline?.count == expected,
              (baseline == nil) == (baseID == 0) else { throw WireError.malformed }
        let columns = (width + 127) / 128, rows = (height + 63) / 64, stride = (width * 10 + 7) / 8
        var changed = [Int](), payloadSize = 16
        if let baseline {
            frame.withUnsafeBytes { current in baseline.withUnsafeBytes { previous in
                for index in 0..<(columns * rows) {
                    let x = index % columns * 128, y = index / columns * 64
                    let w = min(128, width - x), h = min(64, height - y), bytes = (w * 10 + 7) / 8
                    var differs = false
                    for (first, count) in [(y, h), (height + y / 2, h / 2)] {
                        for row in first..<(first + count) {
                            let offset = row * stride + x * 10 / 8
                            if memcmp(current.baseAddress!.advanced(by: offset), previous.baseAddress!.advanced(by: offset), bytes) != 0 { differs = true; break }
                        }
                        if differs { break }
                    }
                    if differs { changed.append(index); payloadSize += 2 + bytes * (h + h / 2) }
                }
            } }
        }
        let full = baseline == nil || payloadSize >= expected + 16
        var result = Data(); result.reserveCapacity(full ? expected + 16 : payloadSize)
        func put<T: FixedWidthInteger>(_ value: T) {
            var network = value.bigEndian
            withUnsafeBytes(of: &network) { result.append(contentsOf: $0) }
        }
        put(UInt32(0x54444431)); put(full ? UInt32(0) : baseID)
        put(UInt16(full ? 0 : 1)); put(UInt16(0)); put(UInt32(full ? 0 : changed.count))
        if full { result.append(frame); return result }
        frame.withUnsafeBytes { pixels in
            for index in changed {
                put(UInt16(index))
                let x = index % columns * 128, y = index / columns * 64
                let w = min(128, width - x), h = min(64, height - y), bytes = (w * 10 + 7) / 8
                for (first, count) in [(y, h), (height + y / 2, h / 2)] {
                    for row in first..<(first + count) {
                        let offset = row * stride + x * 10 / 8
                        result.append(pixels.bindMemory(to: UInt8.self).baseAddress!.advanced(by: offset), count: bytes)
                    }
                }
            }
        }
        return result
    }
    public static func byteCount(width: Int, height: Int, packed: Bool = false) throws -> Int {
        guard (320...4096).contains(width), (240...4096).contains(height), width % 2 == 0, height % 2 == 0 else { throw WireError.malformed }
        return packed ? ((width * 10 + 7) / 8) * (height + height / 2) : width * height * 3
    }
    public static func handshake(session: UInt64) -> Data {
        var w = Writer(); w.put(UInt32(0x54445241)); w.put(session); return w.data
    }
    public static func endpoint(session: UInt64, port: UInt16) -> Data {
        var w = Writer(); w.put(Message.rawVideoEndpoint.rawValue); w.put(session); w.put(port); return w.data
    }
    public static func header(id: UInt32, size: Int, pts: UInt64) -> Data {
        var w = Writer(); w.put(UInt32(0x54445246)); w.put(id); w.put(UInt32(size)); w.put(pts); return w.data
    }
    /// Strip row padding and clear the six unused bits of each P010 word.
    /// All ten meaningful bits pass through unchanged, without quantization.
    public static func pack(width: Int, height: Int, luma: UnsafeRawBufferPointer, lumaStride: Int,
                            chroma: UnsafeRawBufferPointer, chromaStride: Int, packed: Bool = false) throws -> Data {
        let count = try byteCount(width: width, height: height, packed: packed), row = width * 2
        guard lumaStride >= row, chromaStride >= row, lumaStride % 2 == 0, chromaStride % 2 == 0,
              lumaStride <= luma.count / height, chromaStride <= chroma.count / (height / 2),
              let y = luma.baseAddress, let uv = chroma.baseAddress else { throw WireError.malformed }
        let storage = UnsafeMutableRawPointer.allocate(byteCount: count, alignment: 64)
        let rowBytes = packed ? (width * 10 + 7) / 8 : width * 2
        td_raw_pack_plane(y, lumaStride, width, height, storage, packed ? 1 : 0)
        td_raw_pack_plane(uv, chromaStride, width, height / 2, storage.advanced(by: height * rowBytes), packed ? 1 : 0)
        return Data(bytesNoCopy: storage, count: count, deallocator: .custom { pointer, _ in pointer.deallocate() })
    }
}
