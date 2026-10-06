import XCTest
@testable import HostState

final class LoginWindowStateTests: XCTestCase {
    func testDesktopDemandIsScopedToConfiguredAndCurrentUser() {
        let config = LoginWindowConfiguration(port: 47990, requirePairing: false, token: nil, desktopUID: 501)
        XCTAssertEqual(config.desktopStartupTarget(consoleUID: 501), "gui/501/dev.thunderdisplay.desktop")
        for uid in [nil, 0, 499, 502, UInt32.max] as [UInt32?] {
            XCTAssertNil(config.desktopStartupTarget(consoleUID: uid))
        }
        for uid in [nil, 0, 499, UInt32.max] as [UInt32?] {
            XCTAssertNil(LoginWindowConfiguration(port: 47990, requirePairing: false, token: nil, desktopUID: uid).desktopStartupTarget(consoleUID: uid))
        }
    }
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
    func testPreviousBootHeartbeatIsReadableOnlyAsHistory() throws {
        // Written 96 s into the previous boot, exactly like the observed record.
        // Uptime restarts at 0, so a freshness-checked read must reject it while the
        // age-independent read still explains why pre-login capture never started.
        let record = LoginWindowState(pid: 279, uptime: 96.3, phase: .stopped, detail: "LoginWindow agent stopped",
                                      previousPhase: .blocked, previousDetail: "Thunderbolt Bridge unavailable")
        XCTAssertFalse(record.fresh(at: 12, maximumAge: 86400))
        XCTAssertFalse(record.ownsPort(at: 12))
        XCTAssertTrue(record.isWellFormedRecord)
        XCTAssertEqual(record.previousPhase, .blocked)
        XCTAssertEqual(record.previousDetail, "Thunderbolt Bridge unavailable")
        XCTAssertEqual(try JSONDecoder().decode(LoginWindowState.self, from: JSONEncoder().encode(record)), record)
    }
    func testHeartbeatWithoutSnapshotFieldsStillDecodes() throws {
        // Verbatim shape of a heartbeat written by an already-installed binary. New
        // fields must stay optional or an upgrade would hide the last record.
        let legacy = Data(#"{"detail":"LoginWindow agent stopped","pid":279,"uptime":96.32592204166667,"inputChecked":false,"phase":"stopped","captureChecked":false,"port":47990,"version":1}"#.utf8)
        let decoded = try JSONDecoder().decode(LoginWindowState.self, from: legacy)
        XCTAssertNil(decoded.previousPhase); XCTAssertNil(decoded.previousDetail); XCTAssertNil(decoded.recordedAt)
        XCTAssertTrue(decoded.isWellFormedRecord)
        XCTAssertFalse(decoded.ownsPort(at: 12))
    }
    func testSnapshotFieldsAreBoundedAndCannotForgeAListener() throws {
        let object: [String: Any] = ["version": 1, "pid": 1, "uptime": 10, "phase": "stopped", "detail": "d", "port": 47990,
                                     "captureChecked": false, "inputChecked": false,
                                     "previousDetail": String(repeating: "x", count: 1025)]
        let decoded = try JSONDecoder().decode(LoginWindowState.self, from: JSONSerialization.data(withJSONObject: object))
        XCTAssertFalse(decoded.isWellFormedRecord)
        XCTAssertFalse(decoded.ownsPort(at: 11))
        // A crafted snapshot must not promote a stopped record into a live claim.
        var claiming = object
        claiming["previousDetail"] = "short"; claiming["previousPhase"] = "listening"
        let stopped = try JSONDecoder().decode(LoginWindowState.self, from: JSONSerialization.data(withJSONObject: claiming))
        XCTAssertFalse(stopped.ownsPort(at: 11))
    }
    func testConsoleOwnershipRulesLeaveNoGapBetweenBootAndAgent() {
        // A missing console user means the login window owns the display: the boot
        // helper already reads it that way, and the graphical agent must not wait for
        // a session that has already arrived.
        XCTAssertTrue(ConsoleSession.isPreLogin(consoleUser: nil, uid: 0))
        XCTAssertFalse(ConsoleSession.isLoggedIn(consoleUser: nil, uid: 0))
        XCTAssertTrue(ConsoleSession.isPreLogin(consoleUser: "loginwindow", uid: 0))
        XCTAssertTrue(ConsoleSession.isPreLogin(consoleUser: "_mbsetupuser", uid: 248))
        XCTAssertFalse(ConsoleSession.isLoggedIn(consoleUser: "_mbsetupuser", uid: 248))
        XCTAssertTrue(ConsoleSession.isLoggedIn(consoleUser: "caoenming", uid: 501))
        XCTAssertFalse(ConsoleSession.isPreLogin(consoleUser: "caoenming", uid: 501))
        // System accounts never own a desktop session, whatever the uid range.
        XCTAssertFalse(ConsoleSession.isLoggedIn(consoleUser: "_windowserver", uid: 88))
        XCTAssertTrue(ConsoleSession.isPreLogin(consoleUser: "_windowserver", uid: 88))
        // The predicates must stay exact complements inside the accepted range: a gap
        // is what silently blocked pre-login capture.
        let cases: [(String?, uid_t)] = [(nil, 0), ("loginwindow", 0), ("_mbsetupuser", 248), ("caoenming", 501), ("root", 0)]
        for (name, uid) in cases {
            XCTAssertNotEqual(ConsoleSession.isLoggedIn(consoleUser: name, uid: uid), ConsoleSession.isPreLogin(consoleUser: name, uid: uid))
        }
    }
}
