import XCTest
import Darwin
import HIDBridge
@testable import InputSupport

final class LoginWindowInputTests: XCTestCase {
    func testLoginKeyboardDestinationRejectsOtherProcessesAndInvalidIDs() {
        XCTAssertFalse(TDIsLoginWindowKeyboardTarget(0))
        XCTAssertFalse(TDIsLoginWindowKeyboardTarget(-1))
        XCTAssertFalse(TDIsLoginWindowKeyboardTarget(getpid()))
        XCTAssertFalse(TDIsLoginWindowKeyboardTarget(getppid()))
        XCTAssertFalse(TDIsLoginWindowKeyboardTarget(1))
        if geteuid() != 0 { XCTAssertEqual(TDLoginWindowKeyboardTarget(), 0) }
    }
    func testQuartzFallbackRequiresItsOwnAuthorizationAndLocalUserRejection() {
        let notPrivileged = Int32(bitPattern: 0xe00002c1)
        XCTAssertTrue(LoginWindowInput.mayUseQuartz(after: notPrivileged, authorized: true))
        XCTAssertFalse(LoginWindowInput.mayUseQuartz(after: notPrivileged, authorized: false))
        for status: UInt32 in [0, 0xe00002c0, 0xe00002c2, 0xe00002e2] {
            XCTAssertFalse(LoginWindowInput.mayUseQuartz(after: Int32(bitPattern: status), authorized: true))
        }
    }
}
