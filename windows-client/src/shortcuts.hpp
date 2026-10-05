#pragma once
#include <cstdint>
namespace td {
// Matches Win32 HOTKEYF_SHIFT / CONTROL / ALT, without depending on Windows headers.
constexpr uint8_t HotShift=1,HotControl=2,HotAlt=4;
constexpr uint16_t DefaultFullscreenHotkey=0x060D; // Ctrl + Alt + Enter
constexpr uint16_t keyboardKey(uint16_t vk,uint32_t scanCode,bool extended) {
    if(vk==0x10) return scanCode==0x36?0xA1:0xA0;
    if(vk==0x11) return extended?0xA3:0xA2;
    if(vk==0x12) return extended?0xA5:0xA4;
    if(vk==0x0D && extended) return 0x10D;
    return vk;
}
constexpr bool validFullscreenHotkey(uint16_t shortcut) {
    auto key=uint8_t(shortcut),rawMods=uint8_t(shortcut>>8),mods=uint8_t(rawMods&7);
    if(!key || !(mods&(HotControl|HotAlt)) || (rawMods&~15) || key==0x7A) return false; // F11 is reserved out of this UI.
    if(key==0x10 || key==0x11 || key==0x12 || (key>=0xA0 && key<=0xA5) || key==0x5B || key==0x5C) return false;
    if(key==0x1B && (mods==3 || mods==7)) return false; // Task Manager / release input
    if(key==0x2E && (mods&6)==6) return false; // Secure attention sequence
    return true;
}
constexpr bool matchesFullscreenHotkey(uint16_t shortcut,uint16_t key,uint16_t inputFlags) {
    uint8_t mods=((inputFlags&2)?HotShift:0)|((inputFlags&4)?HotControl:0)|((inputFlags&8)?HotAlt:0);
    return validFullscreenHotkey(shortcut) && !(inputFlags&16) && uint8_t(shortcut)==(key==0x10D?0x0D:key) && (uint8_t(shortcut>>8)&7)==mods;
}
constexpr bool taskManagerShortcut(uint16_t key,uint16_t flags) { return key==0x1B && (flags&30)==6; }
constexpr bool localDesktopShortcut(bool fullscreen,uint16_t key,uint16_t flags) {
    if(taskManagerShortcut(key,flags)) return true;
    if(key==0x2E && (flags&12)==12) return true; // Ctrl+Alt+Del
    return !fullscreen && ((flags&16) || ((flags&8) && (key==0x09 || key==0x1B || key==0x73)));
}
constexpr bool localModifier(bool fullscreen,uint16_t key) {
    // Forward Caps Lock to macOS and also let Windows update its toggle state
    // and keyboard LED. Suppressing the physical key-down freezes that state.
    if(key==0x14) return true;
    return !fullscreen && key>=0xA0 && key<=0xA5;
}

}
