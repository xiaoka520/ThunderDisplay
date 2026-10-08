import XCTest
import CoreGraphics
import Darwin
import Wire
@testable import InputSupport

final class InputInjectorTests: XCTestCase {
    private func input(_ kind: UInt8, _ code: UInt16 = 0, _ flags: UInt16 = 0, _ x: Int32 = 0, _ y: Int32 = 0) throws -> Input {
        var data = Data([3, kind])
        for value in [code, flags] { data.append(contentsOf: [UInt8(value >> 8), UInt8(value & 255)]) }
        for value in [x, y] {
            let bits = UInt32(bitPattern: value)
            data.append(contentsOf: [UInt8(bits >> 24), UInt8((bits >> 16) & 255), UInt8((bits >> 8) & 255), UInt8(bits & 255)])
        }
        return try Input(data)
    }
    private func injector(authorized: @escaping () -> Bool = { true }, post: @escaping (CGEvent) throws -> Void) -> InputInjector {
        InputInjector(bounds: CGRect(x: -100, y: 50, width: 1280, height: 720),
            captureSize: CGSize(width: 640, height: 480), contentRect: CGRect(x: 0, y: 60, width: 640, height: 360),
            authorized: authorized, post: post)
    }
    func testDeniedAccessIsReportedInsteadOfSilentlyDroppingInput() throws {
        var count = 0
        let device = injector(authorized: { false }) { _ in count += 1 }
        XCTAssertThrowsError(try device.apply(input(3, 0x41, 1)))
        XCTAssertEqual(count, 0)
    }
    func testLetterboxCoordinatesAndDragRelease() throws {
        var events: [CGEvent] = []
        let device = injector { events.append($0) }
        try device.apply(input(1, 0, 0, 32768, 0))
        try device.apply(input(2, 0, 1, 32768, 0))
        XCTAssertTrue(events.isEmpty)
        try device.apply(input(2, 0, 1, 32768, 32768))
        XCTAssertEqual(events.last?.type, .leftMouseDown)
        XCTAssertEqual(events.last!.location.x, 539.5, accuracy: 2)
        XCTAssertEqual(events.last!.location.y, 409.5, accuracy: 2)
        try device.apply(input(1, 0, 0, 65535, 0))
        XCTAssertEqual(events.last?.type, .leftMouseDragged)
        XCTAssertEqual(events.last!.location.x, 1179, accuracy: 0.01)
        XCTAssertEqual(events.last!.location.y, 50, accuracy: 0.01)
        try device.apply(input(2, 0, 0, 65535, 0))
        XCTAssertEqual(events.last?.type, .leftMouseUp)
        try device.apply(input(1, 0, 0, 32768, 0))
        XCTAssertEqual(events.count, 3) // Hover outside content is ignored again.
    }
    func testRightShiftProducesFlagsTransitionsAndRepeatStaysACharacter() throws {
        var events: [CGEvent] = []
        let device = injector { events.append($0) }
        try device.apply(input(3, 0xA1, 3))
        XCTAssertEqual(events.last?.type, .flagsChanged)
        XCTAssertEqual(events.last?.getIntegerValueField(.keyboardEventKeycode), 60)
        XCTAssertTrue(events.last!.flags.contains(.maskShift))
        XCTAssertEqual(events.last!.flags.rawValue & 6, 4) // Right, not left Shift.
        try device.apply(input(1, 0, 2, 32768, 32768))
        XCTAssertEqual(events.last?.type, .mouseMoved)
        XCTAssertTrue(events.last!.flags.contains(.maskShift))
        XCTAssertEqual(events.last!.flags.rawValue & 6, 4) // Mouse movement retains held modifier state.
        try device.apply(input(3, 0x41, 3))
        XCTAssertEqual(events.last?.type, .keyDown)
        XCTAssertEqual(events.last?.getIntegerValueField(.keyboardEventAutorepeat), 0)
        try device.apply(input(3, 0x41, 3))
        XCTAssertEqual(events.last?.getIntegerValueField(.keyboardEventAutorepeat), 1)
        try device.apply(input(3, 0x41, 2))
        XCTAssertEqual(events.last?.type, .keyUp)
        try device.apply(input(3, 0xA1, 0))
        XCTAssertEqual(events.last?.type, .flagsChanged)
        XCTAssertFalse(events.last!.flags.contains(.maskShift))
        XCTAssertEqual(events.last!.flags.rawValue & 6, 0)
    }
    func testDisconnectReleasesCharactersButtonsAndModifiersWithoutClearingCaps() throws {
        var events: [CGEvent] = []
        let device = injector { events.append($0) }
        try device.apply(input(3, 0xA0, 35))
        try device.apply(input(3, 0x41, 35))
        try device.apply(input(2, 1, 35, 32768, 32768))
        events.removeAll()
        try device.apply(input(5))
        XCTAssertEqual(events.map(\.type), [.keyUp, .flagsChanged, .rightMouseUp])
        XCTAssertTrue(events[0].flags.contains(.maskShift))
        XCTAssertFalse(events[1].flags.contains(.maskShift))
        XCTAssertTrue(events[1].flags.contains(.maskAlphaShift))
        XCTAssertTrue(events[2].flags.contains(.maskAlphaShift))
        XCTAssertFalse(events[2].flags.contains(.maskShift))
        events.removeAll(); device.releaseAll()
        XCTAssertTrue(events.isEmpty)
    }
    func testSystemPostingFailurePropagates() throws {
        let device = injector { _ in throw InputPostingError.system(-1) }
        XCTAssertThrowsError(try device.apply(input(3, 0x41, 1))) { error in
            XCTAssertEqual(error.localizedDescription, "System HID input rejected (0xffffffff)")
        }
    }
    func testNativeConnectionRequiresSystemAuthorization() throws {
        guard !NativeHIDInput.isAuthorized else { throw XCTSkip("Permission is granted; native input is checked by the explicit GUI diagnostic") }
        XCTAssertThrowsError(try NativeHIDInput())
    }
    func testRelativeMovementClickAndDragStayOnSecondaryDisplay() throws {
        var events: [CGEvent] = []
        let bounds = CGRect(x: 0, y: 0, width: 1280, height: 720)
        let device = InputInjector(bounds: bounds, captureSize: CGSize(width: 2560, height: 1440),
            contentRect: CGRect(x: 0, y: 0, width: 2560, height: 1440),
            desktopBounds: [bounds, CGRect(x: 1280, y: 0, width: 1920, height: 1080)],
            pointerPosition: { CGPoint(x: 1270, y: 300) }, authorized: { true }, post: { events.append($0) })
        try device.apply(input(6, 0, 0, 30, 0))
        XCTAssertEqual(events.last!.location, CGPoint(x: 1300, y: 300))
        try device.apply(input(7, 0, 1))
        XCTAssertEqual(events.last!.type, .leftMouseDown)
        XCTAssertEqual(events.last!.location, CGPoint(x: 1300, y: 300))
        try device.apply(input(6, 0, 0, 100, 20))
        XCTAssertEqual(events.last!.type, .leftMouseDragged)
        XCTAssertEqual(events.last!.location, CGPoint(x: 1400, y: 320))
        try device.apply(input(7, 0, 0))
        XCTAssertEqual(events.last!.type, .leftMouseUp)
        XCTAssertEqual(events.last!.location, CGPoint(x: 1400, y: 320))
        // The HiDPI video is twice the logical size; raw movement is not doubled.
        try device.apply(input(6, 0, 0, -200, 0))
        XCTAssertEqual(events.last!.location, CGPoint(x: 1200, y: 320))
    }
    func testFullscreenReleaseThenWindowMappingAndReacquisition() throws {
        var events: [CGEvent] = [], actual = CGPoint(x: -30, y: 100)
        let bounds = CGRect(x: 0, y: 0, width: 1280, height: 720)
        let device = InputInjector(bounds: bounds, captureSize: bounds.size, contentRect: bounds,
            desktopBounds: [bounds, CGRect(x: -1280, y: 0, width: 1280, height: 720)],
            pointerPosition: { actual }, authorized: { true }, post: { events.append($0) })
        try device.apply(input(7, 1, 1))
        try device.apply(input(5))
        XCTAssertEqual(events.last!.type, .rightMouseUp)
        XCTAssertEqual(events.last!.location, actual)
        try device.apply(input(1, 0, 0, 65535, 65535))
        XCTAssertEqual(events.last!.location, CGPoint(x: 1279, y: 719))
        try device.apply(input(5))
        actual = CGPoint(x: -300, y: 200)
        try device.apply(input(6, 0, 0, -20, 10))
        XCTAssertEqual(events.last!.location, CGPoint(x: -320, y: 210))
    }
    func testLegacyInjectorDoesNotAcceptRelativeMovementOrButtons() throws {
        var events: [CGEvent] = []
        let device = injector { events.append($0) }
        try device.apply(input(6, 0, 0, 30, 0)); try device.apply(input(7, 0, 1))
        XCTAssertTrue(events.isEmpty)
    }
}
