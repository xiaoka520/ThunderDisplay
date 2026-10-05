import XCTest
@testable import Wire

final class ClipboardTests: XCTestCase {
    func testUnicodeAndBoundedChunking() throws {
        for text in ["", "Windows → Mac\r\n中文🐱", String(repeating: "x", count: 65536), String(repeating: "x", count: 3071) + "中文"] {
            let packets = TextClipboardWire.packets(text, id: 7)
            XCTAssertFalse(packets.isEmpty)
            var assembler = TextClipboardAssembler(), received: String?
            for packet in packets { XCTAssertLessThanOrEqual(packet.count, ProtocolWire.controlLimit); received = try assembler.append(packet) }
            XCTAssertEqual(received, text)
        }
        XCTAssertTrue(TextClipboardWire.packets(String(repeating: "x", count: 65537), id: 1).isEmpty)
        XCTAssertTrue(TextClipboardWire.packets("a\0b", id: 1).isEmpty)
        XCTAssertTrue(TextClipboardWire.packets("text", id: 0).isEmpty)
    }
    func testGoldenClipboardAndMalformedInput() throws {
        let golden = TextClipboardWire.packets("中", id: 0x01020304)[0]
        XCTAssertEqual(Array(golden), [12,1,2,3,4,0,0,0,3,0,0,0,0,0xe4,0xb8,0xad])
        var assembler = TextClipboardAssembler()
        XCTAssertEqual(try assembler.append(golden), "中")
        var malformed = golden; malformed[malformed.count - 1] = 0xff
        XCTAssertThrowsError(try assembler.append(malformed))
        XCTAssertThrowsError(try assembler.append(Data([12])))
        let chunks = TextClipboardWire.packets(String(repeating: "x", count: 4000), id: 2)
        var new = TextClipboardAssembler()
        XCTAssertThrowsError(try new.append(chunks[1]))
        XCTAssertNil(try new.append(chunks[0]))
        XCTAssertEqual(try new.append(TextClipboardWire.packets("new copy", id: 3)[0]), "new copy")
        var oversized = chunks[0]; oversized[5] = 1
        XCTAssertThrowsError(try new.append(oversized))
    }
    func testNativeHiDPIHello() throws {
        var w = Writer(); w.put(UInt8(1)); w.put(UInt16(1)); w.put(UInt16(50000))
        w.put(UInt16(4096)); w.put(UInt16(2560)); w.put(UInt16(60)); w.put(UInt32(290_000_000)); w.put(UInt8(7))
        w.bytes(Data(repeating: 0, count: 32)); XCTAssertEqual(try Hello(w.data).height, 2560)
    }
}
