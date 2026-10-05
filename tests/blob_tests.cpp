#include "blob.hpp"
#include "cursor_wire.hpp"
#include <cstdlib>
#include <iostream>
#define REQUIRE(x) do { if(!(x)) { std::cerr<<__LINE__<<": "<<#x<<"\n";std::exit(1); } } while(0)
int main() {
    using namespace td;
    Bytes png{0x89,'P','N','G',13,10,26,10};
    Writer cursor;cursor.put(uint8_t(2));cursor.put(uint32_t(23*65536));cursor.put(uint32_t(22*65536));
    cursor.put(int32_t(12*65536));cursor.put(int32_t(11*65536));cursor.put(uint8_t(2));
    cursor.put(uint32_t(png.size()));cursor.bytes(png);cursor.put(uint32_t(png.size()));cursor.bytes(png);
    CursorPayload family(cursor.data);REQUIRE(family.images.size()==2 && family.width==23 && family.height==22 && family.hotX==12 && family.hotY==11);
    Bytes legacy(cursor.data.begin(),cursor.data.begin()+17);legacy[0]=1;legacy.insert(legacy.end(),png.begin(),png.end());
    REQUIRE(CursorPayload(legacy).images==std::vector<Bytes>{png});
    for(int failure:{0,1,2,3,4,5}) {
        auto invalid=cursor.data;
        if(failure==0) invalid[17]=9;
        if(failure==1) invalid[18]=0xff;
        if(failure==2) invalid.pop_back();
        if(failure==3) invalid.push_back(0);
        if(failure==4) invalid[9]=0xff; // Negative click hotspot.
        if(failure==5) invalid[22]=0; // Invalid PNG signature.
        bool rejected=false;try { CursorPayload decoded(invalid); } catch(...) { rejected=true; } REQUIRE(rejected);
    }
    for(auto type:{Message::CursorImage,Message::ClipboardImage}) {
        Bytes image(400000);for(size_t i=0;i<image.size();++i) image[i]=uint8_t(i*17);
        auto packets=blobPackets(type,image,7,ClipboardImageLimit);BlobAssembler assembler;
        std::optional<Bytes> complete;
        for(auto packet:packets) { REQUIRE(packet.size()<=ControlLimit);complete=assembler.push(packet,type,ClipboardImageLimit); }
        REQUIRE(complete && *complete==image);
        for(auto invalid:{0,1,2}) {
            assembler.reset();auto packet=packets.front();
            if(invalid==0) packet[5]=0x7f; // Refuse oversized transfer before allocation.
            if(invalid==1) packet=packets[1]; // No transfer may start at a nonzero offset.
            if(invalid==2) packet[0]=uint8_t(Message::Input);
            bool failed=false;try { assembler.push(packet,type,ClipboardImageLimit); } catch(...) { failed=true; } REQUIRE(failed);
        }
        assembler.reset();REQUIRE(!assembler.push(packets.front(),type,ClipboardImageLimit));
        auto newer=blobPackets(type,Bytes{1,2,3},8,ClipboardImageLimit);
        REQUIRE(assembler.push(newer.front(),type,ClipboardImageLimit)==std::optional<Bytes>(Bytes{1,2,3}));
    }
    Settings settings;settings.bitrate=20000000000ULL;
    auto wide=hello(settings,50000,"",true);REQUIRE(wide.size()==52 && wide[0]==15 && wide[2]==2);
    Reader reader(wide);reader.pos=11;REQUIRE(reader.get<uint64_t>()==20000000000ULL);
    bool failed=false;try { hello(settings,50000,""); } catch(...) { failed=true; } REQUIRE(failed);
    settings.bitrate=1000000000;REQUIRE(hello(settings,50000,"").size()==48);
    std::cout<<"Binary transfers, bounds, replacement and 64-bit bitrate tests passed\n";
}
