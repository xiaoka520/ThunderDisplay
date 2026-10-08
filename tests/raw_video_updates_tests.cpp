#include "raw_video_updates.hpp"
#include "raw_video_inbox.hpp"
#include <cassert>
#include <iostream>

static td::Bytes prefix(uint32_t base,uint16_t kind,uint32_t count) {
    td::Writer w; w.put(uint32_t(0x54444431)); w.put(base); w.put(kind); w.put(uint16_t(0)); w.put(count); return w.data;
}
int main() {
    for(uint16_t width:{320,322,4096}) {
        const uint16_t height=240; td::RawVideoUpdates receiver(width,height);
        td::Bytes image(td::rawPacked10Bytes(width,height)),full=prefix(0,0,0);
        full.insert(full.end(),image.begin(),image.end()); assert(receiver.apply(full,1)==image);
        // Shared Swift/C++ golden: tile zero, first byte changed to 0x7f.
        auto delta=prefix(1,1,1); delta.insert(delta.end(),{0,0}); delta.resize(16+2+160*96);
        delta[18]=0x7f; image[0]=0x7f; assert(receiver.apply(delta,3)==image); // Capture 2 was skipped.
        assert(receiver.apply(prefix(3,1,0),4)==image);
        for(auto bad:{prefix(3,1,0),prefix(4,1,1),prefix(4,2,0)}) {
            try { receiver.apply(bad,5); assert(false); } catch(const std::runtime_error&) {}
        }
        auto truncated=delta; truncated[7]=4; truncated.pop_back();
        try { receiver.apply(truncated,5); assert(false); } catch(const std::runtime_error&) {}
        auto extra=prefix(4,1,0); extra.push_back(0);
        try { receiver.apply(extra,5); assert(false); } catch(const std::runtime_error&) {}
        auto duplicate=delta; duplicate[7]=4; duplicate[15]=2;
        duplicate.insert(duplicate.end(),delta.begin()+16,delta.end());
        try { receiver.apply(duplicate,5); assert(false); } catch(const std::runtime_error&) {}
        auto outside=delta; outside[7]=4; outside[16]=255; outside[17]=255;
        try { receiver.apply(outside,5); assert(false); } catch(const std::runtime_error&) {}
        // Failed validation must leave both the image and its frame ID intact.
        assert(receiver.apply(prefix(4,1,0),6)==image);
        // Replacing a pending displayed snapshot must not omit earlier patches.
        td::RawVideoInbox inbox(image.size()); auto a=inbox.acquire(); assert(a);
        inbox.pixels(*a)=image; assert(inbox.publish({*a,0,0}));
        const unsigned columns=(width+127)/128,index=columns*((height+63)/64)-1;
        const unsigned w=width-(columns-1)*128,h=height-3*64,bytes=(w*10+7)/8,stride=(width*10+7)/8;
        auto corner=prefix(6,1,1); corner.push_back(uint8_t(index>>8)); corner.push_back(uint8_t(index));
        corner.resize(18+size_t(bytes)*(h+h/2)); corner.back()=1;
        image.back()=1; const auto& snapshot=receiver.apply(corner,7); assert(snapshot==image);
        auto b=inbox.acquire(); assert(b); inbox.pixels(*b)=snapshot; assert(inbox.publish({*b,0,0}));
        auto displayed=inbox.take(); assert(displayed && inbox.pixels(displayed->slot)==image && inbox.replacedFrames()==1);
        inbox.release(displayed->slot); inbox.stop();
        if(width%4) {
            auto padding=corner; padding[7]=7; padding.back()=0x80;
            try { receiver.apply(padding,8); assert(false); } catch(const std::runtime_error&) {}
            auto badFull=full; badFull[16+stride-1]=0x80;
            try { receiver.apply(badFull,8); assert(false); } catch(const std::runtime_error&) {}
        }
        assert(receiver.apply(full,9)==td::Bytes(image.size()));
    }
    td::Bytes header; td::Writer w; w.put(uint32_t(0x54445246)); w.put(uint32_t(1)); w.put(uint32_t(16)); w.put(uint64_t(0)); header=w.data;
    assert(td::RawFrameHeader(header,144000,true).size==16);
    try { td::RawFrameHeader bad(header,144000); assert(false); } catch(const std::runtime_error&) {}
    std::cout<<"Exact raw updates, edge chroma, dropped snapshots and malformed baseline recovery passed\n";
}
