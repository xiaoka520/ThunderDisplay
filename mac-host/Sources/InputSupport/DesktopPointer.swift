import CoreGraphics

/// Quartz desktop coordinates are logical points, including negative origins.
/// Keep the individual screens: their bounding rectangle includes empty space.
public struct DesktopPointer {
    public let screens: [CGRect]
    public init(screens: [CGRect]) {
        self.screens = screens.filter { !$0.isNull && !$0.isInfinite && $0.width >= 1 && $0.height >= 1 }
    }
    public static func activeDisplayBounds() -> [CGRect] {
        var count: UInt32 = 0
        guard CGGetActiveDisplayList(0, nil, &count) == .success, count > 0 else { return [] }
        var ids = [CGDirectDisplayID](repeating: 0, count: Int(count))
        guard CGGetActiveDisplayList(count, &ids, &count) == .success else { return [] }
        var bounds: [CGRect] = []
        for id in ids.prefix(Int(count)) {
            let rect = CGDisplayBounds(id)
            if rect.width >= 1, rect.height >= 1, !bounds.contains(rect) { bounds.append(rect) }
        }
        return bounds.sorted {
            if $0.minY != $1.minY { return $0.minY < $1.minY }
            if $0.minX != $1.minX { return $0.minX < $1.minX }
            if $0.width != $1.width { return $0.width < $1.width }
            return $0.height < $1.height
        }
    }
    public func nearest(_ point: CGPoint) -> CGPoint {
        screens.map { rect in
            CGPoint(x: min(rect.maxX - 1, max(rect.minX, point.x)),
                    y: min(rect.maxY - 1, max(rect.minY, point.y)))
        }.min { distance($0, point) < distance($1, point) } ?? point
    }
    public func move(from point: CGPoint, by delta: CGPoint) -> CGPoint {
        let start = nearest(point), target = CGPoint(x: start.x + delta.x, y: start.y + delta.y)
        let reached = travel(from: start, to: target)
        if reached == target { return nearest(reached) }
        // Slide along an outer edge, without crossing a gap between screens.
        let horizontal = travel(from: reached, to: CGPoint(x: target.x, y: reached.y))
        let xy = travel(from: horizontal, to: CGPoint(x: horizontal.x, y: target.y))
        let vertical = travel(from: reached, to: CGPoint(x: reached.x, y: target.y))
        let yx = travel(from: vertical, to: CGPoint(x: target.x, y: vertical.y))
        return nearest(distance(xy, target) <= distance(yx, target) ? xy : yx)
    }
    private func distance(_ a: CGPoint, _ b: CGPoint) -> CGFloat {
        (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)
    }
    private func travel(from start: CGPoint, to target: CGPoint) -> CGPoint {
        let dx = target.x - start.x, dy = target.y - start.y
        // Intersect the movement segment with each display, then walk only the
        // connected intervals. A large delta cannot jump over an empty region.
        var intervals: [(CGFloat, CGFloat)] = []
        for rect in screens {
            var enter: CGFloat = 0, leave: CGFloat = 1, intersects = true
            for (position, delta, low, high) in [(start.x, dx, rect.minX, rect.maxX), (start.y, dy, rect.minY, rect.maxY)] {
                if delta == 0 {
                    if position < low || position >= high { intersects = false; break }
                } else {
                    let a = (low - position) / delta, b = (high - position) / delta
                    enter = max(enter, min(a, b)); leave = min(leave, max(a, b))
                    if enter > leave { intersects = false; break }
                }
            }
            if intersects { intervals.append((enter, leave)) }
        }
        var reach: CGFloat = 0
        for (enter, leave) in intervals.sorted(by: { $0.0 < $1.0 }) {
            if enter > reach + 1e-9 { break }
            reach = max(reach, leave)
        }
        if reach >= 1 { return target }
        return nearest(CGPoint(x: start.x + dx * reach, y: start.y + dy * reach))
    }
}
