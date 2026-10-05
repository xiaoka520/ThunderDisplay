import CoreMedia
import CoreVideo
import Foundation
import Wire
import CoreGraphics

private func hasBT709Metadata(_ format: CMFormatDescription, desktopSRGB: Bool = false) -> Bool {
    let primaries = CMFormatDescriptionGetExtension(format, extensionKey: kCMFormatDescriptionExtension_ColorPrimaries) as? String
    let transfer = CMFormatDescriptionGetExtension(format, extensionKey: kCMFormatDescriptionExtension_TransferFunction) as? String
    let matrix = CMFormatDescriptionGetExtension(format, extensionKey: kCMFormatDescriptionExtension_YCbCrMatrix) as? String
    let fullRange = CMFormatDescriptionGetExtension(format, extensionKey: kCMFormatDescriptionExtension_FullRangeVideo) as? Bool
    return primaries == kCMFormatDescriptionColorPrimaries_ITU_R_709_2 as String &&
        transfer == (desktopSRGB ? kCVImageBufferTransferFunction_sRGB : kCMFormatDescriptionTransferFunction_ITU_R_709_2) as String &&
        matrix == kCMFormatDescriptionYCbCrMatrix_ITU_R_709_2 as String && fullRange != true
}

private func hasMain10Metadata(_ format: CMFormatDescription) -> Bool {
    guard let atoms = CMFormatDescriptionGetExtension(format, extensionKey: kCMFormatDescriptionExtension_SampleDescriptionExtensionAtoms) as? [String: Any],
          let record = atoms["hvcC"] as? Data, record.count >= 23 else { return false }
    // HEVCDecoderConfigurationRecord: general_profile_idc and bitDepth{Luma,Chroma}Minus8.
    return record[1] & 31 == 2 && record[17] & 7 == 2 && record[18] & 7 == 2
}

func encoderCheck(bitrate: UInt64 = 10_000_000, desktopSRGB: Bool = false) throws {
    var writer = Writer(); writer.put(UInt8(15)); writer.put(UInt16(2)); writer.put(UInt16(50000))
    writer.put(UInt16(640)); writer.put(UInt16(360)); writer.put(UInt16(60)); writer.put(bitrate); writer.put(UInt8(3))
    writer.bytes(Data(repeating: 48, count: 32))
    let hello = try Hello(writer.data)
    for codec in [Codec.hevc, .h264, .hevc10] {
        let queue = DispatchQueue(label: "ThunderDisplay.encoder-check")
        let engine = try CaptureEngine(hello: hello, codec: codec, queue: queue, desktopSRGB: desktopSRGB)
        log("Encoder check requested \(bitrate / 1_000_000) Mbps, accepted \(engine.effectiveBitrate / 1_000_000) Mbps for \(codec)")
        if let qp = engine.maximumFrameQP { log("Encoder check verified maximum frame QP \(qp) for \(codec)") }
        let completed = DispatchSemaphore(value: 0)
        var failure: String?, outputCount = 0, colorChecked = false
        queue.sync {
            engine.active = true
            engine.onFailure = { failure = $0; completed.signal() }
            engine.onFormat = { format in
                colorChecked = hasBT709Metadata(format, desktopSRGB: desktopSRGB) && (codec != .hevc10 || hasMain10Metadata(format))
                if !colorChecked { failure = "Hardware encoder returned missing or incorrect BT.709 color metadata" }
            }
            engine.onFrame = { data, pts, key in
                if data.count < 5 || Array(data.prefix(4)) != [0,0,0,1] { failure = "Invalid Annex B output" }
                if outputCount == 0 && !key { failure = "First encoded frame is not an IDR" }
                if outputCount == 2 && !key { failure = "Requested IDR was not produced" }
                log("Encoder check \(codec): frame \(outputCount), \(data.count) bytes, pts \(pts) us, key=\(key)")
                outputCount += 1; completed.signal(); return true
            }
        }
        defer { queue.sync { engine.stop() } }
        for index in 0..<3 {
            var pixel: CVPixelBuffer?
            let attrs = [kCVPixelBufferIOSurfacePropertiesKey: [:]] as CFDictionary
            let pixelFormat = codec == .hevc10 ? (index == 0 ? kCVPixelFormatType_ARGB2101010LEPacked : kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange) : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
            let status = CVPixelBufferCreate(nil, 640, 360, pixelFormat, attrs, &pixel)
            guard status == kCVReturnSuccess, let pixel else { throw HostError("Synthetic pixel buffer: \(status)") }
            CVPixelBufferLockBaseAddress(pixel, [])
            if pixelFormat == kCVPixelFormatType_ARGB2101010LEPacked, let base = CVPixelBufferGetBaseAddress(pixel) {
                let pixels = base.assumingMemoryBound(to: UInt32.self), stride = CVPixelBufferGetBytesPerRow(pixel) / 4
                for y in 0..<360 { for x in 0..<640 {
                    let gray = UInt32(x * 1023 / 639); pixels[y * stride + x] = 0xC0000000 | (gray << 20) | (gray << 10) | gray
                } }
            } else {
                for plane in 0..<2 {
                    if let base = CVPixelBufferGetBaseAddressOfPlane(pixel, plane) {
                        let bytes = CVPixelBufferGetBytesPerRowOfPlane(pixel, plane) * CVPixelBufferGetHeightOfPlane(pixel, plane)
                        if codec == .hevc10 {
                            let words = base.assumingMemoryBound(to: UInt16.self)
                            for j in 0..<(bytes / 2) { words[j] = UInt16(plane == 0 ? 64 + index * 120 : 512) << 6 }
                        } else { memset(base, plane == 0 ? Int32(32 + index * 30) : 128, bytes) }
                    }
                }
            }
            CVPixelBufferUnlockBaseAddress(pixel, [])
            queue.async { if index == 2 { engine.requestIDR() }; engine.encodeImage(pixel, time: CMTime(value: Int64(index), timescale: 60)) }
            guard completed.wait(timeout: .now() + 5) == .success else { throw HostError("Hardware encoder callback timed out") }
            if let failure = queue.sync(execute: { failure }) { throw HostError(failure) }
        }
        guard queue.sync(execute: { outputCount }) == 3 else { throw HostError("Incomplete hardware encoder output") }
        guard queue.sync(execute: { colorChecked }) else { throw HostError("Color metadata was not validated") }
    }
    print("Hardware HEVC / H.264 / HEVC Main10 encoder + \(desktopSRGB ? "sRGB desktop / BT.709 matrix" : "BT.709") color metadata checks passed at requested \(bitrate / 1_000_000) Mbps (synthetic frames; no screen capture or input injection; not a throughput benchmark).")
}

