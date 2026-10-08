import Foundation

public struct ClientDiagnostics {
    public enum State: UInt8 { case stop = 0, start, batch }
    public let state: State
    public let lines: [String]
    public init(_ data: Data, session: UInt64) throws {
        guard data.count >= 11, data.count <= 4011 else { throw WireError.malformed }
        var r = Reader(data)
        guard try r.get(UInt8.self) == Message.clientDiagnostics.rawValue,
              try r.get(UInt8.self) == 1, session != 0, try r.get(UInt64.self) == session,
              let state = State(rawValue: try r.get(UInt8.self)) else { throw WireError.malformed }
        let body = data.dropFirst(11)
        guard state == .batch ? !body.isEmpty : body.isEmpty,
              body.allSatisfy({ $0 == 10 || (32...126).contains($0) }) else { throw WireError.malformed }
        let lines = String(decoding: body, as: UTF8.self).split(separator: "\n").map(String.init)
        guard lines.count <= 33, lines.allSatisfy({ $0.utf8.count <= 1024 }) else { throw WireError.malformed }
        self.state = state; self.lines = lines
    }
}
