import XCTest
@testable import Wire

final class MouseModeTests: XCTestCase {
    private func packet(_ kind: UInt8, code: UInt16 = 0, flags: UInt16 = 0, x: Int32 = 0, y: Int32 = 0) -> Data {
        var w = Writer(); w.put(Message.input.rawValue); w.put(kind); w.put(code); w.put(flags); w.put(x); w.put(y); return w.data
    }
    func testMouseModeGoldenBytesKeepSessionBinding() {
        XCTAssertEqual(Array(MouseModeWire.packet(session: 0x0102030405060708, relative: true)), [20,1,2,3,4,5,6,7,8,1])
        XCTAssertEqual(Array(MouseModeWire.packet(session: 0x0102030405060708, relative: false)), [20,1,2,3,4,5,6,7,8,0])
    }
    func testRelativeSignedCoordinatesAndButtonsAreStrictlyValidated() throws {
        let motion = try Input(packet(6, flags: 14, x: -32767, y: 32767))
        XCTAssertEqual(motion.x, -32767); XCTAssertEqual(motion.y, 32767)
        XCTAssertEqual(try Input(packet(7, code: 2, flags: 1)).code, 2)
        for invalid in [packet(6, x: 32768), packet(6, y: -32768), packet(6, code: 1),
                        packet(7, code: 3), packet(7, x: 1), packet(7, y: -1), packet(6, flags: 64), packet(8)] {
            XCTAssertThrowsError(try Input(invalid))
        }
    }
}
