import XCTest
@testable import HostState

final class LoginWindowStateTests: XCTestCase {
    func testDiscoveryOnlyRelinquishesPortForLiveClaimOrListener() throws {
        for phase in [LoginWindowState.Phase.checking, .blocked, .stopped] {
            XCTAssertFalse(LoginWindowState(pid: 10, uptime: 100, phase: phase).ownsPort(at: 101))
        }
        for phase in [LoginWindowState.Phase.claiming, .listening] {
            let state = LoginWindowState(pid: 10, uptime: 100, phase: phase, port: 48123)
            XCTAssertTrue(state.ownsPort(at: 111.99))
            XCTAssertFalse(state.ownsPort(at: 112))
            XCTAssertTrue(state.fresh(at: 112, maximumAge: 86400)) // UI history cannot claim a live listener.
            XCTAssertFalse(state.ownsPort(at: 99))
            XCTAssertEqual(try JSONDecoder().decode(LoginWindowState.self, from: JSONEncoder().encode(state)), state)
        }
    }
    func testInvalidHeartbeatCannotClaimLoginWindow() throws {
        XCTAssertFalse(LoginWindowState(pid: 0, uptime: 100, phase: .listening).fresh(at: 101))
        XCTAssertFalse(LoginWindowState(pid: 1, uptime: .nan, phase: .listening).fresh(at: 101))
        XCTAssertFalse(LoginWindowState(pid: 1, uptime: 100, phase: .listening, port: 0).fresh(at: 101))
        let state = LoginWindowState(pid: 1, uptime: 100, phase: .listening)
        var object = try XCTUnwrap(JSONSerialization.jsonObject(with: JSONEncoder().encode(state)) as? [String: Any])
        object["version"] = 2
        XCTAssertFalse(try JSONDecoder().decode(LoginWindowState.self, from: JSONSerialization.data(withJSONObject: object)).ownsPort(at: 101))
        object["version"] = 1; object["detail"] = String(repeating: "x", count: 1025)
        XCTAssertFalse(try JSONDecoder().decode(LoginWindowState.self, from: JSONSerialization.data(withJSONObject: object)).fresh(at: 101))
    }
    func testPreLoginPairingPolicyPreservesExplicitUserChoice() throws {
        let unpaired = LoginWindowConfiguration(port: 48123, requirePairing: false, token: String(repeating: "a", count: 32))
        XCTAssertTrue(unpaired.valid); XCTAssertNil(unpaired.token)
        let paired = LoginWindowConfiguration(port: 48123, requirePairing: true, token: String(repeating: "a", count: 32))
        XCTAssertTrue(paired.valid)
        XCTAssertEqual(try PropertyListDecoder().decode(LoginWindowConfiguration.self, from: PropertyListEncoder().encode(paired)), paired)
        XCTAssertFalse(LoginWindowConfiguration(port: 0, requirePairing: false, token: nil).valid)
        XCTAssertFalse(LoginWindowConfiguration(port: 47990, requirePairing: true, token: nil).valid)
        XCTAssertFalse(LoginWindowConfiguration(port: 47990, requirePairing: true, token: String(repeating: "z", count: 32)).valid)
        XCTAssertFalse(LoginWindowConfiguration(port: 47990, requirePairing: true, token: String(repeating: "a", count: 31)).valid)
    }
    func testUnprivilegedProcessCannotPublishRootHeartbeat() {
        if geteuid() != 0 {
            XCTAssertThrowsError(try LoginWindowState(pid: getpid(), uptime: ProcessInfo.processInfo.systemUptime, phase: .listening).write())
        }
    }
    func testFailureHistorySurvivesLoginAndCannotBecomeConnectedState() throws {
        let state = LoginWindowState(pid: 5, uptime: 100, phase: .stopped, captureChecked: true, inputChecked: false, lastFailure: "Input permission unavailable")
        let decoded = try JSONDecoder().decode(LoginWindowState.self, from: JSONEncoder().encode(state))
        XCTAssertEqual(decoded.lastFailure, "Input permission unavailable")
        XCTAssertTrue(decoded.captureChecked); XCTAssertFalse(decoded.inputChecked)
        XCTAssertFalse(decoded.ownsPort(at: 101))
    }
}
