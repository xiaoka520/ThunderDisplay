import AppKit
import ImageIO
import Wire

// AppKit pasteboard access is confined to the main thread. Never export existing content on connect.
final class ClipboardBridge {
    private let pasteboard: NSPasteboard
    init(pasteboard: NSPasteboard = .general) { self.pasteboard = pasteboard }
    private var session: UInt64 = 0, changeCount = 0
    private var timer: Timer?
    var onText: ((UInt64, String) -> Void)?
    var onImage: ((UInt64, Data) -> Void)?
    func stop() { timer?.invalidate(); timer = nil; session = 0; onText = nil; onImage = nil }
    func setSession(_ session: UInt64, enabled: Bool) {
        if !enabled && self.session != session { return }
        timer?.invalidate(); timer = nil
        self.session = enabled ? session : 0
        changeCount = pasteboard.changeCount
        if enabled {
            timer = Timer.scheduledTimer(withTimeInterval: 0.2, repeats: true) { [weak self] _ in self?.poll() }
        }
    }
    private func poll() {
        guard session != 0 else { return }
        let board = pasteboard, count = board.changeCount
        guard count != changeCount else { return }
        changeCount = count
        if board.types?.contains(.png) == true, let png = board.data(forType: .png), Self.validPNG(png) { onImage?(session, png); return }
        if board.types?.contains(.tiff) == true, let data = board.data(forType: .tiff), data.count <= BinaryWire.imageLimit,
            Self.validImageDimensions(data), let bitmap = NSBitmapImageRep(data: data), bitmap.pixelsWide > 0, bitmap.pixelsHigh > 0,
            bitmap.pixelsWide * bitmap.pixelsHigh <= 16 * 1024 * 1024,
            let png = bitmap.representation(using: .png, properties: [:]), Self.validPNG(png) { onImage?(session, png); return }
        guard let text = board.string(forType: .string), text.utf8.count <= TextClipboardWire.limit, !text.contains("\0") else { return }
        onText?(session, text)
    }
    private static func validImageDimensions(_ data: Data) -> Bool {
        guard let source = CGImageSourceCreateWithData(data as CFData, [kCGImageSourceShouldCache: false] as CFDictionary),
            let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
            let width = properties[kCGImagePropertyPixelWidth] as? Int, let height = properties[kCGImagePropertyPixelHeight] as? Int else { return false }
        return width > 0 && height > 0 && width <= 8192 && height <= 8192 && width * height <= 16 * 1024 * 1024
    }
    static func validPNG(_ png: Data) -> Bool {
        guard png.count >= 24, png.count <= BinaryWire.imageLimit,
            png.prefix(8) == Data([137,80,78,71,13,10,26,10]), png[12..<16] == Data("IHDR".utf8) else { return false }
        var reader = Reader(Data(png[16..<24]))
        guard let width = try? reader.get(UInt32.self), let height = try? reader.get(UInt32.self) else { return false }
        return width > 0 && height > 0 && width <= 8192 && height <= 8192 && UInt64(width) * UInt64(height) <= 16 * 1024 * 1024
    }
    func receiveImage(_ png: Data, session: UInt64) {
        guard self.session == session, session != 0, Self.validPNG(png), let bitmap = NSBitmapImageRep(data: png) else { return }
        pasteboard.clearContents(); pasteboard.setData(png, forType: .png)
        if let tiff = bitmap.tiffRepresentation { pasteboard.setData(tiff, forType: .tiff) }
        changeCount = pasteboard.changeCount
    }
    func receive(_ text: String, session: UInt64) {
        guard self.session == session, session != 0 else { return }
        let board = pasteboard
        board.clearContents(); board.setString(text, forType: .string)
        // Suppress the resulting local change, so a received copy is never echoed back.
        changeCount = board.changeCount
    }
    deinit { timer?.invalidate() }
}
