#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include "mac_arrow_data.hpp"

namespace td {
struct CursorRaster { int size,hotX,hotY; std::vector<uint32_t> pixels; };
struct CursorDimensions { int width,height; };
inline size_t bestCursorRepresentation(unsigned dpi,double logicalWidth,double logicalHeight,const std::vector<CursorDimensions>& images) {
    if(images.empty()) throw std::runtime_error("No cursor representation");
    double scale=std::clamp(dpi,96u,768u)/96.0;
    size_t best=0; bool fits=false;
    for(size_t i=0;i<images.size();++i) {
        bool candidate=images[i].width>=logicalWidth*scale && images[i].height>=logicalHeight*scale;
        int area=images[i].width*images[i].height,bestArea=images[best].width*images[best].height;
        if((candidate && !fits) || (candidate==fits && (fits?area<bestArea:area>bestArea))) { best=i;fits=candidate; }
    }
    return best;
}
struct CursorSample { int index; double weight; };
inline std::vector<CursorSample> cursorSamples(int destination,double scale,int sourceSize) {
    std::vector<CursorSample> samples;
    if(scale<1) {
        // Integrate actual pixel coverage once. Supersampling with bilinear
        // taps used to add an extra softening kernel to native cursor edges.
        double start=destination/scale,end=(destination+1)/scale;
        for(int i=std::max(0,int(std::floor(start)));i<std::min(sourceSize,int(std::ceil(end)));++i)
            samples.push_back({i,(std::min(end,i+1.0)-std::max(start,double(i)))*scale});
    } else {
        double center=(destination+0.5)/scale-0.5;
        int first=int(std::floor(center)); double fraction=center-first;
        if(first>=0 && first<sourceSize) samples.push_back({first,1-fraction});
        if(fraction>0 && first+1>=0 && first+1<sourceSize) samples.push_back({first+1,fraction});
    }
    return samples;
}
inline const std::vector<uint32_t>& macArrowPixels() {
    static const auto pixels=[] {
        std::vector<uint32_t> decoded;
        decoded.reserve(mac_arrow::Width*mac_arrow::Height);
        for(size_t i=0;i<sizeof(mac_arrow::Runs)/sizeof(mac_arrow::Runs[0]);i+=2)
            decoded.insert(decoded.end(),mac_arrow::Runs[i],mac_arrow::Runs[i+1]);
        return decoded;
    }();
    return pixels;
}
// Rasterize the exported system image, retaining its own outline and shadow.
// DPI changes scale the source pixels and native hotspot together.
inline CursorRaster rasterCursor(unsigned dpi,int width,int height,double logicalWidth,double logicalHeight,double hotX,double hotY,const std::vector<uint32_t>& source) {
    double uiScale=std::clamp(dpi,96u,768u)/96.0;
    double scale=logicalWidth*uiScale/width,scaleY=logicalHeight*uiScale/height;
    int size=int(std::ceil(std::max(logicalWidth,logicalHeight)*uiScale/32))*32;
    CursorRaster result{size,int(std::round(hotX*uiScale)),int(std::round(hotY*uiScale)),std::vector<uint32_t>(size_t(size)*size)};
    std::vector<std::vector<CursorSample>> columns(size),rows(size);
    for(int i=0;i<size;++i) { columns[i]=cursorSamples(i,scale,width);rows[i]=cursorSamples(i,scaleY,height); }
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        double channels[4]={};
        for(auto sy:rows[y]) for(auto sx:columns[x]) {
            auto value=source[size_t(sy.index)*width+sx.index];
            double weight=sx.weight*sy.weight;
            for(int channel=0;channel<4;++channel) channels[channel]+=((value>>(8*channel))&255)*weight;
        }
        uint32_t value=0;
        for(int channel=0;channel<4;++channel)
            value|=uint32_t(std::clamp(std::round(channels[channel]),0.0,255.0))<<(8*channel);
        result.pixels[size_t(y)*size+x]=value;
    }
    return result;
}
inline CursorRaster macStyleCursor(unsigned dpi) {
    return rasterCursor(dpi,mac_arrow::Width,mac_arrow::Height,double(mac_arrow::Width)/mac_arrow::Density,double(mac_arrow::Height)/mac_arrow::Density,mac_arrow::HotX/mac_arrow::Density,mac_arrow::HotY/mac_arrow::Density,macArrowPixels());
}
}
