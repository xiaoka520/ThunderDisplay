import Foundation

// Aggregate durations only. No screen contents, input or cross-device clock
// subtraction. Capability v9 explicitly opts in to this session-bound report.
public struct VideoStatistics {
    public let values: [UInt32]
    public init(_ data: Data, session: UInt64) throws {
        guard data.count == 46 else { throw WireError.malformed }
        var reader=Reader(data)
        guard try reader.get(UInt8.self) == Message.videoStatistics.rawValue,
              try reader.get(UInt8.self) == 1, try reader.get(UInt64.self) == session else { throw WireError.malformed }
        var durations=[UInt32]()
        for _ in 0..<9 { durations.append(try reader.get(UInt32.self)) }
        for i in stride(from:0,to:8,by:2) {
            guard durations[i]<=durations[i+1], durations[i+1]<=60_000_000 else { throw WireError.malformed }
        }
        values=durations
    }
}
