import AppKit
import Darwin
import Wire
import CursorSupport

// WindowServer's current cursor pixels come from macOS itself (including custom
// app and screenshot cursors). These optional getters are resolved at runtime;
// a system without them keeps the cursor in ScreenCaptureKit's video instead.
private final class WindowServerCursorReader {
    typealias Connection = @convention(c) () -> Int32
    typealias Size = @convention(c) (Int32, UnsafeMutablePointer<Int32>) -> Int32
    typealias Pixels = @convention(c) (Int32, UnsafeMutableRawPointer, UnsafeMutablePointer<Int32>, UnsafeMutablePointer<Int32>, UnsafeMutablePointer<CGRect>, UnsafeMutablePointer<CGPoint>, UnsafeMutablePointer<Int32>, UnsafeMutablePointer<Int32>, UnsafeMutablePointer<Int32>) -> Int32
    typealias Seed = @convention(c) () -> Int32
    private let connection: Int32, size: Size, pixels: Pixels
    private var storage = [UInt8](repeating: 0, count: 1024 * 1024)
    private let visibility: Seed?
    // Keep the framework handle open for the lifetime of all function pointers.
    private let framework: UnsafeMutableRawPointer
    init?() {
        guard let framework = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY | RTLD_LOCAL) else { return nil }
        guard let c = dlsym(framework, "_CGSDefaultConnection"), let s = dlsym(framework, "CGSGetGlobalCursorDataSize"),
            let p = dlsym(framework, "CGSGetGlobalCursorData") else { dlclose(framework); return nil }
        visibility = dlsym(nil, "CGCursorIsVisible").map { unsafeBitCast($0, to: Seed.self) }
        self.framework = framework; connection = unsafeBitCast(c, to: Connection.self)()
        size = unsafeBitCast(s, to: Size.self); pixels = unsafeBitCast(p, to: Pixels.self)
    }
    deinit { dlclose(framework) }
    func isVisible() -> Bool { visibility.map { $0() != 0 } ?? true }
    func read() -> (CGImage, CGSize, CGPoint)? {
        var count: Int32 = 0
        let limit = 1024 * 1024
        guard size(connection, &count) == 0, count > 0, count <= limit else { return nil }
        // The cursor can change between the two calls. Reserve the full bounded
        // capacity rather than an allocation based on the previous cursor size.
        count = Int32(limit)
        var row: Int32 = 0, rect = CGRect.zero, hot = CGPoint.zero, depth: Int32 = 0, channels: Int32 = 0, bits: Int32 = 0
        let result = storage.withUnsafeMutableBytes { pixels(connection, $0.baseAddress!, &count, &row, &rect, &hot, &depth, &channels, &bits) }
        let dimensions = rect.size
        guard result == 0, depth == 32, channels == 4, bits == 8,
            dimensions.width.isFinite, dimensions.height.isFinite, hot.x.isFinite, hot.y.isFinite,
            dimensions.width >= 1, dimensions.height >= 1, dimensions.width <= 256, dimensions.height <= 256,
            hot.x >= 0, hot.y >= 0, hot.x < dimensions.width, hot.y < dimensions.height,
            dimensions.width.rounded() == dimensions.width, dimensions.height.rounded() == dimensions.height else { return nil }
        let width = Int(dimensions.width), height = Int(dimensions.height)
        guard row >= width * 4, Int(row) * height <= Int(count), count <= limit,
            let provider = CGDataProvider(data: Data(storage.prefix(Int(row) * height)) as CFData),
            let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: Int(row),
                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGBitmapInfo.byteOrder32Little.union(CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedFirst.rawValue)),
                provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent) else { return nil }
        return (image, dimensions, hot)
    }
}

