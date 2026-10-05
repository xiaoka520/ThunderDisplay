#pragma once
#include "protocol.hpp"

namespace td {
constexpr size_t ClipboardLimit=65536, ClipboardChunk=3072;
inline bool clipboardUTF8(const Bytes& text) {
    size_t i=0;
    while(i<text.size()) {
        auto c=text[i++]; if(c==0) return false;
        if(c<128) continue;
        unsigned count=0; uint32_t value=0,minimum=0;
        if(c>=0xc2 && c<=0xdf) { count=1; value=c&31; minimum=0x80; }
        else if(c>=0xe0 && c<=0xef) { count=2; value=c&15; minimum=0x800; }
        else if(c>=0xf0 && c<=0xf4) { count=3; value=c&7; minimum=0x10000; }
        else return false;
        if(count>text.size()-i) return false;
        while(count--) { auto next=text[i++]; if((next&0xc0)!=0x80) return false; value=(value<<6)|(next&63); }
        if(value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return false;
    }
    return true;
}
inline std::deque<Bytes> clipboardPackets(const std::string& text,uint32_t id) {
    Bytes data(text.begin(),text.end());
    if(!id || data.size()>ClipboardLimit || !clipboardUTF8(data)) return {};
    std::deque<Bytes> result; size_t offset=0;
    do {
        auto size=std::min(ClipboardChunk,data.size()-offset);
        Writer w; w.put(uint8_t(Message::ClipboardText)); w.put(id); w.put(uint32_t(data.size())); w.put(uint32_t(offset));
        w.data.insert(w.data.end(),data.begin()+offset,data.begin()+offset+size);
        result.push_back(std::move(w.data)); offset+=size;
    } while(offset<data.size());
    return result;
}
class ClipboardAssembler {
    uint32_t transfer=0,total=0;
    Bytes pending;
public:
    void reset() { transfer=total=0; pending.clear(); }
    std::optional<std::string> push(const Bytes& data) {
        Reader r(data);
        if(r.get<uint8_t>()!=uint8_t(Message::ClipboardText)) throw std::runtime_error("Invalid clipboard type");
        auto id=r.get<uint32_t>(),size=r.get<uint32_t>(),offset=r.get<uint32_t>();
        auto count=data.size()-r.pos;
        if(!id || size>ClipboardLimit || count>ClipboardChunk || offset>size || count>size-offset || (!count && size))
            throw std::runtime_error("Invalid clipboard size");
        if(offset==0) { transfer=id; total=size; pending.clear(); }
        if(id!=transfer || size!=total || offset!=pending.size()) throw std::runtime_error("Invalid clipboard order");
        pending.insert(pending.end(),data.begin()+r.pos,data.end());
        if(pending.size()!=total) return {};
        if(!clipboardUTF8(pending)) throw std::runtime_error("Invalid clipboard UTF-8");
        std::string text(pending.begin(),pending.end()); reset(); return text;
    }
};
}
