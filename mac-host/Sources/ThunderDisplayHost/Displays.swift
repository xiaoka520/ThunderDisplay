import AppKit
import Wire

struct HostDisplay: Equatable, Sendable {
    let id: CGDirectDisplayID, name: String
    let width: Int, height: Int, currentHz: Int, captureHz: Int
    let logicalWidth: Int, logicalHeight: Int
    let maximumWidth: Int, maximumHeight: Int, maximumHz: Int
    let desktopBits: Int, hdr: Bool, wideColor: Bool
    var currentText: String { "\(logicalWidth) × \(logicalHeight) · \(currentHz > 0 ? String(currentHz) : ui("可变", "variable")) Hz" }
    var renderText: String { "\(width) × \(height)" }
    var maximumText: String { "\(maximumWidth) × \(maximumHeight) · \(maximumHz) Hz" }
    var colorText: String {
        let depth = desktopBits > 0 ? "\(desktopBits)-bit " + ui("桌面通道", "desktop channel") : ui("桌面色深未知", "Desktop precision unknown")
        return depth + " · " + (hdr ? ui("支持 HDR / EDR", "HDR / EDR available") : "SDR") + (wideColor ? " · P3" : "")
    }
    var payload: Data {
        ProtocolWire.capabilities(width: UInt32(width), height: UInt32(height), hz: UInt16(captureHz),
            maximumWidth: UInt32(maximumWidth), maximumHeight: UInt32(maximumHeight), maximumHz: UInt16(maximumHz),
            flags: (hdr ? 1 : 0) | (wideColor ? 2 : 0), name: name,
            codecMask: CaptureEngine.supportsMain10 ? 7 : 3, streamBits: CaptureEngine.supportsMain10 ? 10 : 8)
    }
}

func hostDisplays() -> [HostDisplay] {
    NSScreen.screens.compactMap { screen -> HostDisplay? in
        guard let number = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber else { return nil }
        let id = CGDirectDisplayID(number.uint32Value)
        guard let mode = CGDisplayCopyDisplayMode(id) else { return nil }
        let width = mode.pixelWidth, height = mode.pixelHeight
        let rawHz = mode.refreshRate
        let currentHz = rawHz > 1 ? Int(rawHz.rounded()) : 0
        // VRR modes can report 0. NSScreen supplies the maximum rate of the active screen.
        let captureHz = min(1000, max(1, currentHz > 0 ? currentHz : screen.maximumFramesPerSecond))
        var maxW = width, maxH = height, maxHz = captureHz
        if let modes = CGDisplayCopyAllDisplayModes(id, [kCGDisplayShowDuplicateLowResolutionModes: true] as CFDictionary) as? [CGDisplayMode] {
            for candidate in modes where candidate.isUsableForDesktopGUI() {
                let w = candidate.pixelWidth, h = candidate.pixelHeight
                let hz = candidate.refreshRate > 1 ? Int(candidate.refreshRate.rounded()) : screen.maximumFramesPerSecond
                if w * h > maxW * maxH || (w * h == maxW * maxH && hz > maxHz) { maxW = w; maxH = h; maxHz = hz }
            }
        }
        return HostDisplay(id: id, name: screen.localizedName, width: width, height: height,
            currentHz: currentHz, captureHz: captureHz,
            logicalWidth: Int(CGDisplayBounds(id).width.rounded()), logicalHeight: Int(CGDisplayBounds(id).height.rounded()),
            maximumWidth: maxW, maximumHeight: maxH,
            maximumHz: min(1000, max(1, maxHz)), desktopBits: screen.depth.bitsPerSample,
            hdr: screen.maximumPotentialExtendedDynamicRangeColorComponentValue > 1,
            wideColor: screen.canRepresent(.p3))
    }
}
