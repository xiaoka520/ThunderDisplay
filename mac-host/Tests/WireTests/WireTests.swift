import XCTest
@testable import Wire

final class WireTests: XCTestCase {
    func testSessionTransitionIsBoundToTheNegotiatedSession() {
        let id: UInt64 = 0x0102030405060708
        let notice = SessionTransitionWire.packet(session: id)
        XCTAssertEqual(notice, Data([17,1,2,3,4,5,6,7,8,1]))
        XCTAssertTrue(SessionTransitionWire.matches(notice, session: id))
        XCTAssertFalse(SessionTransitionWire.matches(notice, session: id + 1))
        XCTAssertFalse(SessionTransitionWire.matches(notice, session: 0))
        XCTAssertFalse(SessionTransitionWire.matches(notice, session: id, acknowledgment: true))
        XCTAssertTrue(SessionTransitionWire.matches(SessionTransitionWire.packet(session: id, acknowledgment: true), session: id, acknowledgment: true))
        XCTAssertFalse(SessionTransitionWire.matches(notice + Data([0]), session: id))
        XCTAssertFalse(SessionTransitionWire.matches(Data(notice.dropLast()), session: id))
        var invalid = notice; invalid[9] = 2
        XCTAssertFalse(SessionTransitionWire.matches(invalid, session: id))
    }
    func hello() -> Data {
        var w = Writer(); w.put(UInt8(1)); w.put(UInt16(1)); w.put(UInt16(50000))
        w.put(UInt16(2560)); w.put(UInt16(1600)); w.put(UInt16(120)); w.put(UInt32(120_000_000)); w.put(UInt8(3))
        w.bytes(Data("0123456789abcdef0123456789abcdef".utf8)); return w.data
    }
    func testGoldenHelloAndWelcome() throws {
        let data = hello(), h = try Hello(data)
        XCTAssertEqual(data.count, 48)
        XCTAssertEqual(Array(data.prefix(16)), [1,0,1,195,80,10,0,6,64,0,120,7,39,14,0,3])
        XCTAssertEqual(h.udpPort, 50000); XCTAssertEqual(h.width, 2560); XCTAssertEqual(h.fps, 120)
        let welcome = ProtocolWire.welcome(session: 0x0102030405060708, codec: .hevc, hello: h)
        XCTAssertEqual(Array(welcome), [2,0,1,1,2,3,4,5,6,7,8,2,10,0,6,64,0,120,7,39,14,0])
    }
    func testGigabitBitrateAndLargeFrameHeader() throws {
        var data = hello(); data.replaceSubrange(11..<15, with: [0x3b,0x9a,0xca,0x00])
        XCTAssertEqual(try Hello(data).bitrate, 1_000_000_000)
        data[14] = 1; XCTAssertThrowsError(try Hello(data))
        let size = 6 * 1024 * 1024
        let frame = Data(repeating: 0x5a, count: size)
        let count = (size + ProtocolWire.fragmentSize - 1) / ProtocolWire.fragmentSize
        let packet = ProtocolWire.packet(frame: frame, session: 1, id: 1, pts: 0, codec: .hevc, key: true, index: count - 1)
        var r = Reader(packet); _ = try r.bytes(28)
        XCTAssertEqual(try r.get(UInt32.self), UInt32(size))
        XCTAssertEqual(try r.get(UInt16.self), UInt16(count - 1))
        XCTAssertEqual(try r.get(UInt16.self), UInt16(count))
        XCTAssertLessThanOrEqual(packet.count, 1200)
    }
    func testDisplayCapabilitiesAndNonPresetRefreshRate() throws {
        var data = hello(); data[9] = 0; data[10] = 144
        XCTAssertEqual(try Hello(data).fps, 144)
        data[9] = 1; data[10] = 0; XCTAssertThrowsError(try Hello(data))
        let payload = ProtocolWire.capabilities(width: 2560, height: 1600, hz: 144,
            maximumWidth: 3840, maximumHeight: 2160, maximumHz: 240, flags: 1, name: "Mac")
        XCTAssertEqual(Array(payload), [10,0,1,0,0,10,0,0,0,6,64,0,144,0,0,15,0,0,0,8,112,0,240,3,8,1,0,3,77,97,99])
        var framer = ControlFramer()
        XCTAssertEqual(try framer.append(ProtocolWire.frame(payload)), [payload])
        let longName = ProtocolWire.capabilities(width: 2560, height: 1600, hz: 60,
            maximumWidth: 3840, maximumHeight: 2160, maximumHz: 240, flags: 0, name: String(repeating: "屏", count: 100))
        XCTAssertEqual(String(data: longName.dropFirst(28), encoding: .utf8), String(repeating: "屏", count: 85))
    }
    func testMain10NegotiationKeepsControlAndVideoWireShape() throws {
        var data = hello(); data[15] = 4
        let parsed = try Hello(data); XCTAssertEqual(parsed.codecMask, 4)
        data[15] = 7; XCTAssertEqual(try Hello(data).codecMask, 7)
        data[15] = 8; XCTAssertThrowsError(try Hello(data))
        let welcome = ProtocolWire.welcome(session: 1, codec: .hevc10, hello: parsed)
        XCTAssertEqual(welcome.count, 22); XCTAssertEqual(welcome[11], 4)
        let packet = ProtocolWire.packet(frame: Data([0,0,0,1,32]), session: 1, id: 1, pts: 0, codec: .hevc10, key: true, index: 0)
        XCTAssertEqual(packet.count, 45); XCTAssertEqual(packet[5], 4)
        let capability = ProtocolWire.capabilities(width: 2560, height: 1600, hz: 60, maximumWidth: 3840,
            maximumHeight: 2160, maximumHz: 240, flags: 0, name: "Mac", codecMask: 7, streamBits: 10)
        XCTAssertEqual(capability[23], 7); XCTAssertEqual(capability[24], 10)
    }
    func testTCPAtEverySplitAndCoalescing() throws {
        let data = ProtocolWire.frame(hello()) + ProtocolWire.frame(Data([5]))
        for i in 0...data.count {
            var f = ControlFramer()
            let messages = try f.append(Data(data.prefix(i))) + f.append(Data(data.dropFirst(i)))
            XCTAssertEqual(messages, [hello(), Data([5])])
        }
        var f = ControlFramer(); var messages: [Data] = []
        for byte in data { messages += try f.append(Data([byte])) }
        XCTAssertEqual(messages.count, 2)
    }
    func testRejectUnboundedControlAndInvalidHello() {
        for bytes in [[UInt8](repeating: 0, count: 4), [0,0,16,1], [255,255,255,255]] {
            var f = ControlFramer(); XCTAssertThrowsError(try f.append(Data(bytes)))
        }
        var invalid = hello(); invalid[6] = 1; XCTAssertThrowsError(try Hello(invalid))
        XCTAssertThrowsError(try Hello(Data(hello().dropLast())))
        XCTAssertThrowsError(try Hello(hello() + Data([0])))
    }
    func testSignedInputAndValidation() throws {
        var w = Writer(); w.put(UInt8(3)); w.put(UInt8(4)); w.put(UInt16(0)); w.put(UInt16(14)); w.put(Int32(-120)); w.put(Int32(48))
        let input = try Input(w.data); XCTAssertEqual(input.x, -120); XCTAssertEqual(input.y, 48)
        XCTAssertThrowsError(try Input(Data([3,9]) + Data(repeating: 0, count: 12)))
    }
    func testVideoHeaderAndFinalFragment() throws {
        let frame = Data(repeating: 0xab, count: 1161)
        let packet = ProtocolWire.packet(frame: frame, session: 0x0102030405060708, id: 9, pts: 1000, codec: .hevc, key: true, index: 1)
        XCTAssertEqual(packet.count, 41)
        var r = Reader(packet)
        XCTAssertEqual(try r.get(UInt32.self), 0x54444231); XCTAssertEqual(try r.get(UInt8.self), 1)
        XCTAssertEqual(try r.get(UInt8.self), 2); XCTAssertEqual(try r.get(UInt16.self), 1)
        XCTAssertEqual(try r.get(UInt64.self), 0x0102030405060708); XCTAssertEqual(try r.get(UInt32.self), 9)
        XCTAssertEqual(try r.get(UInt64.self), 1000); XCTAssertEqual(try r.get(UInt32.self), 1161)
        XCTAssertEqual(try r.get(UInt16.self), 1); XCTAssertEqual(try r.get(UInt16.self), 2)
        XCTAssertEqual(try r.get(UInt16.self), 1); XCTAssertEqual(try r.get(UInt16.self), 0)
        XCTAssertEqual(try r.get(UInt8.self), 0xab); XCTAssertTrue(r.atEnd)
    }
    func testAuthentication() {
        let code = Data("0123456789abcdef0123456789abcdef".utf8)
        XCTAssertTrue(ProtocolWire.matchesToken(code, code))
        XCTAssertFalse(ProtocolWire.matchesToken(code, Data(repeating: 0, count: 32)))
        XCTAssertFalse(ProtocolWire.matchesToken(code, Data(code.dropLast())))
    }
    func testOptionalPairingKeepsWireShapeAndDoesNotBypassRequiredPairing() throws {
        let empty = Data(repeating: 0, count: 32)
        let packet = Data(hello().prefix(16)) + empty
        let parsed = try Hello(packet)
        XCTAssertEqual(parsed.token, empty)
        let code = Data("0123456789abcdef0123456789abcdef".utf8)
        XCTAssertTrue(ProtocolWire.acceptsToken(parsed.token, expected: code, required: false))
        XCTAssertFalse(ProtocolWire.acceptsToken(parsed.token, expected: code, required: true))
        XCTAssertTrue(ProtocolWire.acceptsToken(code, expected: code, required: true))
        XCTAssertFalse(ProtocolWire.acceptsToken(Data(), expected: code, required: false))
    }
}
