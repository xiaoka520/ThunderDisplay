#pragma once
#include "blob.hpp"
namespace td {
struct CursorPayload {
    double width,height,hotX,hotY;
    std::vector<Bytes> images;
    explicit CursorPayload(const Bytes& data) {
        if(data.size()>CursorImageLimit) throw std::runtime_error("Cursor size limit");
        Reader r(data); auto version=r.get<uint8_t>();
        if(version!=1 && version!=2) throw std::runtime_error("Cursor version");
        width=r.get<uint32_t>()/65536.0;height=r.get<uint32_t>()/65536.0;
        hotX=r.get<int32_t>()/65536.0;hotY=r.get<int32_t>()/65536.0;
        if(width<1 || height<1 || width>256 || height>256 || hotX<0 || hotY<0 || hotX>=width || hotY>=height)
            throw std::runtime_error("Cursor dimensions/hotspot");
        if(version==1) { images.emplace_back(data.begin()+r.pos,data.end());r.pos=data.size(); }
        else {
            auto count=r.get<uint8_t>();
            if(!count || count>8) throw std::runtime_error("Cursor representation count");
            for(unsigned i=0;i<count;++i) {
                auto size=r.get<uint32_t>();
                if(size>data.size()-r.pos) throw std::runtime_error("Cursor PNG bounds");
                images.emplace_back(data.begin()+r.pos,data.begin()+r.pos+size);r.pos+=size;
            }
        }
        if(!r.end()) throw std::runtime_error("Trailing cursor data");
        for(const auto& image:images)
            if(image.size()<8 || std::memcmp(image.data(),"\x89PNG\r\n\x1a\n",8)) throw std::runtime_error("Cursor PNG signature");
    }
};
}
