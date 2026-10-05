import XCTest
@testable import HostState

final class HostRecoveryStateTests: XCTestCase {
    func testAppStartsHostWithoutAButtonClick() {
        let state = HostRecoveryState()
        XCTAssertTrue(state.requested)
        XCTAssertTrue(state.canStart(at: 0))
    }
    func testSleepDisplayLossAndWakePreserveIntent() {
        var state = HostRecoveryState()
        state.willSleep()
        state.displaysChanged(at: 10)
        XCTAssertTrue(state.requested)
        XCTAssertFalse(state.canStart(at: 1000))
        state.didWake(at: 1000)
        XCTAssertFalse(state.canStart(at: 1000.9))
        XCTAssertTrue(state.canStart(at: 1001))
        state.started()
        XCTAssertTrue(state.canStart(at: 1001))
    }
    func testDisplayLossBeforeSystemSleepAlsoRecovers() {
        var state = HostRecoveryState()
        state.displaysChanged(at: 0)
        XCTAssertTrue(state.requested)
        state.willSleep()
        state.didWake(at: 5)
        state.displaysChanged(at: 5.5)
        XCTAssertFalse(state.canStart(at: 6))
        XCTAssertTrue(state.canStart(at: 6.5))
    }
    func testTemporaryBridgeAndCaptureFaultsRetryWithoutManualStart() {
        var state = HostRecoveryState()
        for time in [0.0, 3.0, 6.0] {
            state.failed(at: time)
            XCTAssertTrue(state.requested)
            XCTAssertFalse(state.canStart(at: time + 2.9))
            XCTAssertTrue(state.canStart(at: time + 3))
        }
        state.started()
        XCTAssertTrue(state.canStart(at: 9))
    }
    func testExplicitPauseIsDistinctFromSystemInterruption() {
        var state = HostRecoveryState()
        state.pause(); state.willSleep(); state.didWake(at: 10)
        state.displaysChanged(at: 12)
        XCTAssertFalse(state.canStart(at: 1000))
        state.start(at: 1000)
        XCTAssertTrue(state.canStart(at: 1000))
    }
}
