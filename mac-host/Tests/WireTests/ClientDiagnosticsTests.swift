import XCTest
@testable import Wire

final class ClientDiagnosticsTests: XCTestCase {
    private func packet(_ state: UInt8 = 2, _ text: String = "video.performance receive_fps=60 present_fps=59\n", session: UInt64 = 7) -> Data {
        var w = Writer(); w.put(Message.clientDiagnostics.rawValue); w.put(UInt8(1)); w.put(session); w.put(state)
        return w.data + Data(text.utf8)
    }
    func testGoldenSessionAndState() throws {
        XCTAssertEqual(packet(1, ""), Data([23,1,0,0,0,0,0,0,0,7,1]))
        XCTAssertEqual(try ClientDiagnostics(packet(1, ""), session: 7).state, .start)
        XCTAssertEqual(try ClientDiagnostics(packet(0, ""), session: 7).state, .stop)
        XCTAssertEqual(try ClientDiagnostics(packet(), session: 7).lines, ["video.performance receive_fps=60 present_fps=59"])
        XCTAssertThrowsError(try ClientDiagnostics(packet(), session: 8))
        XCTAssertThrowsError(try ClientDiagnostics(packet(session: 0), session: 0))
    }
    func testMalformedShapeAndControlCharacters() throws {
        for p in [Data(), Data(packet().prefix(10)), packet(3), packet(0, "text"), packet(1, "text"), packet(2, ""), packet(2, "escape\u{1b}"), packet(2, "\r"), packet(2, "中文")] {
            XCTAssertThrowsError(try ClientDiagnostics(p, session: 7))
        }
        var p = packet(); p[1] = 2; XCTAssertThrowsError(try ClientDiagnostics(p, session: 7))
    }
    func testBoundedLineAndBatchSize() throws {
        XCTAssertNoThrow(try ClientDiagnostics(packet(2, String(repeating: "x", count: 1024)), session: 7))
        XCTAssertThrowsError(try ClientDiagnostics(packet(2, String(repeating: "x", count: 1025)), session: 7))
        XCTAssertNoThrow(try ClientDiagnostics(packet(2, String(repeating: "x\n", count: 33)), session: 7))
        XCTAssertThrowsError(try ClientDiagnostics(packet(2, String(repeating: "x\n", count: 34)), session: 7))
        XCTAssertThrowsError(try ClientDiagnostics(packet(2, String(repeating: "x\n", count: 2001)), session: 7))
    }
}
