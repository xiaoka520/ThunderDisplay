import XCTest
import Wire
@testable import InputSupport

final class QueuedInputControllerTests: XCTestCase {
    private final class Recorder: InputControlling {
        var received: [Input] = [], releases = 0, fail = false
        func apply(_ input: Input) throws {
            if fail { throw InputPostingError.permissionDenied }
            received.append(input)
        }
        func releaseAll() { releases += 1 }
    }
    private func input(_ kind: UInt8, _ code: UInt16 = 0) throws -> Input {
        var data = Data([3, kind, UInt8(code >> 8), UInt8(code & 255), 0, 1])
        data.append(Data(repeating: 0, count: 8)); return try Input(data)
    }
    func testMouseCoalescingPreservesClickAndKeyOrderingAndBoundedBacklog() throws {
        var jobs: [() -> Void] = []
        let recorder = Recorder()
        let queue = QueuedInputController(capacity: 3, allowed: { true }, factory: { recorder },
            onFailure: { XCTFail($0) }, schedule: { jobs.append($0) })
        for _ in 0..<1000 { try queue.apply(input(1)) }
        try queue.apply(input(2)); try queue.apply(input(3, 65))
        XCTAssertThrowsError(try queue.apply(input(3, 66)))
        XCTAssertEqual(jobs.count, 1); XCTAssertTrue(recorder.received.isEmpty)
        jobs.removeFirst()()
        XCTAssertEqual(recorder.received.map(\.kind), [1, 2, 3])
        XCTAssertEqual(recorder.received.last?.code, 65)
    }
    func testDisconnectDropsQueuedKeysAndReleasesAlreadyPostedKeys() throws {
        var jobs: [() -> Void] = []
        let recorder = Recorder()
        let queue = QueuedInputController(allowed: { true }, factory: { recorder },
            onFailure: { XCTFail($0) }, schedule: { jobs.append($0) })
        try queue.apply(input(3, 65)); jobs.removeFirst()()
        try queue.apply(input(3, 66)); queue.releaseAll()
        while !jobs.isEmpty { jobs.removeFirst()() }
        XCTAssertEqual(recorder.received.map(\.code), [65])
        XCTAssertEqual(recorder.releases, 1)
        XCTAssertThrowsError(try queue.apply(input(3, 67)))
    }
    func testLoginHandoverCancelsBeforeCreatingOrPostingInput() throws {
        var jobs: [() -> Void] = [], allowed = true, created = false
        let queue = QueuedInputController(allowed: { allowed }, factory: { created = true; return Recorder() },
            onFailure: { XCTFail($0) }, schedule: { jobs.append($0) })
        try queue.apply(input(3, 65)); allowed = false
        while !jobs.isEmpty { jobs.removeFirst()() }
        XCTAssertFalse(created)
        XCTAssertThrowsError(try queue.apply(input(3, 66)))
    }
    func testPostingErrorClosesQueueAndReportsFailureOnce() throws {
        var jobs: [() -> Void] = [], errors: [String] = []
        let recorder = Recorder(); recorder.fail = true
        let queue = QueuedInputController(allowed: { true }, factory: { recorder },
            onFailure: { errors.append($0) }, schedule: { jobs.append($0) })
        try queue.apply(input(3, 65)); try queue.apply(input(3, 66))
        while !jobs.isEmpty { jobs.removeFirst()() }
        XCTAssertEqual(errors.count, 1); XCTAssertEqual(recorder.releases, 1)
        XCTAssertTrue(recorder.received.isEmpty)
    }
}
