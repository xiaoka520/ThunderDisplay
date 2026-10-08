#pragma once
#include "raw_video_damage.hpp"

namespace td {
// Reconstruct on the receiver before publishing into the latest-frame inbox.
// Pixel-worker drops therefore discard complete independent snapshots, never
// unapplied deltas. Validate the entire update before modifying the baseline.
class RawVideoUpdates {
    uint16_t width,height;
    uint32_t previous=0;
    Bytes image;
    std::vector<uint64_t> revisions;
    uint64_t revision=0;
    struct Tile { size_t offset; unsigned index,bytes,rows,x,y; };
public:
    RawVideoUpdates(uint16_t w,uint16_t h):width(w),height(h),image(rawPacked10Bytes(w,h)),revisions(rawTileCount(w,h)) {}
    size_t snapshot(Bytes& destination,std::vector<uint64_t>& acknowledged) const {
        if(!previous) throw std::runtime_error("Raw snapshot needs a baseline");
        if(destination.size()!=image.size()) throw std::runtime_error("Invalid raw snapshot size");
        const auto regions=rawChangedRegions(width,height,revisions,acknowledged);
        size_t bytes=0;
        for(const auto& region:regions) bytes+=copyRaw10Region(image,destination,width,height,region);
        acknowledged=revisions; return bytes;
    }
    const Bytes& apply(const Bytes& payload,uint32_t id) {
        if(!id || (previous && !newer(id,previous)) || payload.size()<16 || payload.size()>image.size()+16)
            throw std::runtime_error("Invalid raw update frame");
        Reader r(payload);
        if(r.get<uint32_t>()!=0x54444431) throw std::runtime_error("Invalid raw update magic");
        const auto base=r.get<uint32_t>(); const auto kind=r.get<uint16_t>();
        if(r.get<uint16_t>()) throw std::runtime_error("Invalid raw update flags");
        const auto count=r.get<uint32_t>();
        if(kind==0) {
            if(base || count || payload.size()!=image.size()+16) throw std::runtime_error("Invalid full raw update");
            if(width%4) {
                const unsigned stride=(width*10+7)/8;
                for(unsigned row=0;row<unsigned(height)+height/2;++row)
                    if(payload[16+size_t(row+1)*stride-1]&0xf0) throw std::runtime_error("Invalid full raw padding");
            }
            std::memcpy(image.data(),payload.data()+16,image.size());
            std::fill(revisions.begin(),revisions.end(),++revision);
        } else if(kind==1) {
            const unsigned columns=(width+127)/128,total=columns*((height+63)/64),stride=(width*10+7)/8;
            if(!previous || base!=previous || count>total) throw std::runtime_error("Invalid raw update baseline");
            std::vector<Tile> tiles; tiles.reserve(count); size_t offset=16; unsigned last=0;
            for(unsigned i=0;i<count;++i) {
                if(offset+2>payload.size()) throw std::runtime_error("Truncated raw update index");
                unsigned index=(unsigned(payload[offset])<<8)|payload[offset+1]; offset+=2;
                if(index>=total || (i && index<=last)) throw std::runtime_error("Invalid raw update tile order");
                last=index;
                const unsigned x=index%columns*128,y=index/columns*64;
                const unsigned w=std::min<unsigned>(128,width-x),h=std::min<unsigned>(64,height-y),bytes=(w*10+7)/8;
                const size_t length=size_t(bytes)*(h+h/2);
                if(length>payload.size()-offset) throw std::runtime_error("Truncated raw update pixels");
                if(w%4) for(unsigned row=0;row<h+h/2;++row)
                    if(payload[offset+size_t(row)*bytes+bytes-1]&0xf0) throw std::runtime_error("Invalid raw update padding");
                tiles.push_back({offset,index,bytes,h,x,y}); offset+=length;
            }
            if(offset!=payload.size()) throw std::runtime_error("Extra raw update pixels");
            ++revision;
            for(const auto& tile:tiles) {
                revisions[tile.index]=revision;
                const auto* source=payload.data()+tile.offset;
                for(unsigned row=0;row<tile.rows;++row,source+=tile.bytes)
                    std::memcpy(image.data()+size_t(tile.y+row)*stride+tile.x*10/8,source,tile.bytes);
                for(unsigned row=0;row<tile.rows/2;++row,source+=tile.bytes)
                    std::memcpy(image.data()+size_t(height+tile.y/2+row)*stride+tile.x*10/8,source,tile.bytes);
            }
        } else throw std::runtime_error("Invalid raw update kind");
        previous=id; return image;
    }
};
}
