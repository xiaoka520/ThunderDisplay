import XCTest
@testable import HostState

final class LaunchServiceStateTests: XCTestCase {
    func testFailedRegistrationIsNotRunning() {
        XCTAssertEqual(LaunchServiceState.parse("state = spawn scheduled\nlast exit code = 78: EX_CONFIG\njob state = spawn failed", succeeded: true), .failed(78))
        XCTAssertEqual(LaunchServiceState.parse("state = not running\nlast exit code = 0", succeeded: true), .stopped)
    }
    func testRunningRequiresBothStateAndProcess() {
        XCTAssertEqual(LaunchServiceState.parse(" state = running\n pid = 123", succeeded: true), .running)
        XCTAssertEqual(LaunchServiceState.parse("state = running", succeeded: true), .stopped)
        XCTAssertEqual(LaunchServiceState.parse("state = exited\npid = 123", succeeded: true), .stopped)
        XCTAssertEqual(LaunchServiceState.parse("state = running\npid = 123", succeeded: false), .unavailable)
        XCTAssertEqual(LaunchServiceState.parse("state = running\npid = 0", succeeded: true), .stopped)
    }
    func testOldServiceManagementJobCannotProveSystemDaemonIsRunning() {
        let path = "/Library/LaunchDaemons/dev.thunderdisplay.boot.system.plist"
        XCTAssertEqual(LaunchServiceState.parseInstalledDaemon("path = (submitted by smd.100)\nstate = running\npid = 123", succeeded: true, plistPath: path), .unavailable)
        XCTAssertEqual(LaunchServiceState.parseInstalledDaemon("path = /Library/LaunchDaemons/dev.thunderdisplay.boot.plist\nstate = running\npid = 123", succeeded: true, plistPath: path), .unavailable)
        XCTAssertEqual(LaunchServiceState.parseInstalledDaemon("path = \(path)\nstate = running\npid = 123", succeeded: true, plistPath: path), .running)
        XCTAssertEqual(LaunchServiceState.parseInstalledDaemon("path = \(path)\nstate = spawn scheduled\nlast exit code = 78: EX_CONFIG", succeeded: true, plistPath: path), .failed(78))
    }
}
