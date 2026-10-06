import XCTest
import Dispatch
@testable import HostState

final class PreparationWatchdogTests: XCTestCase {
    func testTimeoutRunsWhileCallingThreadIsBlocked() {
        let signal = DispatchSemaphore(value: 0)
        let watchdog = PreparationWatchdog { stage in
            XCTAssertEqual(stage, "System HID input")
            signal.signal()
        }
        watchdog.arm("System HID input", timeout: 0.02)
        // No AppKit / main run loop is pumped while a system call blocks.
        XCTAssertEqual(signal.wait(timeout: .now() + 1), .success)
        XCTAssertEqual(signal.wait(timeout: .now() + 0.2), .timedOut) // Fires once.
        withExtendedLifetime(watchdog) {}
    }
    func testCompletedPreparationDoesNotTimeout() {
        let signal = DispatchSemaphore(value: 0)
        let watchdog = PreparationWatchdog { _ in signal.signal() }
        watchdog.arm("Frame", timeout: 0.05)
        watchdog.disarm()
        XCTAssertEqual(signal.wait(timeout: .now() + 0.2), .timedOut)
        withExtendedLifetime(watchdog) {}
    }
    func testNewPreparationReplacesPreviousDeadline() {
        let signal = DispatchSemaphore(value: 0)
        let watchdog = PreparationWatchdog { stage in
            XCTAssertEqual(stage, "Keyboard / mouse event preparation")
            signal.signal()
        }
        watchdog.arm("Frame", timeout: 0.02)
        watchdog.arm("Keyboard / mouse event preparation", timeout: 0.3)
        XCTAssertEqual(signal.wait(timeout: .now() + 0.15), .timedOut)
        XCTAssertEqual(signal.wait(timeout: .now() + 1), .success)
        withExtendedLifetime(watchdog) {}
    }
    func testFinishingOldOperationCannotCancelNewPreparation() {
        let signal = DispatchSemaphore(value: 0)
        let watchdog = PreparationWatchdog { stage in
            XCTAssertEqual(stage, "New connection")
            signal.signal()
        }
        let old = watchdog.arm("Old connection", timeout: 1)
        watchdog.arm("New connection", timeout: 0.02)
        watchdog.disarm(ticket: old)
        XCTAssertEqual(signal.wait(timeout: .now() + 1), .success)
        withExtendedLifetime(watchdog) {}
    }
}
