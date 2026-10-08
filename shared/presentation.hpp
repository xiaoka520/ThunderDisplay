#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
namespace td {
struct PixelRect { int left=0,top=0,right=0,bottom=0; };
struct PresentationGeometry {
    PixelRect source,destination;
    double scale=1;
    PresentationGeometry(int frameWidth,int frameHeight,int windowWidth,int windowHeight,bool exact) {
        if(frameWidth<2 || frameHeight<2 || windowWidth<1 || windowHeight<1) throw std::runtime_error("Invalid presentation dimensions");
        if(exact) {
            int w=std::min(frameWidth,windowWidth),h=std::min(frameHeight,windowHeight);
            source={(frameWidth-w)/2,(frameHeight-h)/2,(frameWidth-w)/2+w,(frameHeight-h)/2+h};
            destination={(windowWidth-w)/2,(windowHeight-h)/2,(windowWidth-w)/2+w,(windowHeight-h)/2+h};
        } else {
            scale=std::min(double(windowWidth)/frameWidth,double(windowHeight)/frameHeight);
            int w=std::max(1,int(frameWidth*scale)),h=std::max(1,int(frameHeight*scale));
            source={0,0,frameWidth,frameHeight}; destination={(windowWidth-w)/2,(windowHeight-h)/2,(windowWidth-w)/2+w,(windowHeight-h)/2+h};
        }
    }
    bool pointer(int px,int py,bool dragging,int frameWidth,int frameHeight,int& x,int& y) const {
        int w=destination.right-destination.left,h=destination.bottom-destination.top;
        if(w<=1 || h<=1) return false;
        if(!dragging && (px<destination.left || px>=destination.right || py<destination.top || py>=destination.bottom)) return false;
        double sx=source.left+std::clamp(double(px-destination.left)/(w-1),0.0,1.0)*(source.right-source.left-1);
        double sy=source.top+std::clamp(double(py-destination.top)/(h-1),0.0,1.0)*(source.bottom-source.top-1);
        x=int(sx/(frameWidth-1)*65535); y=int(sy/(frameHeight-1)*65535); return true;
    }
    bool remotePointer(uint16_t x,uint16_t y,int frameWidth,int frameHeight,int& px,int& py) const {
        double sx=double(x)/65535*(frameWidth-1),sy=double(y)/65535*(frameHeight-1);
        if(sx<source.left || sx>source.right-1 || sy<source.top || sy>source.bottom-1) return false;
        px=destination.left+int(std::round((sx-source.left)/std::max(1,source.right-source.left-1)*(destination.right-destination.left-1)));
        py=destination.top+int(std::round((sy-source.top)/std::max(1,source.bottom-source.top-1)*(destination.bottom-destination.top-1)));
        return true;
    }
};
}
