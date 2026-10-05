#pragma once
#include <algorithm>

namespace td {
// Logical pixels. Optional pairing and manual controls collapse as complete cards.
struct SetupLayout {
    int width,height; bool paired=false,manual=false;
    int fullWidth() const { return std::max(1,width-56); }
    int fieldWidth() const { return std::max(1,width-248); }
    int networkTop() const { return 414; }
    int networkHeight() const { return paired?278:182; }
    int qualityTop() const { return networkTop()+networkHeight()+20; }
    int qualityHeight() const { return 510; }
    int diagnosticsTop() const { return qualityTop()+qualityHeight()+20; }
    int minimumHeight() const { return diagnosticsTop()+242; }
    int extraHeight() const { return std::max(0,height-minimumHeight()); }
    int contentHeight() const { return minimumHeight()+extraHeight(); }
    struct Cell { int x,width; };
    Cell column(int index,int count,int gap) const {
        int cellWidth=(width-96-(count-1)*gap)/count;
        return {48+index*(cellWidth+gap),cellWidth};
    }
};
}
