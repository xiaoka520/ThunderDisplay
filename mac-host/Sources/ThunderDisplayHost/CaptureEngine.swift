import CoreMedia
import ScreenCaptureKit
import VideoToolbox
import Wire

// All mutable state is accessed on queue; startCapture bridges back through queue.sync.
final class CaptureEngine: NSObject, SCStreamOutput, SCStreamDelegate, @unchecked Sendable {
    let codec: Codec
    private(set) var effectiveBitrate: UInt64
    private(set) var maximumFrameQP: Int?
    let desktopSRGB: Bool
    private let cursorVisible: Bool
    private let hello: Hello, queue: DispatchQueue
    private var compression: VTCompressionSession?, stream: SCStream?
    private var transfer: VTPixelTransferSession?, tenBitPool: CVPixelBufferPool?
    var onInputFormat: ((OSType) -> Void)?
    static let supportsMain10: Bool = {
        var w = Writer(); w.put(UInt8(1)); w.put(UInt16(1)); w.put(UInt16(50000))
        w.put(UInt16(640)); w.put(UInt16(360)); w.put(UInt16(60)); w.put(UInt32(10_000_000)); w.put(UInt8(4)); w.bytes(Data(repeating: 0, count: 32))
        let queue = DispatchQueue(label: "ThunderDisplay.Main10-probe")
        guard let hello = try? Hello(w.data), let engine = try? CaptureEngine(hello: hello, codec: .hevc10, queue: queue) else { return false }
        queue.sync { engine.stop() }; return true
    }()
    private var busy = false, forceKey = true, stopped = false
    var active = false
    private(set) var contentRect = CGRect.zero
    var onFrame: ((Data, UInt64, Bool) -> Bool)?
    var onFailure: ((String) -> Void)?
    var onFormat: ((CMFormatDescription) -> Void)?
    private var origin: CMTime?
    private var latestImage: CVPixelBuffer?, latestTime = CMTime.zero, lastPresentation: CMTime?
    private var submitted = 0, dropped = 0, encoded = 0
    private var consecutiveEncoderDrops = 0
    private var lastStats = DispatchTime.now().uptimeNanoseconds
    private static let background = CGColor(gray: 0, alpha: 1)

