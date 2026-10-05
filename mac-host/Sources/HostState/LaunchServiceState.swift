import Foundation

public enum LaunchServiceState: Equatable {
    case running, stopped, failed(Int), unavailable
    public static func parseInstalledDaemon(_ output: String, succeeded: Bool, plistPath: String) -> Self {
        let lines = output.split(separator: "\n").map { $0.trimmingCharacters(in: .whitespaces) }
        // A submitted ServiceManagement job is not the installed system daemon,
        // even if its label or process happens to look correct.
        guard lines.contains("path = " + plistPath) else { return .unavailable }
        return parse(output, succeeded: succeeded)
    }
    public static func parse(_ output: String, succeeded: Bool) -> Self {
        guard succeeded else { return .unavailable }
        let lines = output.split(separator: "\n").map { $0.trimmingCharacters(in: .whitespaces) }
        if lines.contains("state = running"), lines.contains(where: { $0.hasPrefix("pid = ") && (Int($0.dropFirst(6)) ?? 0) > 0 }) { return .running }
        if let line = lines.first(where: { $0.hasPrefix("last exit code = ") }),
           let code = Int(line.dropFirst("last exit code = ".count).split(separator: ":").first ?? ""), code != 0 { return .failed(code) }
        return .stopped
    }
}
