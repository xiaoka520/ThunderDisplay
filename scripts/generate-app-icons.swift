// Convert the shared artwork to native app icons without changing its design.
// Run on macOS: swift scripts/generate-app-icons.swift
import Foundation
import CoreGraphics
import ImageIO

func require(_ condition: Bool, _ message: String) throws {
    if !condition { throw NSError(domain: "ThunderDisplay.Icons", code: 1,
                                   userInfo: [NSLocalizedDescriptionKey: message]) }
}
func put16(_ value: UInt16, into data: inout Data) {
    var encoded = value.littleEndian
    withUnsafeBytes(of: &encoded) { data.append(contentsOf: $0) }
}
func put32(_ value: UInt32, into data: inout Data) {
    var encoded = value.littleEndian
    withUnsafeBytes(of: &encoded) { data.append(contentsOf: $0) }
}

let root = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent()
guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB) else {
    throw NSError(domain: "ThunderDisplay.Icons", code: 1,
                  userInfo: [NSLocalizedDescriptionKey: "Cannot create icon color space"])
}

func raster(_ size: Int, _ image: CGImage) throws -> (png: Data, pixels: Data) {
    guard let context = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
                                  bytesPerRow: size * 4, space: colorSpace,
                                  bitmapInfo: CGBitmapInfo.byteOrder32Big.rawValue |
                                      CGImageAlphaInfo.premultipliedLast.rawValue) else {
        throw NSError(domain: "ThunderDisplay.Icons", code: 2)
    }
    let scale = Double(size) / Double(max(image.width, image.height))
    let width = Double(image.width) * scale, height = Double(image.height) * scale
    context.interpolationQuality = .high
    context.draw(image, in: CGRect(x: (Double(size) - width) / 2,
                                  y: (Double(size) - height) / 2, width: width, height: height))
    guard let output = context.makeImage(), let pixels = context.data else {
        throw NSError(domain: "ThunderDisplay.Icons", code: 3)
    }
    let encoded = NSMutableData()
    guard let destination = CGImageDestinationCreateWithData(encoded, "public.png" as CFString, 1, nil) else {
        throw NSError(domain: "ThunderDisplay.Icons", code: 4)
    }
    CGImageDestinationAddImage(destination, output, nil)
    try require(CGImageDestinationFinalize(destination), "Cannot encode icon PNG")
    return (encoded as Data, Data(bytes: pixels, count: size * size * 4))
}

// Small Windows icons use conventional BGRA DIBs and an AND mask. The 256 px
// entry uses PNG. Unpremultiply RGB so transparent edges do not get a dark halo.
func windowsBitmap(_ size: Int, _ pixels: Data) -> Data {
    let maskStride = ((size + 31) / 32) * 4
    var result = Data()
    for value in [UInt32(40), UInt32(size), UInt32(size * 2)] { put32(value, into: &result) }
    put16(1, into: &result); put16(32, into: &result)
    for value in [UInt32(0), UInt32(size * size * 4 + maskStride * size), 0, 0, 0, 0] {
        put32(value, into: &result)
    }
    var mask = Data(repeating: 0, count: maskStride * size)
    for row in 0..<size {
        for column in 0..<size {
            let index = ((size - row - 1) * size + column) * 4
            let alpha = Int(pixels[index + 3])
            for channel in [2, 1, 0] {
                result.append(alpha == 0 ? 0 : UInt8(min(255, (Int(pixels[index + channel]) * 255 + alpha / 2) / alpha)))
            }
            result.append(UInt8(alpha))
            if alpha == 0 { mask[row * maskStride + column / 8] |= UInt8(0x80 >> (column % 8)) }
        }
    }
    result.append(mask)
    return result
}

