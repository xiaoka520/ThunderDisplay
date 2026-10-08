import CoreMedia
import CoreVideo
import VideoToolbox
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
        log("Encoder check \(codec): verified speed priority \(engine.encodingSpeedPrioritized)")
        if let qp = engine.maximumFrameQP { log("Encoder check verified maximum frame QP \(qp) for \(codec)") }
        let completed = DispatchSemaphore(value: 0)
        var failure: String?, outputCount = 0, colorChecked = false, inputCount = 0
        var previousPTS: UInt64?
        queue.sync {
            engine.active = true
            engine.onInputFormat = { _ in inputCount += 1 }
            engine.onFailure = { failure = $0; completed.signal() }
            engine.onFormat = { format in
                colorChecked = hasBT709Metadata(format, desktopSRGB: desktopSRGB) && (codec != .hevc10 || hasMain10Metadata(format))
                if !colorChecked { failure = "Hardware encoder returned missing or incorrect BT.709 color metadata" }
            }
            engine.onFrame = { data, pts, key in
                if let previousPTS, pts<=previousPTS { failure = "Encoder returned out-of-order timestamps" }; previousPTS=pts
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
        // A burst must respect the announced one/two-frame hardware bound.
        // Excess submissions are refused instead of creating a codec FIFO.
        var burst: CVPixelBuffer?
        let format=codec == .hevc10 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
        guard CVPixelBufferCreate(nil,640,360,format,[kCVPixelBufferIOSurfacePropertiesKey:[:]] as CFDictionary,&burst)==kCVReturnSuccess,
              let burst else { throw HostError("Synthetic pipeline allocation failed") }
        CVPixelBufferLockBaseAddress(burst,[])
        for plane in 0..<2 {
            let size=CVPixelBufferGetBytesPerRowOfPlane(burst,plane)*CVPixelBufferGetHeightOfPlane(burst,plane)
            if let base=CVPixelBufferGetBaseAddressOfPlane(burst,plane) { memset(base,plane == 0 ? 64:128,size) }
        }
        CVPixelBufferUnlockBaseAddress(burst,[])
        let limit = engine.hardwareAdmissionLimit
        queue.async { for i in 3..<(3+limit+2) { engine.encodeImage(burst,time:CMTime(value:Int64(i),timescale:60)) } }
        for _ in 0..<limit {
            guard completed.wait(timeout:.now()+5) == .success else { throw HostError("Bounded pipeline callback timed out") }
        }
        guard queue.sync(execute:{outputCount==3+limit && inputCount==3+limit}) else { throw HostError("Hardware admission bound was not respected") }
        if let failure=queue.sync(execute:{failure}) { throw HostError(failure) }
        log("Encoder bounded pipeline check \(codec): \(limit) ordered outputs, extra admissions refused")
    }
    print("Hardware HEVC / H.264 / HEVC Main10 encoder + \(desktopSRGB ? "sRGB desktop / BT.709 matrix" : "BT.709") color metadata checks passed at requested \(bitrate / 1_000_000) Mbps (synthetic frames; no screen capture or input injection; not a throughput benchmark).")
}

/// Measure the production capture/color/encoding path without networking or input.
/// Use a moving test window for comparisons; idle desktops intentionally send fewer frames.
func captureRateCheck(displayID: UInt32?, raw: Bool = false, updates: Bool = false, depth: Int = 3, unthrottled: Bool = false) throws {
    guard CGPreflightScreenCaptureAccess() else { throw HostError("Authorize screen recording before --capture-rate-check") }
    let id = displayID ?? CGMainDisplayID()
    guard let mode = CGDisplayCopyDisplayMode(id) else { throw HostError("Selected display is unavailable") }
    let scale = min(1, 4096 / Double(mode.pixelWidth), 4096 / Double(mode.pixelHeight))
    let width = UInt16(Int(Double(mode.pixelWidth) * scale / 2) * 2)
    let height = UInt16(Int(Double(mode.pixelHeight) * scale / 2) * 2)
    var writer = Writer(); writer.put(Message.helloWide.rawValue); writer.put(UInt16(2)); writer.put(UInt16(50000))
    writer.put(width); writer.put(height); writer.put(UInt16(60)); writer.put(UInt64(2_000_000_000)); writer.put(UInt8(raw ? 16 : 4))
    writer.bytes(Data(repeating: 48, count: 32))
    let queue = DispatchQueue(label: "ThunderDisplay.capture-rate-check", qos: .userInteractive)
    let engine = try CaptureEngine(hello: Hello(writer.data), codec: raw ? .rawPacked10 : .hevc10, queue: queue, cursorVisible: !raw, desktopSRGB: true,
        captureQueueDepth: depth, unthrottledCapture: unthrottled)
    var failure: String?, frames = 0, began: UInt64 = 0, bytes = 0
    var baseline: Data?, updateBytes = 0, updateTime: UInt64 = 0
    queue.sync {
        engine.onFailure = { failure = $0 }
        engine.onFrame = { data, _, _ in
            if raw && data.count != (try? RawVideoWire.byteCount(width: Int(width), height: Int(height), packed: true)) { failure = "Invalid packed ten-bit frame size"; return false }
            if raw && frames == 0 {
                log("Raw packed ten-bit full-frame size verified; no unused P010 word bits transmitted")
            }
            if updates {
                let start = DispatchTime.now().uptimeNanoseconds
                do {
                    updateBytes += try RawVideoWire.update(data, baseline: baseline, baseID: UInt32(frames), width: Int(width), height: Int(height)).count
                    baseline = data; updateTime += DispatchTime.now().uptimeNanoseconds-start
                } catch { failure = "Raw update construction failed"; return false }
            }
            frames += 1; bytes += data.count; return true
        }
    }
    Task {
        do {
            _ = try await engine.start(displayID: id)
            queue.async { began = DispatchTime.now().uptimeNanoseconds; engine.active = true; engine.requestIDR() }
        } catch { queue.async { failure = error.localizedDescription } }
    }
    let deadline = Date().addingTimeInterval(7)
    while Date() < deadline {
        if queue.sync(execute: { failure != nil }) { break }
        _ = RunLoop.current.run(mode: .default, before: min(deadline, Date().addingTimeInterval(0.05)))
    }
    let result = queue.sync { () -> (Int, Int, Double, String?) in
        let seconds = began == 0 ? 0 : Double(DispatchTime.now().uptimeNanoseconds - began) / 1e9
        let result = (frames, bytes, seconds, failure); engine.stop(); return result
    }
    if let failure = result.3 { throw HostError(failure) }
    guard result.0 > 0, result.2 > 0 else { throw HostError("Capture rate check returned no frames") }
    log(raw ? "Capture rate diagnostic: raw P010 10-bit; capture/color/copy only; no compression" : "Capture rate diagnostic: Main10 sRGB; capture/color/encoding only")
    log(String(format: "Capture rate result: %dx%d; %.1f fps; %d frames; %.1f Mbps",
        Int(width), Int(height), Double(result.0) / result.2, result.0, Double(result.1) * 8 / result.2 / 1e6))
    if updates { log(String(format: "Exact update capture diagnostic: %.3f Gbps; bytes/frame %d; compare/copy us avg %llu; queue %d; unthrottled %@; no network",
        Double(updateBytes)*8/result.2/1e9, updateBytes/max(1,result.0), updateTime/UInt64(max(1,result.0))/1000, depth, unthrottled ? "true" : "false")) }
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
