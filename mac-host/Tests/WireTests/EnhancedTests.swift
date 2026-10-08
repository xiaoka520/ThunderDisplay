import XCTest
@testable import Wire
final class EnhancedTests: XCTestCase {
    func testDetectedLinkRatesAboveTwentyGigabits() throws {
        for rate in [UInt64(40_000_000_000), 80_000_000_000, 120_000_000_000] {
            var w = Writer(); w.put(Message.helloWide.rawValue); w.put(UInt16(2)); w.put(UInt16(50000))
            w.put(UInt16(2560)); w.put(UInt16(1600)); w.put(UInt16(60)); w.put(rate); w.put(UInt8(2)); w.bytes(Data(repeating: 0, count: 32))
            let hello = try Hello(w.data)
            XCTAssertEqual(hello.bitrate, rate)
            var reply = Reader(ProtocolWire.welcome(session: 1, codec: .hevc, hello: hello))
            _ = try reply.bytes(18); XCTAssertEqual(try reply.get(UInt64.self), rate)
        }
    }
    func testWideTwentyGigabitHelloKeepsPrecision() throws {
        var w = Writer();w.put(Message.helloWide.rawValue);w.put(UInt16(2));w.put(UInt16(50000))
        w.put(UInt16(4096));w.put(UInt16(2560));w.put(UInt16(60));w.put(UInt64(20_000_000_000));w.put(UInt8(3));w.bytes(Data(repeating: 0, count: 32))
        let hello = try Hello(w.data);XCTAssertTrue(hello.wide);XCTAssertEqual(hello.bitrate, 20_000_000_000)
        let reply = ProtocolWire.welcome(session: 1, codec: .hevc, hello: hello)
        XCTAssertEqual(reply.count, 26);var r = Reader(reply);_ = try r.bytes(18);XCTAssertEqual(try r.get(UInt64.self),20_000_000_000)
        let limited = ProtocolWire.welcome(session: 1, codec: .hevc, hello: hello, acceptedBitrate: 10_737_000_000)
        var accepted = Reader(limited);_ = try accepted.bytes(18);XCTAssertEqual(try accepted.get(UInt64.self), 10_737_000_000)
        var invalid = w.data;invalid[11] = 1;XCTAssertThrowsError(try Hello(invalid))
    }
    func testImageAndCursorChunksAndBounds() throws {
        let data = Data((0..<400000).map { UInt8(truncatingIfNeeded: $0 * 17) })
        for kind in [Message.cursorImage, .clipboardImage] {
            let packets = BinaryWire.packets(data, kind: kind, id: 8, limit: BinaryWire.imageLimit)
            var assembler = BinaryAssembler(), result: Data?
            for packet in packets { XCTAssertLessThanOrEqual(packet.count, ProtocolWire.controlLimit);result = try assembler.append(packet, kind: kind, limit: BinaryWire.imageLimit) }
            XCTAssertEqual(result, data)
            assembler = BinaryAssembler();XCTAssertThrowsError(try assembler.append(packets[1], kind: kind, limit: BinaryWire.imageLimit))
            var invalid = packets[0];invalid[5] = 0x7f
            XCTAssertThrowsError(try assembler.append(invalid, kind: kind, limit: BinaryWire.imageLimit))
        }
    }
}
