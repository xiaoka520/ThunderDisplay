import AppKit
import XCTest
@testable import CursorSupport

final class SystemCursorImageTests: XCTestCase {
    func testHighestNativeRepresentationKeepsLogicalSizeAndHotspot() throws {
        let image = NSImage(size: CGSize(width: 23, height: 22))
        for density in [1, 2, 5, 10] {
            let rep = try XCTUnwrap(NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 23 * density,
                pixelsHigh: 22 * density, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
                isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 32))
            rep.size = image.size
            rep.bitmapData?.initialize(repeating: UInt8(density), count: rep.bytesPerRow * rep.pixelsHigh)
            image.addRepresentation(rep)
        }
        let cursor = NSCursor(image: image, hotSpot: CGPoint(x: 12, y: 11))
        let result = try XCTUnwrap(SystemCursorImage.read(cursor))
        XCTAssertEqual(result.pixels.width, 230); XCTAssertEqual(result.pixels.height, 220)
        XCTAssertEqual(result.representations.map { $0.width }, [23, 46, 115, 230])
        XCTAssertEqual(result.logicalSize, CGSize(width: 23, height: 22))
        XCTAssertEqual(result.hotspot, CGPoint(x: 12, y: 11))
        // The source representation is retained rather than resized/filtered.
        let bytes = try XCTUnwrap(result.pixels.dataProvider?.data as Data?)
        XCTAssertTrue(bytes.allSatisfy { $0 == 10 })
    }
    func testCustomScreenshotCursorKeepsItsOwnPixelsAndClickLocation() throws {
        let rep = try XCTUnwrap(NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 60, pixelsHigh: 48,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 32))
        rep.size = CGSize(width: 30, height: 24)
        rep.bitmapData?.initialize(repeating: 128, count: rep.bytesPerRow * rep.pixelsHigh)
        let image = NSImage(size: rep.size); image.addRepresentation(rep)
        let cursor = NSCursor(image: image, hotSpot: CGPoint(x: 15, y: 12))
        let result = try XCTUnwrap(SystemCursorImage.read(cursor))
        XCTAssertEqual(result.pixels.width, 60); XCTAssertEqual(result.pixels.height, 48)
        XCTAssertEqual(result.logicalSize, rep.size); XCTAssertEqual(result.hotspot, cursor.hotSpot)
    }
    func testEmptyImageIsUnavailableInsteadOfInventingAnArrow() {
        XCTAssertNil(SystemCursorImage.read(NSCursor(image: NSImage(size: .zero), hotSpot: .zero)))
    }
}
