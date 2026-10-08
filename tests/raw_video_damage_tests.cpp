#include "raw_video_updates.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <random>

static td::Bytes prefix(uint32_t base,uint16_t kind,uint32_t count) {
    td::Writer writer; writer.put(uint32_t(0x54444431)); writer.put(base); writer.put(kind); writer.put(uint16_t(0)); writer.put(count); return writer.data;
}
static td::Bytes full(const td::Bytes& pixels) {
    auto data=prefix(0,0,0); data.insert(data.end(),pixels.begin(),pixels.end()); return data;
}
static td::Bytes patch(uint16_t width,uint16_t height,uint32_t base,const std::vector<unsigned>& indices,std::mt19937& random) {
    auto data=prefix(base,1,uint32_t(indices.size())); const unsigned columns=(width+127)/128;
    for(auto index:indices) {
        const unsigned x=index%columns*128,y=index/columns*64,w=std::min<unsigned>(128,width-x),h=std::min<unsigned>(64,height-y),bytes=(w*10+7)/8;
        data.push_back(uint8_t(index>>8)); data.push_back(uint8_t(index));
        for(unsigned row=0;row<h+h/2;++row) for(unsigned column=0;column<bytes;++column) {
            auto value=uint8_t(random()); if(w%4 && column+1==bytes) value&=15; data.push_back(value);
        }
    }
    return data;
}
int main() {
    std::mt19937 random(927);
    for(const auto dimensions:std::array<std::pair<uint16_t,uint16_t>,4>{{{320,240},{322,242},{4096,2560},{4094,2562}}}) {
        const auto width=dimensions.first,height=dimensions.second;
        td::RawVideoUpdates updates(width,height);
        td::Bytes pixels(td::rawPacked10Bytes(width,height));
        for(auto& value:pixels) value=uint8_t(random());
        const unsigned stride=(width*10+7)/8;
        if(width%4) for(unsigned row=0;row<unsigned(height)+height/2;++row) pixels[size_t(row+1)*stride-1]&=15;
        updates.apply(full(pixels),1);
        std::array<td::Bytes,3> snapshots;
        std::array<std::vector<uint64_t>,3> revisions;
        for(auto& snapshot:snapshots) snapshot.resize(pixels.size());
        td::Bytes converted(td::rawP010Bytes(width,height)),expected(converted.size());
        std::vector<uint64_t> acknowledged;
        uint64_t copied=0,processed=0,complete=0;
        for(uint32_t frame=1;frame<=91;++frame) {
            if(frame>1) {
                std::vector<unsigned> indices;
                const unsigned total=unsigned(td::rawTileCount(width,height));
                if(frame%7) {
                    indices.push_back((frame*17)%total);
                    if(frame%3==0) indices.push_back(total-1);
                    std::sort(indices.begin(),indices.end()); indices.erase(std::unique(indices.begin(),indices.end()),indices.end());
                }
                pixels=updates.apply(patch(width,height,frame-1,indices,random),frame);
            }
            const unsigned slot=frame%3;
            copied+=updates.snapshot(snapshots[slot],revisions[slot]);
            assert(snapshots[slot]==pixels);
            // The consumer deliberately skips two out of three snapshots and
            // sometimes fails to submit a converted frame. Revisions must
            // carry the union of every change since its last successful frame.
            if(frame%3 && frame!=1 && frame!=91) continue;
            const auto regions=td::rawChangedRegions(width,height,revisions[slot],acknowledged);
            for(const auto& region:regions) td::unpackRaw10Region(snapshots[slot],width,height,converted,region);
            td::unpackRaw10(pixels,width,height,expected,false);
            assert(converted==expected);
            processed+=td::rawRegionP010Bytes(regions); ++complete;
            if(frame%5) acknowledged=revisions[slot];
        }
        assert(copied<pixels.size()*91 && processed<converted.size()*complete);
        auto& snapshot=snapshots[1]; auto& version=revisions[1];
        updates.snapshot(snapshot,version);
        const auto before=version;
        auto invalid=patch(width,height,91,{0},random); invalid.push_back(0);
        try { updates.apply(invalid,92); assert(false); } catch(const std::runtime_error&) {}
        assert(updates.snapshot(snapshot,version)==0 && version==before && snapshot==pixels);
        pixels.assign(pixels.size(),0); updates.apply(full(pixels),92);
        assert(updates.snapshot(snapshot,version)==pixels.size() && snapshot==pixels);
        for(const auto& region:td::rawChangedRegions(width,height,version,acknowledged)) td::unpackRaw10Region(snapshot,width,height,converted,region,false);
        assert(converted==td::Bytes(converted.size()));
        // Invalid crop/padding metadata must fail before touching a snapshot.
        for(const auto region:std::array<td::RawVideoRegion,4>{{{2,0,128,64},{0,1,128,64},{0,0,127,64},{0,0,unsigned(width)+2,64}}}) {
            try { td::unpackRaw10Region(snapshot,width,height,converted,region); assert(false); } catch(const std::runtime_error&) {}
        }
        std::cout<<width<<"x"<<height<<" exact skipped/failing snapshots; copied="<<copied<<" converted="<<processed<<"\n";
    }
    std::vector<uint64_t> older(12,1),newer=older;
    newer[0]=newer[1]=newer[3]=newer[4]=2;
    auto regions=td::rawChangedRegions(384,256,newer,older); // Broad changes use the full-frame fast path.
    assert(regions.size()==1 && regions[0].width==384 && regions[0].height==256);
    older.assign(64,1); newer=older; newer[0]=newer[1]=newer[8]=newer[9]=2;
    regions=td::rawChangedRegions(1024,512,newer,older);
    assert(regions.size()==1 && regions[0].width==256 && regions[0].height==128);
    assert(td::rawRegionP010Bytes(regions)==256*128*3);
    assert(td::rawChangedRegions(1024,512,newer,newer).empty());
    std::cout<<"Revision-based partial snapshots, all ten-bit values, chroma edges, drop accumulation and failed-submission retry passed\n";
}
