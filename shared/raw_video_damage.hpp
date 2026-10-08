#pragma once
#include "raw_video.hpp"

namespace td {
struct RawVideoRegion { unsigned x,y,width,height; };
inline size_t rawTileCount(uint16_t width,uint16_t height) {
    rawP010Bytes(width,height); return size_t((width+127)/128)*((height+63)/64);
}
// Revision equality is local metadata, not a wire frame-ID comparison. Each
// consumer/buffer keeps its own acknowledged revisions, so dropped snapshots
// cannot lose patches that arrived between displayed frames.
inline std::vector<RawVideoRegion> rawChangedRegions(uint16_t width,uint16_t height,
    const std::vector<uint64_t>& current,const std::vector<uint64_t>& previous) {
    const unsigned columns=(width+127)/128,rows=(height+63)/64;
    const auto total=rawTileCount(width,height);
    if(current.size()!=total) throw std::runtime_error("Invalid raw tile revisions");
    if(previous.size()!=total) return {{0,0,width,height}};
    size_t changed=0;
    for(size_t i=0;i<total;++i) changed+=current[i]!=previous[i];
    if(!changed) return {};
    if(changed>total/4) return {{0,0,width,height}};
    std::vector<RawVideoRegion> regions;
    regions.reserve(changed);
    for(unsigned row=0;row<rows;++row) for(unsigned column=0;column<columns;) {
        if(current[size_t(row)*columns+column]==previous[size_t(row)*columns+column]) { ++column; continue; }
        const unsigned start=column;
        do { ++column; } while(column<columns && current[size_t(row)*columns+column]!=previous[size_t(row)*columns+column]);
        RawVideoRegion region{start*128,row*64,std::min<unsigned>(width,column*128)-start*128,std::min<unsigned>(64,height-row*64)};
        bool merged=false;
        for(auto& older:regions) if(older.x==region.x && older.width==region.width && older.y+older.height==region.y) {
            older.height+=region.height; merged=true; break;
        }
        if(!merged) regions.push_back(region);
    }
    return regions;
}
inline void checkRawRegion(uint16_t width,uint16_t height,const RawVideoRegion& region) {
    if(!region.width || !region.height || region.x%4 || region.y%2 || region.width%2 || region.height%2 ||
       region.x>=width || region.y>=height || region.width>width-region.x || region.height>height-region.y ||
       (region.width%4 && region.x+region.width!=width)) throw std::runtime_error("Invalid raw pixel region");
}
inline size_t copyRaw10Region(const Bytes& source,Bytes& target,uint16_t width,uint16_t height,const RawVideoRegion& region) {
    if(source.size()!=rawPacked10Bytes(width,height) || target.size()!=source.size()) throw std::runtime_error("Invalid raw snapshot size");
    checkRawRegion(width,height,region);
    if(!region.x && !region.y && region.width==width && region.height==height) {
        std::memcpy(target.data(),source.data(),source.size()); return source.size();
    }
    const unsigned stride=(width*10+7)/8,bytes=(region.width*10+7)/8,offset=region.x*10/8;
    for(unsigned row=0;row<region.height;++row) {
        const auto position=size_t(region.y+row)*stride+offset;
        std::memcpy(target.data()+position,source.data()+position,bytes);
    }
    for(unsigned row=0;row<region.height/2;++row) {
        const auto position=size_t(height+region.y/2+row)*stride+offset;
        std::memcpy(target.data()+position,source.data()+position,bytes);
    }
    return size_t(bytes)*(region.height+region.height/2);
}
inline void unpackRaw10Region(const Bytes& packed,uint16_t width,uint16_t height,Bytes& p010,
    const RawVideoRegion& region,bool useVector=true) {
    if(packed.size()!=rawPacked10Bytes(width,height) || p010.size()!=rawP010Bytes(width,height)) throw std::runtime_error("Invalid raw unpack size");
    checkRawRegion(width,height,region);
    const unsigned stride=(width*10+7)/8,rowBytes=(region.width*10+7)/8;
    static const bool vector=raw10VectorAvailable();
    for(unsigned row=0;row<region.height+region.height/2;++row) {
        const unsigned sourceRow=row<region.height?region.y+row:height+region.y/2+row-region.height;
        const auto* input=packed.data()+size_t(sourceRow)*stride+region.x*10/8;
        auto* output=p010.data()+(size_t(sourceRow)*width+region.x)*2;
        unsigned x=useVector && vector?unpackRaw10VectorRow(input,output,region.width,rowBytes):0;
        input+=size_t(x)*5/4; output+=size_t(x)*2;
        for(;x+4<=region.width;x+=4,input+=5,output+=8) {
            uint32_t lo; std::memcpy(&lo,input,4);
            const uint64_t bits=uint64_t(lo)|(uint64_t(input[4])<<32);
            const uint16_t words[4]={uint16_t((bits&1023)<<6),uint16_t(((bits>>10)&1023)<<6),uint16_t(((bits>>20)&1023)<<6),uint16_t(((bits>>30)&1023)<<6)};
            std::memcpy(output,words,8);
        }
        if(x<region.width) {
            if(input[2]&0xf0) throw std::runtime_error("Invalid packed row padding");
            const uint32_t bits=uint32_t(input[0])|(uint32_t(input[1])<<8)|(uint32_t(input[2])<<16);
            const uint16_t words[2]={uint16_t((bits&1023)<<6),uint16_t(((bits>>10)&1023)<<6)};
            std::memcpy(output,words,4);
        }
    }
}
inline size_t rawRegionP010Bytes(const std::vector<RawVideoRegion>& regions) {
    size_t bytes=0; for(const auto& region:regions) bytes+=size_t(region.width)*region.height*3; return bytes;
}
}