private final class SystemCursorReader {
    private let fallback = WindowServerCursorReader()
    func isVisible() -> Bool { fallback?.isVisible() ?? true }
    func read() -> ([CGImage], CGSize, CGPoint)? {
        // This deprecated getter still supplies the live system cursor on
        // supported macOS versions, including its 2x/5x/10x representations.
        // NSCursor.current would only describe this app and must not be used.
        if let cursor = NSCursor.currentSystem, let image = SystemCursorImage.read(cursor) {
            return (image.representations, image.logicalSize, image.hotspot)
        }
        // Retain the actual WindowServer shape for unsupported/custom cursors;
        // never substitute a generic arrow for e.g. a screenshot cursor.
        guard let (image, size, hotspot) = fallback?.read() else { return nil }
        return ([image], size, hotspot)
    }
}

final class NativeCursorMonitor {
    private let reader = SystemCursorReader()
    private var timer: Timer?, session: UInt64 = 0, previous: Data?, variants = false
    private var previousPosition: CGPoint?, previousVisibility: Bool?
    var available: Bool { reader.read() != nil }
    func diagnostic() -> String {
        guard let (images, size, hotspot) = reader.read() else { return "Native cursor unavailable; use video cursor" }
        return "Native cursor representations: \(images.map { "\($0.width)×\($0.height)" }.joined(separator: ", ")); logical points: \(size.width)×\(size.height); hotspot: \(hotspot.x),\(hotspot.y); visible: \(reader.isVisible())"
    }
    var onImage: ((UInt64, Data) -> Void)?
    var onPosition: ((UInt64, CGPoint, Bool) -> Void)?
    func stop() { timer?.invalidate(); timer = nil; session = 0; previous = nil; previousPosition = nil; previousVisibility = nil }
    func setSession(_ session: UInt64, enabled: Bool, variants: Bool = false) {
        if !enabled && self.session != session { return }
        stop(); guard enabled else { return }; self.session = session; self.variants = variants
        poll()
        let timer = Timer(timeInterval: 1.0 / 60, repeats: true) { [weak self] _ in self?.poll() }
        RunLoop.main.add(timer, forMode: .common); self.timer = timer
    }
    private func poll() {
        guard session != 0 else { return }
        let showing = reader.isVisible()
        if let point = CGEvent(source: nil)?.location,
           point != previousPosition || showing != previousVisibility {
            previousPosition = point; previousVisibility = showing
            onPosition?(session, point, showing)
        }
        guard let (images, size, hotspot) = reader.read() else { return }
        // A cursor animation can update pixels without changing the shape seed.
        // Compare native pixels before PNG encoding instead of skipping by seed.
        var signature = Writer(); signature.put(UInt8(showing ? 1 : 0))
        signature.put(UInt32((size.width * 65536).rounded())); signature.put(UInt32((size.height * 65536).rounded()))
        signature.put(Int32((hotspot.x * 65536).rounded())); signature.put(Int32((hotspot.y * 65536).rounded()))
        if showing { for image in images {
            signature.put(UInt32(image.width)); signature.put(UInt32(image.height))
            if let bytes = image.dataProvider?.data { signature.bytes(bytes as Data) }
        } }
        guard signature.data != previous else { return }
        let bitmaps: [NSBitmapImageRep]
        if showing { bitmaps = images.map { NSBitmapImageRep(cgImage: $0) } }
        else {
            guard let blank = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 1, pixelsHigh: 1, bitsPerSample: 8,
                samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 4, bitsPerPixel: 32) else { return }
            blank.bitmapData?.initialize(repeating: 0, count: 4); bitmaps = [blank]
        }
        var pngs = [Data](), total = 18
        for bitmap in bitmaps {
            guard let png = bitmap.representation(using: .png, properties: [:]), total + 4 + png.count <= BinaryWire.cursorLimit else { continue }
            pngs.append(png); total += 4 + png.count
        }
        let dimensions = showing ? size : CGSize(width: 1, height: 1), point = showing ? hotspot : .zero
        guard let payload = try? CursorPayload.encode(width: UInt32((dimensions.width * 65536).rounded()),
            height: UInt32((dimensions.height * 65536).rounded()), hotX: Int32((point.x * 65536).rounded()),
            hotY: Int32((point.y * 65536).rounded()), images: pngs, variants: variants) else { return }
        previous = signature.data; onImage?(session, payload)
    }
    deinit { timer?.invalidate() }
}