func writeWindowsIcon(_ image: CGImage, _ filename: String, silhouette: Bool = false) throws {
    let sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    let entries = try sizes.map { size -> Data in
        let rendered = try raster(size, image)
        if silhouette {
            var pixels = rendered.pixels
            for index in stride(from: 0, to: pixels.count, by: 4) {
                pixels[index] = 0; pixels[index + 1] = 0; pixels[index + 2] = 0
            }
            return windowsBitmap(size, pixels)
        }
        return size == 256 ? rendered.png : windowsBitmap(size, rendered.pixels)
    }
    var ico = Data()
    put16(0, into: &ico); put16(1, into: &ico); put16(UInt16(sizes.count), into: &ico)
    var offset = 6 + sizes.count * 16
    for (size, entry) in zip(sizes, entries) {
        ico.append(contentsOf: [size == 256 ? 0 : UInt8(size), size == 256 ? 0 : UInt8(size), 0, 0])
        put16(1, into: &ico); put16(32, into: &ico)
        put32(UInt32(entry.count), into: &ico); put32(UInt32(offset), into: &ico)
        offset += entry.count
    }
    for entry in entries { ico.append(entry) }
    try ico.write(to: root.appendingPathComponent("windows-client/assets/\(filename).ico"), options: .atomic)
}

let files = FileManager.default
for (variant, filename) in [("ThunderDisplay", "Icon-iOS-Default-1024@1x.png"),
                            ("ThunderDisplayDark", "Icon-iOS-Dark-1024@1x.png")] {
let source = root.appendingPathComponent("Icon/" + filename)
guard let decoder = CGImageSourceCreateWithURL(source as CFURL, nil),
      let image = CGImageSourceCreateImageAtIndex(decoder, 0, nil) else {
    throw NSError(domain: "ThunderDisplay.Icons", code: 1,
                  userInfo: [NSLocalizedDescriptionKey: "Cannot load " + source.path])
}
let iconset = root.appendingPathComponent("build/app-icons/\(variant).iconset")
try files.createDirectory(at: iconset, withIntermediateDirectories: true)
for logicalSize in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let name = "icon_\(logicalSize)x\(logicalSize)\(scale == 2 ? "@2x" : "").png"
        try raster(logicalSize * scale, image).png.write(to: iconset.appendingPathComponent(name), options: .atomic)
    }
}
let macIcon = root.appendingPathComponent("mac-host/Resources/\(variant).icns")
try files.createDirectory(at: macIcon.deletingLastPathComponent(), withIntermediateDirectories: true)
let converter = Process()
converter.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
converter.arguments = ["-c", "icns", iconset.path, "-o", macIcon.path]
try converter.run(); converter.waitUntilExit()
try require(converter.terminationStatus == 0, "iconutil failed")

try writeWindowsIcon(image, variant)
print("Generated Mac ICNS and Windows ICO from Icon/\(filename) (alpha preserved)")
}

// Status icons use the supplied monochrome artwork, independently of app icons.
// macOS renders its template in the menu bar's native color; Windows selects
// white or black while keeping the same shape and alpha at each taskbar DPI.
let statusSource = root.appendingPathComponent("Icon/Icon.png")
guard let decoder = CGImageSourceCreateWithURL(statusSource as CFURL, nil),
      let statusImage = CGImageSourceCreateImageAtIndex(decoder, 0, nil) else {
    throw NSError(domain: "ThunderDisplay.Icons", code: 1,
                  userInfo: [NSLocalizedDescriptionKey: "Cannot load " + statusSource.path])
}
for scale in [1, 2, 3] {
    let suffix = scale == 1 ? "" : "@\(scale)x"
    try raster(22 * scale, statusImage).png.write(
        to: root.appendingPathComponent("mac-host/Resources/ThunderDisplayStatus\(suffix).png"), options: .atomic)
}
try writeWindowsIcon(statusImage, "ThunderDisplayStatus")
try writeWindowsIcon(statusImage, "ThunderDisplayStatusLight", silhouette: true)
print("Generated native status icons from Icon/Icon.png (shape and alpha preserved)")
