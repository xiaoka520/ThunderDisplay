// Read the installed macOS arrow through AppKit; do not redraw its geometry.
// Usage: swift scripts/export-mac-cursor.swift <output-directory>
// Follow with scripts/embed-mac-cursor.py to generate the Windows source asset.
import AppKit
import Foundation

let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
let application = NSApplication.shared
let cursor = NSCursor.arrow
let image = cursor.image
print("System arrow: \(image.size), hotspot \(cursor.hotSpot)")
fflush(stdout)
guard image.size.width > 0, image.size.height > 0 else {
    fputs("The system cursor image is unavailable in this process.\n", stderr)
    exit(1)
}
let density = 4
let width = Int(ceil(image.size.width)) * density
let height = Int(ceil(image.size.height)) * density
guard let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height,
    bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
    colorSpaceName: .deviceRGB, bitmapFormat: [],
    bytesPerRow: width * 4, bitsPerPixel: 32), let context = NSGraphicsContext(bitmapImageRep: bitmap) else {
    fatalError("Cannot rasterize the macOS cursor")
}
bitmap.size = image.size
NSGraphicsContext.saveGraphicsState()
NSGraphicsContext.current = context
context.imageInterpolation = .high
context.cgContext.scaleBy(x: CGFloat(density), y: CGFloat(density))
image.draw(in: NSRect(origin: .zero, size: image.size), from: .zero, operation: .copy, fraction: 1)
NSGraphicsContext.restoreGraphicsState()
guard let pixels = bitmap.bitmapData, let png = bitmap.representation(using: .png, properties: [:]) else {
    fatalError("No pixels returned by AppKit")
}
var bgra = Data(bytes: pixels, count: width * height * 4)
for offset in stride(from: 0, to: bgra.count, by: 4) { bgra.swapAt(offset, offset + 2) }
try bgra.write(to: output.appendingPathComponent("mac-arrow.bgra"))
try png.write(to: output.appendingPathComponent("mac-arrow.png"))
let metadata: [String: Any] = ["source": "NSCursor.arrow.image", "system": ProcessInfo.processInfo.operatingSystemVersionString,
    "width": width, "height": height, "density": density,
    "logicalWidth": image.size.width, "logicalHeight": image.size.height,
    "hotX": cursor.hotSpot.x * Double(density), "hotY": cursor.hotSpot.y * Double(density)]
try JSONSerialization.data(withJSONObject: metadata, options: [.prettyPrinted, .sortedKeys])
    .write(to: output.appendingPathComponent("mac-arrow.json"))
print(String(data: try JSONSerialization.data(withJSONObject: metadata, options: [.sortedKeys]), encoding: .utf8)!)
for representation in image.representations { print("Representation: \(type(of: representation)), \(representation.pixelsWide)×\(representation.pixelsHigh)") }