    init(hello: Hello, codec: Codec, queue: DispatchQueue, cursorVisible: Bool = true, desktopSRGB: Bool = false) throws {
        effectiveBitrate = hello.bitrate
        self.desktopSRGB = desktopSRGB
        self.hello = hello; self.codec = codec; self.queue = queue; self.cursorVisible = cursorVisible
        super.init()
        var status: OSStatus = -1
        // Low-latency rate control can accept tall frames / very high bitrates
        // but drop every frame. Use normal hardware rate control for these
        // sessions; no B frames and one-frame admission still bound latency.
        for lowLatency in (hello.height > 2304 || hello.bitrate > 300_000_000 ? [false] : [true, false]) {
            var spec: [CFString: Any] = [kVTVideoEncoderSpecification_RequireHardwareAcceleratedVideoEncoder: true]
            if lowLatency { spec[kVTVideoEncoderSpecification_EnableLowLatencyRateControl] = true }
            status = VTCompressionSessionCreate(allocator: nil, width: Int32(hello.width), height: Int32(hello.height),
            codecType: codec == .h264 ? kCMVideoCodecType_H264 : kCMVideoCodecType_HEVC,
            encoderSpecification: spec as CFDictionary, imageBufferAttributes: [kCVPixelBufferPixelFormatTypeKey: codec == .hevc10 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange] as CFDictionary, compressedDataAllocator: nil,
            outputCallback: { context, _, status, info, sample in
                guard let context else { return }
                let engine = Unmanaged<CaptureEngine>.fromOpaque(context).takeUnretainedValue()
                engine.queue.async { engine.output(status: status, info: info, sample: sample) }
            }, refcon: Unmanaged.passUnretained(self).toOpaque(), compressionSessionOut: &compression)
            if status == noErr { break }
            if let compression { VTCompressionSessionInvalidate(compression) }; compression = nil
        }
        guard status == noErr, let compression else { throw HostError("Hardware \(codec) encoder unavailable (\(status))") }
        do {
            try set(kVTCompressionPropertyKey_RealTime, kCFBooleanTrue)
            try set(kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse)
            // Desktop edges benefit from quality-first encoding; preserve one-frame admission.
            let qualityStatus = VTSessionSetProperty(compression, key: kVTCompressionPropertyKey_PrioritizeEncodingSpeedOverQuality, value: kCFBooleanFalse)
            if qualityStatus != noErr { log("Encoder does not expose quality priority (\(qualityStatus))") }
            try configureBitrate()
            try set(kVTCompressionPropertyKey_ExpectedFrameRate, NSNumber(value: hello.fps))
            try set(kVTCompressionPropertyKey_MaxKeyFrameInterval, NSNumber(value: max(1, Int(hello.fps) / 2)))
            try set(kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, NSNumber(value: 0.5))
            try set(kVTCompressionPropertyKey_ProfileLevel, codec == .hevc10 ? kVTProfileLevel_HEVC_Main10_AutoLevel : codec == .hevc ? kVTProfileLevel_HEVC_Main_AutoLevel : kVTProfileLevel_H264_High_AutoLevel)
            configureDetailPreservation()
            try set(kVTCompressionPropertyKey_ColorPrimaries, kCVImageBufferColorPrimaries_ITU_R_709_2)
            try set(kVTCompressionPropertyKey_TransferFunction, desktopSRGB ? kCVImageBufferTransferFunction_sRGB : kCVImageBufferTransferFunction_ITU_R_709_2)
            try set(kVTCompressionPropertyKey_YCbCrMatrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2)
            if codec == .hevc10 || desktopSRGB {
                let transferStatus = VTPixelTransferSessionCreate(allocator: nil, pixelTransferSessionOut: &transfer)
                guard transferStatus == noErr, let transfer else { throw HostError("10-bit pixel transfer unavailable: \(transferStatus)") }
                for (key, value) in [(kVTPixelTransferPropertyKey_DestinationColorPrimaries, kCVImageBufferColorPrimaries_ITU_R_709_2),
                                     (kVTPixelTransferPropertyKey_DestinationTransferFunction, desktopSRGB ? kCVImageBufferTransferFunction_sRGB : kCVImageBufferTransferFunction_ITU_R_709_2),
                                     (kVTPixelTransferPropertyKey_DestinationYCbCrMatrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2)] {
                    let result = VTSessionSetProperty(transfer, key: key, value: value)
                    guard result == noErr else { throw HostError("10-bit transfer color property: \(result)") }
                }
                let attributes = [kCVPixelBufferWidthKey: Int(hello.width), kCVPixelBufferHeightKey: Int(hello.height),
                    kCVPixelBufferPixelFormatTypeKey: codec == .hevc10 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                    kCVPixelBufferIOSurfacePropertiesKey: [:]] as CFDictionary
                let poolStatus = CVPixelBufferPoolCreate(nil, nil, attributes, &tenBitPool)
                guard poolStatus == kCVReturnSuccess else { throw HostError("10-bit pixel pool unavailable: \(poolStatus)") }
            }
            let delay = VTSessionSetProperty(compression, key: kVTCompressionPropertyKey_MaxFrameDelayCount, value: NSNumber(value: 1))
            if delay != noErr { log("Encoder does not expose MaxFrameDelayCount (\(delay)); one frame admission limit remains active") }
            let prepared = VTCompressionSessionPrepareToEncodeFrames(compression)
            guard prepared == noErr else { throw HostError("Prepare encoder: \(prepared)") }
        } catch { VTCompressionSessionInvalidate(compression); self.compression = nil; throw error }
    }
    private func configureBitrate() throws {
        let key = kVTCompressionPropertyKey_AverageBitRate
        func accepts(_ value: UInt64) -> Bool { VTSessionSetProperty(compression!, key: key, value: NSNumber(value: value)) == noErr }
        if accepts(hello.bitrate) { return }
        // Some hardware rejects ultra-high property values.
        // Probe the encoder's actual range in whole Mbps, then explicitly report
        // the accepted value in Welcome rather than truncating a UInt64 request.
        guard hello.wide, hello.bitrate > 1_000_000_000, accepts(1_000_000_000) else {
            throw HostError("Hardware encoder rejected AverageBitRate \(hello.bitrate / 1_000_000) Mbps")
        }
        var low: UInt64 = 1000, high = hello.bitrate / 1_000_000
        while low + 1 < high {
            let middle = low + (high - low) / 2
            if accepts(middle * 1_000_000) { low = middle } else { high = middle }
        }
        effectiveBitrate = low * 1_000_000
        try set(key, NSNumber(value: effectiveBitrate))
        log("Hardware bitrate limit: requested \(hello.bitrate / 1_000_000) Mbps, accepted \(effectiveBitrate / 1_000_000) Mbps")
    }
    private func configureDetailPreservation() {
        // A QP ceiling with an inadequate budget can force encoder frame drops.
        // Keep explicitly low custom rates usable; apply the ceiling only with
        // enough bits per source pixel per frame for the selected codec.
        let pixelsPerSecond = Double(hello.width) * Double(hello.height) * Double(hello.fps)
        let budget = Double(effectiveBitrate) / pixelsPerSecond
        guard budget >= (codec == .h264 ? 1.2 : 0.75) else {
            log("Encoder detail budget: \(effectiveBitrate / 1_000_000) Mbps; QP ceiling skipped for limited per-frame budget")
            return
        }
        let key = kVTCompressionPropertyKey_MaxAllowedFrameQP
        let result = VTSessionSetProperty(compression!, key: key, value: NSNumber(value: 22))
        guard result == noErr else {
            log("Encoder does not expose maximum frame QP (\(result)); higher bitrate budget remains active")
            return
        }
        var value: Unmanaged<CFTypeRef>?
        let copied = VTSessionCopyProperty(compression!, key: key, allocator: nil, valueOut: &value)
        let property = value?.takeRetainedValue()
        if copied == noErr, let number = property as? NSNumber, number.intValue == 22 {
            maximumFrameQP = number.intValue
            log("Encoder detail budget: \(effectiveBitrate / 1_000_000) Mbps; verified maximum frame QP \(number.intValue)")
        } else {
            log("Encoder accepted maximum frame QP request; readback unavailable (\(copied))")
        }
    }
    private func set(_ key: CFString, _ value: CFTypeRef) throws {
        let status = VTSessionSetProperty(compression!, key: key, value: value)
        guard status == noErr else { throw HostError("Encoder property \(key): \(status)") }
    }
    func start(displayID: UInt32?) async throws -> UInt32 {
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        guard let display = displayID.flatMap({ id in content.displays.first { $0.displayID == id } }) ??
                (displayID == nil ? content.displays.first { $0.displayID == CGMainDisplayID() } ?? content.displays.first : nil)
        else { throw HostError("Selected display is unavailable") }
        let config = SCStreamConfiguration()
        config.width = Int(hello.width); config.height = Int(hello.height)
        config.minimumFrameInterval = CMTime(value: 1, timescale: CMTimeScale(hello.fps))
        config.queueDepth = 3; config.showsCursor = cursorVisible; config.capturesAudio = false
        config.pixelFormat = codec == .hevc10 ? kCVPixelFormatType_ARGB2101010LEPacked : desktopSRGB ? kCVPixelFormatType_32BGRA : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
        config.colorSpaceName = desktopSRGB ? CGColorSpace.sRGB : CGColorSpace.itur_709
        config.colorMatrix = kCVImageBufferYCbCrMatrix_ITU_R_709_2
        config.scalesToFit = true
        let scale = min(CGFloat(hello.width) / CGFloat(display.width), CGFloat(hello.height) / CGFloat(display.height))
        let contentWidth = floor(CGFloat(display.width) * scale), contentHeight = floor(CGFloat(display.height) * scale)
        let rect = CGRect(x: floor((CGFloat(hello.width) - contentWidth) / 2), y: floor((CGFloat(hello.height) - contentHeight) / 2),
                          width: contentWidth, height: contentHeight)
        config.destinationRect = rect
        config.backgroundColor = Self.background
        if #available(macOS 14, *) { config.preservesAspectRatio = true }
        let stream = SCStream(filter: SCContentFilter(display: display, excludingApplications: [], exceptingWindows: []), configuration: config, delegate: self)
        try stream.addStreamOutput(self, type: .screen, sampleHandlerQueue: queue)
        try queue.sync {
            guard !stopped else { throw HostError("Capture cancelled") }
            contentRect = rect
            self.stream = stream
        }
        try await stream.startCapture()
        let cancelled = queue.sync { stopped }
        if cancelled { try? await stream.stopCapture(); throw HostError("Capture cancelled") }
        return display.displayID
    }
    func stop() {
        stopped = true; active = false; onFrame = nil; onFailure = nil; onFormat = nil; onInputFormat = nil; latestImage = nil
        if let transfer { VTPixelTransferSessionInvalidate(transfer) }; transfer = nil; tenBitPool = nil
        if let compression { VTCompressionSessionInvalidate(compression) }; compression = nil
        if let stream { Task { try? await stream.stopCapture() } }; stream = nil
    }
    func requestIDR() {
        forceKey = true
        // Ready and packet loss can occur while the desktop is idle.
        if active, !stopped, !busy, let latestImage {
            encodeImage(latestImage, time: origin == nil ? latestTime : CMClockGetTime(CMClockGetHostTimeClock()))
        }
    }
    func stream(_ stream: SCStream, didStopWithError error: Error) {
        queue.async { [weak self] in guard let self, !self.stopped else { return }; self.onFailure?(error.localizedDescription) }
    }
    func stream(_ stream: SCStream, didOutputSampleBuffer sample: CMSampleBuffer, of type: SCStreamOutputType) {
        guard !stopped, type == .screen, CMSampleBufferIsValid(sample), compression != nil,
              let attachments = CMSampleBufferGetSampleAttachmentsArray(sample, createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
              let status = attachments.first?[.status] as? Int, status == SCFrameStatus.complete.rawValue,
              let image = CMSampleBufferGetImageBuffer(sample) else { return }
        latestImage = image; latestTime = CMSampleBufferGetPresentationTimeStamp(sample)
        guard active else { return }
        guard !busy else { dropped += 1; return }
        encodeImage(image, time: CMSampleBufferGetPresentationTimeStamp(sample))
    }
    // Also used by the permission-free synthetic hardware encoder diagnostic.
    func encodeImage(_ image: CVPixelBuffer, time: CMTime) {
        guard active, !stopped, let compression, !busy else { return }
        var input = image
        let format = CVPixelBufferGetPixelFormatType(image); onInputFormat?(format)
        let expectedYUV = codec == .hevc10 ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
        if (codec == .hevc10 || desktopSRGB) && format != expectedYUV {
            let expectedRGB = codec == .hevc10 ? kCVPixelFormatType_ARGB2101010LEPacked : kCVPixelFormatType_32BGRA
            guard format == expectedRGB, let transfer, let tenBitPool else {
                onFailure?(desktopSRGB ? "DesktopColorUnavailable: Capture did not produce the requested RGB format" : "Main10Unavailable: Capture did not produce 10-bit pixels"); return
            }
            if desktopSRGB {
                // RGB capture is explicitly requested in sRGB. Preserve its curve
                // through YUV conversion, rather than applying a video OETF.
                CVBufferSetAttachment(image, kCVImageBufferCGColorSpaceKey, CGColorSpace(name: CGColorSpace.sRGB)!, .shouldPropagate)
                CVBufferSetAttachment(image, kCVImageBufferColorPrimariesKey, kCVImageBufferColorPrimaries_ITU_R_709_2, .shouldPropagate)
                CVBufferSetAttachment(image, kCVImageBufferTransferFunctionKey, kCVImageBufferTransferFunction_sRGB, .shouldPropagate)
            }
            var converted: CVPixelBuffer?
            let allocated = CVPixelBufferPoolCreatePixelBuffer(nil, tenBitPool, &converted)
            guard allocated == kCVReturnSuccess, let converted else { onFailure?("\(desktopSRGB ? "DesktopColorUnavailable" : "Main10Unavailable"): YUV buffer allocation failed (\(allocated))"); return }
            let result = VTPixelTransferSessionTransferImage(transfer, from: image, to: converted)
            guard result == noErr else { onFailure?("\(desktopSRGB ? "DesktopColorUnavailable" : "Main10Unavailable"): RGB to YUV conversion failed (\(result))"); return }
            input = converted
        }
        if origin == nil { origin = time }
        var presentation = CMTimeSubtract(time, origin!)
        if let lastPresentation, CMTimeCompare(presentation, lastPresentation) <= 0 {
            presentation = CMTimeAdd(lastPresentation, CMTime(value: 1, timescale: 1_000_000))
        }
        lastPresentation = presentation
        busy = true; submitted += 1
        let props = forceKey ? [kVTEncodeFrameOptionKey_ForceKeyFrame: true] as CFDictionary : nil
        forceKey = false
        let result = VTCompressionSessionEncodeFrame(compression, imageBuffer: input, presentationTimeStamp: presentation,
            duration: CMTime(value: 1, timescale: CMTimeScale(hello.fps)), frameProperties: props, sourceFrameRefcon: nil, infoFlagsOut: nil)
        if result != noErr { busy = false; forceKey = true; onFailure?("Encode frame: \(result)") }
    }
    private func output(status: OSStatus, info: VTEncodeInfoFlags, sample: CMSampleBuffer?) {
        defer { busy = false }
        guard !stopped, active else { return }
        guard status == noErr else { forceKey = true; onFailure?("Hardware encoder callback failed (\(status)); \(hello.width)×\(hello.height)"); return }
        guard !info.contains(.frameDropped), let sample, CMSampleBufferDataIsReady(sample) else {
            forceKey = true; consecutiveEncoderDrops += 1
            log("Hardware encoder dropped frame \(consecutiveEncoderDrops); \(hello.width)×\(hello.height), flags \(info.rawValue)")
            if consecutiveEncoderDrops >= 6 { onFailure?("Hardware encoder repeatedly dropped frames at \(hello.width)×\(hello.height)") }
            return
        }
        consecutiveEncoderDrops = 0
        do {
            let attachments = CMSampleBufferGetSampleAttachmentsArray(sample, createIfNecessary: false) as? [[CFString: Any]]
            let key = !((attachments?.first?[kCMSampleAttachmentKey_NotSync] as? Bool) ?? false)
            if let format = CMSampleBufferGetFormatDescription(sample) { onFormat?(format) }
            let frame = try annexB(sample, key: key)
            let seconds = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample))
            let pts = UInt64(max(0, seconds) * 1_000_000)
            if frame.count > ProtocolWire.maxFrameSize || onFrame?(frame, pts, key) != true { forceKey = true; dropped += 1 }
            else { encoded += 1 }
            let now = DispatchTime.now().uptimeNanoseconds
            if now - lastStats >= 5_000_000_000 {
                let elapsed = Double(now - lastStats) / 1_000_000_000
                log(String(format: "Encoded %.1f fps, admitted %d, dropped %d, %@", Double(encoded)/elapsed, submitted, dropped, String(describing: codec)))
                lastStats = now; submitted = 0; dropped = 0; encoded = 0
            }
        } catch { forceKey = true; onFailure?("Bitstream conversion: \(error)") }
    }
    private func annexB(_ sample: CMSampleBuffer, key: Bool) throws -> Data {
        guard let format = CMSampleBufferGetFormatDescription(sample), let block = CMSampleBufferGetDataBuffer(sample) else { throw HostError("Missing encoded data") }
        var result = Data(), headerLength: Int32 = 0
        var pointer: UnsafePointer<UInt8>?, size = 0, count = 0
        func parameter(_ index: Int) -> OSStatus {
            if codec != .h264 {
                return CMVideoFormatDescriptionGetHEVCParameterSetAtIndex(format, parameterSetIndex: index, parameterSetPointerOut: &pointer,
                    parameterSetSizeOut: &size, parameterSetCountOut: &count, nalUnitHeaderLengthOut: &headerLength)
            }
            return CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, parameterSetIndex: index, parameterSetPointerOut: &pointer,
                parameterSetSizeOut: &size, parameterSetCountOut: &count, nalUnitHeaderLengthOut: &headerLength)
        }
        guard parameter(0) == noErr else { throw HostError("Missing codec parameter sets") }
        if key {
            for index in 0..<count {
                guard parameter(index) == noErr, let pointer else { throw HostError("Invalid parameter set") }
                result.append(contentsOf: [0,0,0,1]); result.append(pointer, count: size)
            }
        }
        guard (1...4).contains(headerLength) else { throw HostError("Invalid NAL length size") }
        let dataLength = CMBlockBufferGetDataLength(block)
        guard dataLength > 0 else { throw HostError("Empty encoded buffer") }
        var bytes = Data(count: dataLength)
        let copied = bytes.withUnsafeMutableBytes { CMBlockBufferCopyDataBytes(block, atOffset: 0, dataLength: dataLength, destination: $0.baseAddress!) }
        guard copied == noErr else { throw HostError("Copy encoded data: \(copied)") }
        var r = Reader(bytes)
        while !r.atEnd {
            var length = 0
            for _ in 0..<Int(headerLength) { length = (length << 8) | Int(try r.get(UInt8.self)) }
            guard length > 0 else { throw WireError.malformed }
            let nal = try r.bytes(length); result.append(contentsOf: [0,0,0,1]); result.append(nal)
        }
        return result
    }
    deinit { if let compression { VTCompressionSessionInvalidate(compression) } }
}
