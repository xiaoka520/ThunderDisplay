The arrow image was read from the installed macOS system with `NSCursor.arrow.image`
and its native `hotSpot`. It retains the system outline, rounded corners and shadow.
See `mac-arrow.json` for the source OS and image dimensions. This Apple system
image is not an original ThunderDisplay illustration.

To regenerate on a Mac, initialize AppKit and export through the public API:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer swift scripts/export-mac-cursor.swift build/mac-cursor
python3 scripts/embed-mac-cursor.py build/mac-cursor
```

The embedding script removes transparent margins without changing visible pixels
or the click location. Windows scales the premultiplied source with its monitor DPI.

0.7.0 retains this system-exported arrow only as the brief initial fallback.
The connected Mac now supplies the actual WindowServer cursor pixels, logical
size and hotspot over CursorImage, including application-specific cursor shapes.
No screenshot/resize/text cursor is hand drawn. Runtime getter availability is
negotiated; without it ScreenCaptureKit keeps the cursor in the video.

0.7.6 prefers `NSCursor.currentSystem` and retains the largest actual native
bitmap representation (bounded to 1024 pixels per side), with logical size and
hotspot still in points. On the development Mac, the live text cursor has native
1x/2x/5x/10x representations; using only the WindowServer 1x getter blurred it
when Windows enlarged it. The deprecated getter remains optional: unsupported
or custom shapes fall back to the actual WindowServer pixels, never a guessed
arrow. Vector-only assets are rendered by AppKit from the system image.
Windows uses exact pixel-coverage reduction once, preserves pixels at 1:1, and
applies premultiplied-alpha interpolation only when an asset must be enlarged.

0.7.7 transmits the native representation family to query7 clients. Windows
selects the smallest native bitmap covering its current monitor DPI; exact
1x/2x matches bypass resampling. Fractional DPI uses one coverage reduction.
Moving to another monitor selects another cached representation automatically.
Older peers continue receiving the single-image version1 payload.
