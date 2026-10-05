import XCTest
@testable import HostState

final class PermissionStateTests: XCTestCase {
    func testFreshGrantRequiresRelaunchForStaleProcess() {
        XCTAssertEqual(PermissionState.evaluate(current: false, fresh: true), .relaunchRequired)
        XCTAssertFalse(PermissionState.evaluate(current: false, fresh: true).usable)
    }
    func testRealCaptureOverridesFalsePreflight() {
        XCTAssertEqual(PermissionState.evaluate(current: false, fresh: false, verified: true), .ready)
    }
    func testFailedCaptureCheckIsNotReportedAsGranted() {
        XCTAssertEqual(PermissionState.evaluate(current: true, fresh: true, verified: false), .checkFailed)
        XCTAssertEqual(PermissionState.evaluate(current: false, fresh: true, verified: false), .relaunchRequired)
    }
    func testMissingOrFailedProbeDoesNotInventAuthorization() {
        XCTAssertEqual(PermissionState.evaluate(current: false, fresh: nil), .notEffective)
        XCTAssertEqual(PermissionState.evaluate(current: false, fresh: false), .notEffective)
    }
    func testAuthorizationFlowClosesOnlyAfterBothGrants() {
        for screen in [PermissionState.ready, .relaunchRequired] {
            for access in [PermissionState.ready, .relaunchRequired] {
                XCTAssertTrue(PermissionState.authorizationComplete(screen: screen, access: access))
            }
        }
        for missing in [PermissionState.notEffective, .checkFailed] {
            XCTAssertFalse(PermissionState.authorizationComplete(screen: .ready, access: missing))
            XCTAssertFalse(PermissionState.authorizationComplete(screen: missing, access: .ready))
        }
        XCTAssertFalse(PermissionState.relaunchRequired.usable)
    }
}
