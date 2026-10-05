#pragma once
#include <algorithm>
#include <cstdint>

namespace td {
// Settings document position, in device pixels. Focus is allowed off-screen
// after a user scroll; only a real focus transition should reveal a control.
class SetupScroll {
    int position_=0, limit_=0, page_=1, wheelRemainder_=0;
public:
    int position() const { return position_; }
    void resize(int contentHeight,int viewportHeight) {
        page_=std::max(1,viewportHeight);
        limit_=std::max(0,contentHeight-page_);
        if(position_>limit_) moveTo(limit_);
    }
    void moveTo(int64_t position) {
        position_=int(std::clamp<int64_t>(position,0,limit_));
        wheelRemainder_=0;
    }
    void wheel(int delta,int pixelsPerNotch) {
        int64_t distance=int64_t(delta)*pixelsPerNotch+wheelRemainder_;
        wheelRemainder_=int(distance%120);
        position_=int(std::clamp<int64_t>(int64_t(position_)-distance/120,0,limit_));
        if((position_==0 && distance>0) || (position_==limit_ && distance<0)) wheelRemainder_=0;
    }
    void reveal(int top,int bottom,bool focusChanged) {
        if(!focusChanged) return;
        if(top<position_) moveTo(top);
        else if(bottom>position_+page_) moveTo(bottom-page_);
    }
};

enum class SetupWheelTarget { Document, Dropdown, Diagnostics };
inline SetupWheelTarget setupWheelTarget(bool dropdownOpen,bool overDiagnostics,int delta,int position,int limit) {
    if(dropdownOpen) return SetupWheelTarget::Dropdown;
    if(overDiagnostics && ((delta>0 && position>0) || (delta<0 && position<limit))) return SetupWheelTarget::Diagnostics;
    return SetupWheelTarget::Document;
}
}
