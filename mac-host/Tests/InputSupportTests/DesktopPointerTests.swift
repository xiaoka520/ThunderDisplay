import XCTest
import CoreGraphics
@testable import InputSupport

final class DesktopPointerTests: XCTestCase {
    private let main = CGRect(x: 0, y: 0, width: 1920, height: 1080)
    func testCrossesBothDirectionsAcrossAdjacentDisplaysWithDifferentSizes() {
        let desktop = DesktopPointer(screens: [main, CGRect(x: 1920, y: 120, width: 1280, height: 720)])
        let right = desktop.move(from: CGPoint(x: 1910, y: 300), by: CGPoint(x: 30, y: 0))
        XCTAssertEqual(right, CGPoint(x: 1940, y: 300))
        XCTAssertEqual(desktop.move(from: right, by: CGPoint(x: -50, y: 0)), CGPoint(x: 1890, y: 300))
        XCTAssertEqual(desktop.move(from: CGPoint(x: 1910, y: 50), by: CGPoint(x: 30, y: 20)), CGPoint(x: 1919, y: 70))
    }
    func testNegativeOriginsAndVerticalArrangementUseQuartzCoordinates() {
        let left = DesktopPointer(screens: [main, CGRect(x: -1280, y: -200, width: 1280, height: 1024)])
        XCTAssertEqual(left.move(from: CGPoint(x: 10, y: 300), by: CGPoint(x: -40, y: -20)), CGPoint(x: -30, y: 280))
        let above = DesktopPointer(screens: [main, CGRect(x: 200, y: -900, width: 1600, height: 900)])
        let point = above.move(from: CGPoint(x: 400, y: 10), by: CGPoint(x: 20, y: -30))
        XCTAssertEqual(point, CGPoint(x: 420, y: -20))
        XCTAssertEqual(above.move(from: point, by: CGPoint(x: 0, y: 40)), CGPoint(x: 420, y: 20))
    }
    func testPointerStaysOnActualDisplaysAndSlidesAlongOuterEdges() {
        let desktop = DesktopPointer(screens: [main, CGRect(x: 1920, y: 0, width: 1280, height: 720)])
        XCTAssertEqual(desktop.move(from: CGPoint(x: 3100, y: 600), by: CGPoint(x: 200, y: 200)), CGPoint(x: 3199, y: 719))
        let moved = desktop.move(from: CGPoint(x: 1910, y: 900), by: CGPoint(x: 100, y: -200))
        XCTAssertTrue(desktop.screens.contains { $0.contains(moved) })
        XCTAssertEqual(moved.y, 700)
    }
    func testLargeMovementCannotJumpAcrossAGap() {
        let desktop = DesktopPointer(screens: [main, CGRect(x: 2020, y: 0, width: 1280, height: 720)])
        XCTAssertEqual(desktop.move(from: CGPoint(x: 1910, y: 300), by: CGPoint(x: 1000, y: 0)), CGPoint(x: 1919, y: 300))
    }
    func testInvalidAndMirroredGeometryDoesNotExpandMovementRange() {
        let desktop = DesktopPointer(screens: [main, main, .zero, .null])
        XCTAssertEqual(desktop.move(from: CGPoint(x: 1800, y: 1000), by: CGPoint(x: 1000, y: 1000)), CGPoint(x: 1919, y: 1079))
    }
}
