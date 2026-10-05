#include "shortcuts.hpp"
#include "cursor.hpp"
#include <cstdlib>
#include <iostream>
#define REQUIRE(expr) do { if(!(expr)) { std::cerr<<__LINE__<<": "<<#expr<<"\n"; std::exit(1); } } while(0)
int main() {
    using namespace td;
    REQUIRE(keyboardKey(0x10,0x2A,false)==0xA0 && keyboardKey(0x10,0x36,false)==0xA1);
    REQUIRE(keyboardKey(0x11,0x1D,false)==0xA2 && keyboardKey(0x11,0x1D,true)==0xA3);
    REQUIRE(keyboardKey(0x12,0x38,false)==0xA4 && keyboardKey(0x12,0x38,true)==0xA5);
    REQUIRE(keyboardKey(0x0D,0x1C,true)==0x10D && keyboardKey(0xA5,0x38,true)==0xA5);
    REQUIRE(validFullscreenHotkey(DefaultFullscreenHotkey));
    REQUIRE(matchesFullscreenHotkey(DefaultFullscreenHotkey,0x0D,12));
    REQUIRE(matchesFullscreenHotkey(DefaultFullscreenHotkey,0x10D,12));
    REQUIRE(!matchesFullscreenHotkey(DefaultFullscreenHotkey,0x0D,14));
    REQUIRE(!matchesFullscreenHotkey(DefaultFullscreenHotkey,0x0D,28));
    REQUIRE(validFullscreenHotkey(0x0346)); // User-defined Ctrl+Shift+F
    for(auto shortcut:{0x001B,0x000D,0x067A,0x031B,0x071B,0x062E,0x0700,0x06A2,0x1246}) REQUIRE(!validFullscreenHotkey(uint16_t(shortcut)));
    REQUIRE(validFullscreenHotkey(0x0E23)); // Ctrl+Alt+End, extended key flag is not a modifier.
    REQUIRE(matchesFullscreenHotkey(0x0E23,0x23,12));
    REQUIRE(localDesktopShortcut(true,0x1B,6));
    REQUIRE(localDesktopShortcut(true,0x2E,12));
    REQUIRE(!localDesktopShortcut(true,0x09,8)); // Alt+Tab stays remote.
    REQUIRE(!localDesktopShortcut(true,0x73,8)); // Alt+F4 stays remote.
    REQUIRE(!localDesktopShortcut(true,0x52,16)); // Win+R stays remote.
    REQUIRE(!localDesktopShortcut(true,0x1B,4)); // Ctrl+Esc cannot open local Start.
    REQUIRE(localDesktopShortcut(false,0x09,8));
    REQUIRE(localDesktopShortcut(false,0x52,16));
    REQUIRE(!localModifier(true,0xA2) && !localModifier(true,0xA0));
    REQUIRE(localModifier(false,0xA2) && localModifier(false,0xA0));
    REQUIRE(!localModifier(true,0xA4) && localModifier(false,0xA4));
    // Caps Lock is a local state update as well as remote input in both modes.
    REQUIRE(localModifier(true,0x14) && localModifier(false,0x14));
    REQUIRE(!localModifier(true,0x41) && !localModifier(false,0x41));
    REQUIRE(!localModifier(true,0x5B) && !localModifier(true,0x09));
    REQUIRE(taskManagerShortcut(0x1B,6));
    REQUIRE(!taskManagerShortcut(0x1B,14));
    REQUIRE(!taskManagerShortcut(0x1B,22));
    // Native screenshot cursor has a central hotspot; it must be scaled from
    // macOS points exactly once, independently of the streamed HiDPI resolution.
    std::vector<uint32_t> pixels(32*32,0xffffffff);
    for(auto dpi:{96u,120u,144u,192u,288u}) {
        auto raster=rasterCursor(dpi,32,32,32,32,16,16,pixels);
        REQUIRE(raster.hotX==int(std::round(16*dpi/96.0)) && raster.hotY==raster.hotX);
        REQUIRE(raster.hotX<raster.size && raster.pixels.size()==size_t(raster.size*raster.size));
        auto arrow=macStyleCursor(dpi);
        REQUIRE(arrow.hotX==int(std::round(mac_arrow::HotX/mac_arrow::Density*dpi/96.0)));
    }
    std::cout<<"Fullscreen shortcut and reserved system shortcut tests passed\n";
}
