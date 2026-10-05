import XCTest
@testable import Wire
final class CursorWireTests: XCTestCase {
    let png = Data([0x89, 0x50, 0x4e, 0x47, 13, 10, 26, 10])
    func testNativeVariantsAndLegacyShape() throws {
        let body = try CursorPayload.encode(width: 23 * 65536, height: 22 * 65536,
            hotX: 12 * 65536, hotY: 11 * 65536, images: [png, png], variants: true)
        let decoded = try CursorPayload(body)
        XCTAssertEqual(body.count, 42); XCTAssertEqual(body[0], 2); XCTAssertEqual(body[17], 2)
        XCTAssertEqual(decoded.images, [png, png]); XCTAssertEqual(decoded.hotX, 12 * 65536)
        let legacy = try CursorPayload.encode(width: 23 * 65536, height: 22 * 65536,
            hotX: 12 * 65536, hotY: 11 * 65536, images: [png, png + Data([1])], variants: false)
        XCTAssertEqual(legacy[0], 1); XCTAssertEqual(try CursorPayload(legacy).images, [png + Data([1])])
        // The same bytes/offsets are covered by the C++ parser test.
        XCTAssertEqual(Array(body.prefix(5)), [2, 0, 23, 0, 0])
    }
    func testMalformedVariantsAreRejectedBeforePNGDecode() throws {
        let body = try CursorPayload.encode(width: 23 * 65536, height: 22 * 65536,
            hotX: 12 * 65536, hotY: 11 * 65536, images: [png, png], variants: true)
        for kind in 0..<6 {
            var invalid = body
            switch kind {
            case 0: invalid[17] = 9
            case 1: invalid[18] = 255
            case 2: invalid.removeLast()
            case 3: invalid.append(0)
            case 4: invalid[9] = 255
            default: invalid[22] = 0
            }
            XCTAssertThrowsError(try CursorPayload(invalid))
        }
        XCTAssertThrowsError(try CursorPayload.encode(width: 1, height: 65536, hotX: 0, hotY: 0, images: [png], variants: true))
        XCTAssertThrowsError(try CursorPayload.encode(width: 65536, height: 65536, hotX: 0, hotY: 0, images: [], variants: true))
    }
}
