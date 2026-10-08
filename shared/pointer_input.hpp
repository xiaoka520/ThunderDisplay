#pragma once

namespace td {
enum class PointerInputMode { Released, Absolute, Relative };
inline PointerInputMode pointerInputMode(bool multiDisplay, bool fullscreen, bool capture, bool foreground, bool live) {
    if(!capture || !foreground || !live) return PointerInputMode::Released;
    return multiDisplay && fullscreen ? PointerInputMode::Relative : PointerInputMode::Absolute;
}
}
