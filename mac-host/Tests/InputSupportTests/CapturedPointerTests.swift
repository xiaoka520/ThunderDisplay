import XCTest
import CoreGraphics
@testable import InputSupport

final class CapturedPointerTests: XCTestCase {
    func testOtherScreenAndDisplayGapHidePointer() {
        let bounds = CGRect(x: -2048, y: 0, width: 2048, height: 1280)
        let size = CGSize(width: 4096, height: 2560), content = CGRect(origin: .zero, size: size)
        let first = CapturedPointer.normalized(CGPoint(x: -2048, y: 0), bounds: bounds, captureSize: size, contentRect: content)
        XCTAssertEqual(first?.0, 0); XCTAssertEqual(first?.1, 0)
        let last = CapturedPointer.normalized(CGPoint(x: -1, y: 1279), bounds: bounds, captureSize: size, contentRect: content)
        XCTAssertEqual(last?.0, 65535); XCTAssertEqual(last?.1, 65535)
        for point in [CGPoint(x: 0, y: 10), CGPoint(x: -2049, y: 10), CGPoint(x: -100, y: 1280)] {
            XCTAssertNil(CapturedPointer.normalized(point, bounds: bounds, captureSize: size, contentRect: content))
        }
    }
    func testLetterboxUsesCaptureContentAndRetainsHotspotPosition() {
        let point = CapturedPointer.normalized(CGPoint(x: 0, y: 0), bounds: CGRect(x: 0, y: 0, width: 1920, height: 1080),
            captureSize: CGSize(width: 2560, height: 1600), contentRect: CGRect(x: 0, y: 80, width: 2560, height: 1440))
        XCTAssertEqual(point?.0, 0); XCTAssertEqual(point?.1, 3279)
    }
}