/// Requires existing screen permission. Logs metadata only, with no network or input.
func captureCheck(displayID: UInt32?, tenBit: Bool = false, nativePixels: Bool = false, cursorVisible: Bool = true, desktopSRGB: Bool = false) throws {
    guard CGPreflightScreenCaptureAccess() else { throw HostError("Authorize screen recording before --capture-check") }
    var writer = Writer(); writer.put(UInt8(1)); writer.put(UInt16(1)); writer.put(UInt16(50000))
    let mode = CGDisplayCopyDisplayMode(displayID ?? CGMainDisplayID())
    let scale = min(1, 4096 / Double(mode?.pixelWidth ?? 2560), 4096 / Double(mode?.pixelHeight ?? 1600))
    let width = nativePixels ? Int(Double(mode?.pixelWidth ?? 2560) * scale / 2) * 2 : 2560
    let height = nativePixels ? Int(Double(mode?.pixelHeight ?? 1600) * scale / 2) * 2 : 1600
    log("Capture diagnostic requested \(width)×\(height), native=\(nativePixels), Main10=\(tenBit), video cursor=\(cursorVisible)")
    writer.put(UInt16(width)); writer.put(UInt16(height)); writer.put(UInt16(60)); writer.put(UInt32(nativePixels ? 300_000_000 : 120_000_000)); writer.put(UInt8(2))
    writer.bytes(Data(repeating: 48, count: 32))
    let queue = DispatchQueue(label: "ThunderDisplay.capture-check")
    let codec: Codec = tenBit ? .hevc10 : .hevc
    let engine = try CaptureEngine(hello: Hello(writer.data), codec: codec, queue: queue, cursorVisible: cursorVisible, desktopSRGB: desktopSRGB)
    let completed = DispatchSemaphore(value: 0)
    var failure: String?, captured = false, colorChecked = false, inputChecked = false
    queue.sync {
        engine.onFailure = { failure = $0; completed.signal() }
        engine.onInputFormat = { format in
            inputChecked = format == (tenBit ? kCVPixelFormatType_ARGB2101010LEPacked : desktopSRGB ? kCVPixelFormatType_32BGRA : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)
            log("Capture diagnostic input pixel format \(format)")
        }
        engine.onFormat = { format in
            let dimensions = CMVideoFormatDescriptionGetDimensions(format)
            colorChecked = dimensions.width == width && dimensions.height == height && hasBT709Metadata(format, desktopSRGB: desktopSRGB) && (!tenBit || hasMain10Metadata(format))
            if !colorChecked { failure = "Desktop encoder returned missing or incorrect BT.709 color metadata" }
        }
        engine.onFrame = { data, pts, key in
            if !captured {
                captured = true
                if !key || data.count < 5 || Array(data.prefix(4)) != [0,0,0,1] { failure = "Invalid initial capture bitstream" }
                log("Real capture check: \(width)×\(height) \(tenBit ? "HEVC Main10 / 10-bit RGB capture → P010" : "HEVC / 8-bit"), \(data.count) bytes, pts \(pts), key=\(key), viewport \(engine.contentRect)")
                completed.signal()
            }
            return true
        }
    }
    Task {
        do { _ = try await engine.start(displayID: displayID); queue.async { engine.active = true; engine.requestIDR() } }
        catch { queue.async { failure = error.localizedDescription; completed.signal() } }
    }
    let deadline = Date().addingTimeInterval(8)
    var done = false
    while Date() < deadline {
        if completed.wait(timeout: .now()) == .success { done = true; break }
        _ = RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.05))
        queue.async { engine.requestIDR() }
    }
    queue.sync { engine.stop() }
    guard done else { throw HostError("Real capture timed out") }
    if let failure = queue.sync(execute: { failure }) { throw HostError(failure) }
    guard queue.sync(execute: { inputChecked }) else { throw HostError("Capture pixel precision was not validated") }
    guard queue.sync(execute: { captured }) else { throw HostError("No desktop frame captured") }
    guard queue.sync(execute: { colorChecked }) else { throw HostError("Desktop color metadata was not validated") }
    print("\(tenBit ? "10-bit " : "8-bit ")ScreenCaptureKit → VideoToolbox capture + \(desktopSRGB ? "sRGB desktop / BT.709 matrix" : "BT.709") color metadata check passed. No image was saved or sent.")
}
