#pragma once
#include <algorithm>

namespace td {
// Logical pixels. Optional pairing and manual controls collapse as complete cards.
struct SetupLayout {
    static constexpr int DefaultLogHeight=220,MinLogHeight=120,MaxLogHeight=960;
    int width,height; bool paired=false,manual=false;
    int preferredLogHeight=DefaultLogHeight;
    static int boundedLogHeight(int value) { return std::clamp(value,MinLogHeight,MaxLogHeight); }
    int fullWidth() const { return std::max(1,width-56); }
    int fieldWidth() const { return std::max(1,width-248); }
    int networkTop() const { return 414; }
    int networkHeight() const { return paired?278:182; }
    int qualityTop() const { return networkTop()+networkHeight()+20; }
    int qualityHeight() const { return 638; }
    int diagnosticsTop() const { return qualityTop()+qualityHeight()+20; }
    int logTop() const { return diagnosticsTop()+102; }
    int logHeight() const { return boundedLogHeight(preferredLogHeight); }
    int logResizeTop() const { return logTop()+logHeight()+4; }
    int diagnosticsHeight() const { return 146+logHeight(); }
    int shortcutsTop() const { return diagnosticsTop()+diagnosticsHeight()+16; }
    int minimumHeight() const { return shortcutsTop()+68; }
    int extraHeight() const { return std::max(0,height-minimumHeight()); }
    int contentHeight() const { return minimumHeight()+extraHeight(); }
    struct Cell { int x,width; };
    Cell column(int index,int count,int gap) const {
        int cellWidth=(width-96-(count-1)*gap)/count;
        return {48+index*(cellWidth+gap),cellWidth};
    }
};
}
