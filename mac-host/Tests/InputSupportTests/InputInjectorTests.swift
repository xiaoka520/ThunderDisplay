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
}
