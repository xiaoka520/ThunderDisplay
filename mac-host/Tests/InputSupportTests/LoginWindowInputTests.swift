import XCTest
import CoreGraphics
import HIDBridge
@testable import InputSupport

final class LoginWindowInputTests: XCTestCase {
    func testUnmarkedProcessCannotInitializePreLoginInput() throws {
        // Xcode loads this test bundle into XCTestRunner, whose main executable
        // is unmarked. The real host's section is checked when packaging it.
        guard !TDPreLoginAppMarkerPresent() else { throw XCTSkip("Main executable has the marker; package validation checks the host") }
        XCTAssertThrowsError(try LoginWindowInput()) { error in
            guard case InputPostingError.missingPreLoginMarker = error else {
                return XCTFail("Unexpected error: \(error)")
            }
        }
    }
    func testSessionMouseRejectsKeyboardWithoutChangingButtonState() throws {
        let event = try XCTUnwrap(CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: true))
        var buttons: UInt8 = 5
        XCTAssertNotEqual(TDSessionPostMouse(event, &buttons), 0)
        XCTAssertEqual(buttons, 5)
    }
    func testNoConsentIsNotTreatedAsReady() throws {
        guard !CGPreflightPostEventAccess() else { throw XCTSkip("Explicit signed app diagnostic covers authorized delivery") }
        XCTAssertThrowsError(try LoginWindowInput())
    }
}
