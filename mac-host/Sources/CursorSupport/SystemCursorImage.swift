import AppKit

public struct SystemCursorImage {
    public let representations: [CGImage]
    public var pixels: CGImage { representations.last! }
    public let logicalSize: CGSize
    public let hotspot: CGPoint

    public static func read(_ cursor: NSCursor) -> Self? {
        let size = cursor.image.size, hotspot = cursor.hotSpot
        guard size.width.isFinite, size.height.isFinite, hotspot.x.isFinite, hotspot.y.isFinite,
              size.width >= 1, size.height >= 1, size.width <= 256, size.height <= 256,
              hotspot.x >= 0, hotspot.y >= 0, hotspot.x < size.width, hotspot.y < size.height else { return nil }
        // Preserve an actual high-density system representation. Logical size
        // and click location stay in points, independently of the PNG pixels.
        let candidates = cursor.image.representations.compactMap { rep -> CGImage? in
            guard let bitmap = rep as? NSBitmapImageRep, let image = bitmap.cgImage,
                  image.width > 0, image.height > 0, image.width <= 1024, image.height <= 1024,
                  abs(Double(image.width) / Double(image.height) - size.width / size.height) < 0.02 else { return nil }
            return image
        }
        let ordered = candidates.sorted { $0.width * $0.height < $1.width * $1.height }
        if !ordered.isEmpty {
            let selected = ordered.count <= 8 ? ordered : Array(ordered.prefix(7)) + [ordered.last!]
            return Self(representations: selected, logicalSize: size, hotspot: hotspot)
        }
        // Vector-only system images are drawn by AppKit from their own asset,
        // never from a replacement outline or an enlarged low-density bitmap.
        guard cursor.image.representations.contains(where: { $0 is NSPDFImageRep || $0 is NSEPSImageRep }) else { return nil }
        let density = min(4.0, 1024.0 / max(size.width, size.height))
        guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(ceil(size.width * density)),
              pixelsHigh: Int(ceil(size.height * density)), bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
              isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 32),
              let context = NSGraphicsContext(bitmapImageRep: bitmap) else { return nil }
        bitmap.size = size
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = context
        context.cgContext.scaleBy(x: density, y: density)
        cursor.image.draw(in: CGRect(origin: .zero, size: size), from: .zero, operation: .copy, fraction: 1)
        NSGraphicsContext.restoreGraphicsState()
        guard let pixels = bitmap.cgImage else { return nil }
        return Self(representations: [pixels], logicalSize: size, hotspot: hotspot)
    }
}
