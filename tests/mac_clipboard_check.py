#!/usr/bin/env python3
"""Native AppKit bridge check using a private pasteboard, never the user's clipboard."""
from pathlib import Path
import os
import platform
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
env = os.environ.copy()
env.setdefault('DEVELOPER_DIR', '/Applications/Xcode.app/Contents/Developer')
with tempfile.TemporaryDirectory(prefix='td-clipboard-check-') as directory:
    folder = Path(directory)
    compiler = ['xcrun', 'swiftc', '-target', f'{platform.machine()}-apple-macosx13.0',
        '-module-cache-path', str(folder / 'module-cache')]
    subprocess.run(compiler + ['-emit-library', '-emit-module', '-module-name', 'Wire',
        '-emit-module-path', str(folder / 'Wire.swiftmodule'),
        str(ROOT / 'mac-host/Sources/Wire/Wire.swift'), str(ROOT / 'mac-host/Sources/Wire/Clipboard.swift'), str(ROOT / 'mac-host/Sources/Wire/Blob.swift'),
        '-o', str(folder / 'libWire.dylib')], env=env, check=True)
    (folder / 'main.swift').write_text('''
import AppKit
let board = NSPasteboard(name: NSPasteboard.Name("ThunderDisplay-Test-" + UUID().uuidString))
defer { board.releaseGlobally() }
func spin() { RunLoop.current.run(until: Date().addingTimeInterval(0.25)) }
board.clearContents(); board.setString("existing private content", forType: .string)
let bridge = ClipboardBridge(pasteboard: board)
var received: [(UInt64, String)] = []
var images: [(UInt64, Data)] = []
bridge.onImage = { images.append(($0, $1)) }
bridge.onText = { received.append(($0, $1)) }
bridge.setSession(7, enabled: true); spin()
precondition(received.isEmpty, "Connect must not export preexisting text")
board.clearContents(); board.setString("Mac 复制文本🐱", forType: .string); spin()
precondition(received.count == 1 && received[0].0 == 7 && received[0].1 == "Mac 复制文本🐱")
bridge.receive("Windows 文字", session: 7); spin()
precondition(board.string(forType: .string) == "Windows 文字" && received.count == 1, "Remote text must not echo")
bridge.receive("stale", session: 6)
precondition(board.string(forType: .string) == "Windows 文字", "A stale session must not replace clipboard content")
bridge.setSession(7, enabled: false)
board.clearContents(); board.setString("disabled", forType: .string); spin()
precondition(received.count == 1, "Disabled bridge must not transmit changes")
bridge.setSession(8, enabled: true)
bridge.setSession(7, enabled: false) // A late disconnect must not disable the newer session.
board.clearContents(); board.setString("new session", forType: .string); spin()
precondition(received.count == 2 && received[1].0 == 8)
let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: 12, pixelsHigh: 8, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 48, bitsPerPixel: 32)!
bitmap.bitmapData!.initialize(repeating: 220, count: 384)
let png = bitmap.representation(using: .png, properties: [:])!
precondition(ClipboardBridge.validPNG(png))
board.clearContents(); board.setData(png, forType: .png); spin()
precondition(images.count == 1 && images[0].0 == 8 && images[0].1 == png)
board.clearContents(); board.setData(bitmap.tiffRepresentation!, forType: .tiff); spin()
precondition(images.count == 2 && ClipboardBridge.validPNG(images[1].1))
bridge.receiveImage(png, session: 8); spin()
precondition(board.data(forType: .png) == png && board.data(forType: .tiff) != nil && images.count == 2, "Remote image must not echo")
bridge.receiveImage(Data([1,2,3]), session: 8)
bridge.receiveImage(png, session: 7)
precondition(board.data(forType: .png) == png, "Reject corrupt and stale-session images")
bridge.setSession(8, enabled: false)
board.clearContents(); board.setData(png, forType: .png); spin()
precondition(images.count == 2)
bridge.stop(); board.clearContents(); board.setString("stopped", forType: .string); spin()
precondition(received.count == 2)
print("Native private-pasteboard clipboard lifecycle, Unicode, PNG/TIFF image formats, echo suppression and stale-session checks passed")
''')
    subprocess.run(compiler + ['-I', directory, '-L', directory, '-lWire', '-Xlinker', '-rpath', '-Xlinker', directory,
        str(folder / 'main.swift'), str(ROOT / 'mac-host/Sources/ThunderDisplayHost/ClipboardBridge.swift'),
        '-framework', 'AppKit', '-o', str(folder / 'clipboard-check')], env=env, check=True)
    subprocess.run([str(folder / 'clipboard-check')], env=env, check=True)
