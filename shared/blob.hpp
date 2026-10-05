#pragma once
#include "protocol.hpp"
namespace td {
constexpr size_t ClipboardImageLimit=32*1024*1024,CursorImageLimit=512*1024,BlobChunk=3072;
inline std::deque<Bytes> blobPackets(Message kind,const Bytes& data,uint32_t id,size_t limit) {
    if(!id || data.empty() || data.size()>limit) return {};
    std::deque<Bytes> packets;
    for(size_t offset=0;offset<data.size();offset+=BlobChunk) {
        Writer w;w.put(uint8_t(kind));w.put(id);w.put(uint32_t(data.size()));w.put(uint32_t(offset));
        w.data.insert(w.data.end(),data.begin()+offset,data.begin()+std::min(data.size(),offset+BlobChunk));
        packets.push_back(std::move(w.data));
    }
    return packets;
}
class BlobAssembler {
    uint32_t transfer=0,total=0; Bytes pending;
public:
    void reset() { transfer=total=0;pending.clear(); }
    std::optional<Bytes> push(const Bytes& data,Message kind,size_t limit) {
        Reader r(data);
        if(r.get<uint8_t>()!=uint8_t(kind)) throw std::runtime_error("Invalid binary transfer type");
        auto id=r.get<uint32_t>(),size=r.get<uint32_t>(),offset=r.get<uint32_t>();
        auto count=data.size()-r.pos;
        if(!id || !size || size>limit || !count || count>BlobChunk || offset>size || count>size-offset)
            throw std::runtime_error("Invalid binary transfer bounds");
        if(!offset) { reset();transfer=id;total=size; }
        if(id!=transfer || size!=total || offset!=pending.size()) throw std::runtime_error("Invalid binary transfer order");
        pending.insert(pending.end(),data.begin()+r.pos,data.end());
        if(pending.size()!=total) return {};
        auto complete=std::move(pending);reset();return complete;
    }
};
}
