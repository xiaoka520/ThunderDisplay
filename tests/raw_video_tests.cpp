#include "raw_video.hpp"
#include <array>
#include <cassert>
#include <iostream>
int main() {
    const uint64_t session=0x0102030405060708;
    assert(td::rawP010Bytes(4096,2560)==31457280);
    assert(td::rawP010Rate(4096,2560,60)==15099494400ull);
    assert(td::rawPacked10Bytes(4096,2560)==19660800);
    assert(td::rawVideoRate(4096,2560,60,td::Codec::RawPacked10)==9437184000ull);
    for(auto codec:{td::Codec::RawP010,td::Codec::RawPacked10,td::Codec::RawDelta10}) {
        td::Writer w; w.put(uint8_t(td::Message::WelcomeWide)); w.put(uint16_t(2)); w.put(session); w.put(uint8_t(codec));
        w.put(uint16_t(4096)); w.put(uint16_t(2560)); w.put(uint16_t(60)); w.put(td::rawVideoRate(4096,2560,60,codec));
        td::Welcome welcome(w.data); assert(welcome.codec==codec && welcome.settings.bitDepth==10);
        w.data[0]=uint8_t(td::Message::Welcome);
        try { td::Welcome legacy(w.data); assert(false); } catch(const std::runtime_error&) {}
    }
    for(uint16_t width:{320,322}) {
        constexpr uint16_t height=240;
        td::Bytes packed(td::rawPacked10Bytes(width,height)),p010(td::rawP010Bytes(width,height)),expected(p010.size());
        const auto rowBytes=(width*10+7)/8;
        for(unsigned row=0;row<height+height/2;++row) for(unsigned x=0;x<width;++x) {
            uint16_t value=uint16_t((row*width+x)%1024);
            if(!row && x<4) value=std::array<uint16_t,4>{0,1,512,1023}[x];
            const auto word=uint16_t(value<<6); std::memcpy(expected.data()+(size_t(row)*width+x)*2,&word,2);
            // Independent, bit-at-a-time reference for the production unpacker.
            for(unsigned bit=0;bit<10;++bit) {
                auto offset=x*10+bit; packed[size_t(row)*rowBytes+offset/8]|=uint8_t(((value>>bit)&1)<<(offset%8));
            }
        }
        assert((td::Bytes(packed.begin(),packed.begin()+5)==td::Bytes{0,4,0,224,255}));
        td::unpackRaw10(packed,width,height,p010); assert(p010==expected);
        td::unpackRaw10(packed,width,height,p010,false); assert(p010==expected);
        auto truncated=packed; truncated.pop_back();
        try { td::unpackRaw10(truncated,width,height,p010); assert(false); } catch(const std::runtime_error&) {}
        if(width%4) {
            packed[rowBytes-1]|=0x80;
            try { td::unpackRaw10(packed,width,height,p010); assert(false); } catch(const std::runtime_error&) {}
        }
    }
    assert((td::rawHandshake(session)==td::Bytes{0x54,0x44,0x52,0x41,1,2,3,4,5,6,7,8}));
    const td::Bytes endpoint{22,1,2,3,4,5,6,7,8,0xC3,0x50};
    assert(td::rawEndpoint(endpoint,session)==50000);
    const td::Bytes header{0x54,0x44,0x52,0x46,0,0,0,1,0,3,0x84,0,0,0,0,0,0,0,0,2};
    const td::RawFrameHeader frame(header,230400); assert(frame.id==1 && frame.pts==2);
    const td::Bytes pointer{21,1,2,3,4,5,6,7,8,1,0x12,0x34,0xff,0xff};
    const td::CursorPosition cursor(pointer,session); assert(cursor.visible && cursor.x==0x1234 && cursor.y==65535);
    for(auto invalid:{td::Bytes(pointer.begin(),pointer.end()-1),td::Bytes{21,1,2,3,4,5,6,7,8,2,0,0,0,0},td::Bytes{21,1,2,3,4,5,6,7,8,0,0,1,0,0}}) {
        try { td::CursorPosition bad(invalid,session); assert(false); } catch(const std::runtime_error&) {}
    }
    try { td::CursorPosition bad(pointer,session+1); assert(false); } catch(const std::runtime_error&) {}
    try { td::RawFrameHeader bad(header,31457280); assert(false); } catch(const std::runtime_error&) {}
    try { td::rawEndpoint(endpoint,session+1); assert(false); } catch(const std::runtime_error&) {}
    try { td::rawP010Bytes(4095,2560); assert(false); } catch(const std::runtime_error&) {}
    std::cout<<"Raw P010 sizes, bandwidth, session binding and strict framing passed\n";
}
