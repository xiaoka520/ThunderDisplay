import XCTest
@testable import Wire

final class RawVideoTests: XCTestCase {
    func testRawHandshakeHeaderAndEndpointGoldenBytes() throws {
        let session: UInt64 = 0x0102030405060708
        XCTAssertEqual(Array(RawVideoWire.handshake(session: session)), [0x54,0x44,0x52,0x41,1,2,3,4,5,6,7,8])
        XCTAssertEqual(Array(RawVideoWire.endpoint(session: session, port: 50000)), [22,1,2,3,4,5,6,7,8,0xC3,0x50])
        XCTAssertEqual(Array(RawVideoWire.header(id: 1, size: 230400, pts: 2)), [0x54,0x44,0x52,0x46,0,0,0,1,0,3,0x84,0,0,0,0,0,0,0,0,2])
        XCTAssertEqual(Array(CursorPositionWire.packet(session: session, visible: true, x: 0x1234, y: 65535)), [21,1,2,3,4,5,6,7,8,1,0x12,0x34,0xff,0xff])
        XCTAssertEqual(Array(CursorPositionWire.packet(session: session, visible: false, x: 123, y: 456)).suffix(5), [0,0,0,0,0])
    }
    func testP010PackingStripsPaddingWithoutChangingTenBitWords() throws {
        let width = 320, height = 240, stride = width * 2 + 64
        var y = Data(repeating: 0xAA, count: stride * height), uv = Data(repeating: 0xAA, count: stride * height / 2)
        for row in 0..<height { for x in 0..<width {
            let word = UInt16((x + row) % 1024) << 6
            y[row * stride + x * 2] = UInt8(truncatingIfNeeded: word); y[row * stride + x * 2 + 1] = UInt8(word >> 8)
        } }
        for row in 0..<(height / 2) { for x in 0..<width {
            let word = UInt16((x + row + 512) % 1024) << 6
            uv[row * stride + x * 2] = UInt8(truncatingIfNeeded: word); uv[row * stride + x * 2 + 1] = UInt8(word >> 8)
        } }
        let packed = try y.withUnsafeBytes { y in try uv.withUnsafeBytes { uv in
            try RawVideoWire.pack(width: width, height: height, luma: y, lumaStride: stride, chroma: uv, chromaStride: stride)
        } }
        XCTAssertEqual(packed.count, width * height * 3)
        var noisyY = y, noisyUV = uv; noisyY[0] |= 63; noisyUV[0] |= 63
        let canonical = try noisyY.withUnsafeBytes { y in try noisyUV.withUnsafeBytes { uv in
            try RawVideoWire.pack(width: width, height: height, luma: y, lumaStride: stride, chroma: uv, chromaStride: stride)
        } }
        XCTAssertEqual(canonical, packed, "CoreVideo's unused low bits must not change a transmitted ten-bit value")
        for row in 0..<height { XCTAssertEqual(packed.subdata(in: row * width * 2..<(row + 1) * width * 2), y.subdata(in: row * stride..<row * stride + width * 2)) }
        for row in 0..<(height / 2) {
            XCTAssertEqual(packed.subdata(in: (height + row) * width * 2..<(height + row + 1) * width * 2), uv.subdata(in: row * stride..<row * stride + width * 2))
        }
        XCTAssertThrowsError(try y.withUnsafeBytes { y in try uv.withUnsafeBytes { uv in
            try RawVideoWire.pack(width: width, height: height, luma: y, lumaStride: 639, chroma: uv, chromaStride: stride)
        } })
        XCTAssertEqual(try RawVideoWire.byteCount(width: 4096, height: 2560), 31_457_280)
        XCTAssertThrowsError(try RawVideoWire.byteCount(width: 4095, height: 2560))
    }
    func testRawHelloRequiresWideVersionAndAnExclusiveRawMask() throws {
        func hello(wide: Bool, mask: UInt8) -> Data {
            var w = Writer(); w.put((wide ? Message.helloWide : .hello).rawValue); w.put(UInt16(wide ? 2 : 1)); w.put(UInt16(50000))
            w.put(UInt16(4096)); w.put(UInt16(2560)); w.put(UInt16(60))
            if wide { w.put(UInt64(15_099_494_400)) } else { w.put(UInt32(120_000_000)) }
            w.put(mask); w.bytes(Data(repeating: 0, count: 32)); return w.data
        }
        XCTAssertEqual(try Hello(hello(wide: true, mask: 8)).codecMask, 8)
        XCTAssertEqual(try Hello(hello(wide: true, mask: 16)).codecMask, 16)
        XCTAssertEqual(try Hello(hello(wide: true, mask: 32)).codecMask, 32)
        XCTAssertThrowsError(try Hello(hello(wide: false, mask: 32)))
        XCTAssertThrowsError(try Hello(hello(wide: true, mask: 48)))
        XCTAssertThrowsError(try Hello(hello(wide: false, mask: 8)))
        XCTAssertThrowsError(try Hello(hello(wide: false, mask: 16)))
        XCTAssertThrowsError(try Hello(hello(wide: true, mask: 12)))
        XCTAssertThrowsError(try Hello(hello(wide: true, mask: 24)))
        XCTAssertThrowsError(try Hello(hello(wide: true, mask: 20)))
    }
    func testExactUpdatesUseOnlyTheTransmittedBaselineAndPreserveEdgeChroma() throws {
        for width in [320, 322, 4096] {
            let height = 240, count = try RawVideoWire.byteCount(width: width, height: height, packed: true)
            let blank = Data(repeating: 0, count: count)
            let full = try RawVideoWire.update(blank, baseline: nil, baseID: 0, width: width, height: height)
            XCTAssertEqual(Array(full.prefix(16)), [0x54,0x44,0x44,0x31,0,0,0,0,0,0,0,0,0,0,0,0])
            XCTAssertEqual(full.dropFirst(16), blank)
            var next = blank; next[0] = 0x7f
            let delta = try RawVideoWire.update(next, baseline: blank, baseID: 1, width: width, height: height)
            XCTAssertEqual(Array(delta.prefix(18)), [0x54,0x44,0x44,0x31,0,0,0,1,0,1,0,0,0,0,0,1,0,0])
            XCTAssertEqual(delta.count, 16 + 2 + 160 * 96); XCTAssertEqual(delta[18], 0x7f)
            XCTAssertTrue(delta.dropFirst(19).allSatisfy { $0 == 0 })
            var corner = blank; corner[count - 1] = 1 // Skip next: it was never transmitted.
            let skipped = try RawVideoWire.update(corner, baseline: blank, baseID: 1, width: width, height: height)
            let columns = (width + 127) / 128, index = columns * 4 - 1
            XCTAssertEqual(Array(skipped[16..<18]), [UInt8(index >> 8), UInt8(truncatingIfNeeded: index)])
            XCTAssertEqual(skipped.last, 1)
            XCTAssertTrue(skipped[18..<(skipped.count - 1)].allSatisfy { $0 == 0 })
            let unchanged = try RawVideoWire.update(blank, baseline: blank, baseID: 7, width: width, height: height)
            XCTAssertEqual(Array(unchanged), [0x54,0x44,0x44,0x31,0,0,0,7,0,1,0,0,0,0,0,0])
            var allChanged = Data(repeating: 0x11, count: count)
            if width % 4 != 0 { let stride = (width * 10 + 7) / 8; for row in 0..<(height + height / 2) { allChanged[(row + 1) * stride - 1] &= 15 } }
            let dense = try RawVideoWire.update(allChanged, baseline: blank, baseID: 1, width: width, height: height)
            XCTAssertEqual(dense.count, count + 16); XCTAssertEqual(dense.dropFirst(16), allChanged)
            XCTAssertEqual(dense.prefix(16), full.prefix(16))
        }
        XCTAssertThrowsError(try RawVideoWire.update(Data(), baseline: nil, baseID: 0, width: 320, height: 240))
    }
    func testPackedTenBitRowsRetainEveryValueAndExcludePadding() throws {
        XCTAssertEqual(try RawVideoWire.byteCount(width: 4096, height: 2560, packed: true), 19_660_800)
        for width in [320, 322] {
            let height = 240, stride = width * 2 + 14, rows = height + height / 2
            var input = Data(repeating: 0xAA, count: stride * rows)
            for row in 0..<rows { for x in 0..<width {
                let value = UInt16((row * width + x) % 1024) << 6 | 63
                input[row * stride + x * 2] = UInt8(truncatingIfNeeded: value)
                input[row * stride + x * 2 + 1] = UInt8(value >> 8)
            } }
            // Shared Swift/C++ golden samples: 0, 1, 512, 1023.
            for (x, value) in [UInt16(0), 1, 512, 1023].enumerated() {
                input[x * 2] = UInt8(truncatingIfNeeded: value << 6); input[x * 2 + 1] = UInt8(value >> 2)
            }
            let result = try input.withUnsafeBytes { bytes in
                try RawVideoWire.pack(width: width, height: height, luma: bytes, lumaStride: stride,
                    chroma: UnsafeRawBufferPointer(start: bytes.baseAddress!.advanced(by: height * stride), count: height / 2 * stride), chromaStride: stride, packed: true)
            }
            XCTAssertEqual(Array(result.prefix(5)), [0,4,0,224,255])
            let rowBytes = (width * 10 + 7) / 8
            XCTAssertEqual(result.count, rowBytes * rows)
            for row in 0..<rows { for x in 0..<width {
                let bit = x * 10, byte = row * rowBytes + bit / 8
                let value = (UInt16(result[byte]) | UInt16(result[byte + 1]) << 8) >> (bit % 8) & 1023
                let original = (UInt16(input[row * stride + x * 2]) | UInt16(input[row * stride + x * 2 + 1]) << 8) >> 6
                XCTAssertEqual(value, original)
            } }
            if width % 4 != 0 { for row in 0..<rows { XCTAssertEqual(result[(row + 1) * rowBytes - 1] & 0xF0, 0) } }
        }
    }
}
