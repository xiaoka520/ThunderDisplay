import XCTest
@testable import Wire

final class VideoMailboxTests: XCTestCase {
    private func frame(_ id: UInt32, key: Bool = false) -> OutgoingVideoFrame {
        OutgoingVideoFrame(data: Data([1,2,3]), pts: UInt64(id)*1000, id: id, key: key)
    }
    func testBoundedQueueRetiresDependenciesAndResumesFromIDR() {
        let mailbox=VideoMailbox()
        XCTAssertFalse(mailbox.push(frame(1)).accepted)
        let first=mailbox.push(frame(2,key:true)); XCTAssertTrue(first.accepted && first.start)
        XCTAssertEqual(mailbox.next()?.id,2)
        let pending=mailbox.push(frame(3)); XCTAssertTrue(pending.accepted); XCTAssertFalse(pending.start)
        XCTAssertFalse(mailbox.push(frame(4)).accepted)
        XCTAssertNil(mailbox.next()); XCTAssertFalse(mailbox.push(frame(5)).accepted)
        let resumed=mailbox.push(frame(6,key:true)); XCTAssertTrue(resumed.accepted && resumed.start)
        XCTAssertEqual(mailbox.next()?.id,6)
        XCTAssertTrue(mailbox.push(frame(7)).accepted)
        XCTAssertTrue(mailbox.push(frame(8,key:true)).accepted)
        XCTAssertEqual(mailbox.next()?.id,8)
        mailbox.discontinuity(); XCTAssertFalse(mailbox.push(frame(9)).accepted)
        XCTAssertTrue(mailbox.push(frame(10,key:true)).accepted)
        mailbox.stop(); XCTAssertTrue(mailbox.cancelled); XCTAssertNil(mailbox.next())
        XCTAssertFalse(mailbox.push(frame(11,key:true)).accepted)
    }
    func testBlockedSenderDoesNotBlockProducerOrCancellation() {
        let mailbox=VideoMailbox(), entered=DispatchSemaphore(value:0), release=DispatchSemaphore(value:0)
        XCTAssertTrue(mailbox.push(frame(1,key:true)).accepted)
        let finished=expectation(description:"sender exits")
        DispatchQueue.global().async {
            XCTAssertNotNil(mailbox.next()); entered.signal(); release.wait(); finished.fulfill()
        }
        XCTAssertEqual(entered.wait(timeout:.now()+1),.success)
        // The consumer is blocked outside the mailbox lock; input/control can
        // enqueue or cancel without waiting for hundreds of video packets.
        XCTAssertTrue(mailbox.push(frame(2)).accepted)
        XCTAssertFalse(mailbox.push(frame(3)).accepted)
        mailbox.stop(); XCTAssertTrue(mailbox.cancelled)
        release.signal(); wait(for:[finished],timeout:1)
    }
}
