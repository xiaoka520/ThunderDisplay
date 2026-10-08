import CoreGraphics

/// The remote cursor uses exactly the inverse of absolute input mapping.
/// A pointer on another screen is hidden instead of clamped to a video edge.
public enum CapturedPointer {
    public static func normalized(_ point: CGPoint, bounds: CGRect, captureSize: CGSize, contentRect: CGRect) -> (UInt16, UInt16)? {
        guard point.x.isFinite, point.y.isFinite, bounds.width > 1, bounds.height > 1,
              captureSize.width > 1, captureSize.height > 1, contentRect.width > 1, contentRect.height > 1,
              bounds.contains(point) else { return nil }
        let x = contentRect.minX + (point.x - bounds.minX) / (bounds.width - 1) * (contentRect.width - 1)
        let y = contentRect.minY + (point.y - bounds.minY) / (bounds.height - 1) * (contentRect.height - 1)
        return (UInt16(min(65535, max(0, (x / (captureSize.width - 1) * 65535).rounded()))),
                UInt16(min(65535, max(0, (y / (captureSize.height - 1) * 65535).rounded()))))
    }
}
