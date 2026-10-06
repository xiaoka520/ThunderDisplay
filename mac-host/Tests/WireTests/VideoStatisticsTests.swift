import XCTest
@testable import Wire

final class VideoStatisticsTests: XCTestCase {
    private func report(session: UInt64 = 7, values: [UInt32] = [1,2,3,4,5,6,7,8,9]) -> Data {
        var writer=Writer(); writer.put(Message.videoStatistics.rawValue); writer.put(UInt8(1)); writer.put(session)
        for value in values { writer.put(value) }; return writer.data
    }
    func testAggregateReportRequiresExactSessionAndShape() throws {
        let data=report()
        XCTAssertEqual(try VideoStatistics(data,session:7).values,[1,2,3,4,5,6,7,8,9])
        XCTAssertThrowsError(try VideoStatistics(data,session:8))
        XCTAssertThrowsError(try VideoStatistics(Data(data.dropLast()),session:7))
        XCTAssertThrowsError(try VideoStatistics(data+Data([0]),session:7))
        var invalid=data; invalid[1]=2; XCTAssertThrowsError(try VideoStatistics(invalid,session:7))
    }
    func testBoundsRejectMalformedDurationsWithoutAcceptingArbitraryLogText() throws {
        XCTAssertThrowsError(try VideoStatistics(report(values:[2,1,0,0,0,0,0,0,0]),session:7))
        XCTAssertThrowsError(try VideoStatistics(report(values:[0,60_000_001,0,0,0,0,0,0,0]),session:7))
        XCTAssertEqual(try VideoStatistics(report(values:[0,60_000_000,0,0,0,0,0,0,UInt32.max]),session:7).values.last,UInt32.max)
    }
}
