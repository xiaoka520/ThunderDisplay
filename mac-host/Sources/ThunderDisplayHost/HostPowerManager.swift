import Foundation
import IOKit.pwr_mgt

final class HostPowerManager {
    private var assertion: IOPMAssertionID?
    private(set) var error: String?
    var active: Bool { assertion != nil }
    func update(enabled: Bool) {
        if !enabled {
            if let assertion { IOPMAssertionRelease(assertion) }
            assertion = nil; error = nil; return
        }
        guard assertion == nil else { return }
        var id: IOPMAssertionID = 0
        let result = IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleSystemSleep as CFString,
            IOPMAssertionLevel(kIOPMAssertionLevelOn), "ThunderDisplay host available over Thunderbolt" as CFString, &id)
        if result == kIOReturnSuccess { assertion = id; error = nil }
        else { error = "\(result)" }
    }
    deinit { if let assertion { IOPMAssertionRelease(assertion) } }
}
